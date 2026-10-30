// Wake word on the device: the AFE front end (one microphone, no AEC) feeding MultiNet5
// Chinese command words. Compiled only when CONFIG_HG_WAKE_MULTINET is on, and only then does
// main/CMakeLists.txt require esp-sr and esp_audio_effects (see the guard there).
//
// The engine never opens a second microphone: core hands it the one the app records through
// (wake_attach in main.cpp, once at startup), it starts and stops that microphone itself while
// it owns it, and it takes its samples from the mic tap the audio port fires on every chunk.
// Core decides ownership: hg::wake_pause() before a recording opens the microphone at its own
// rate, hg::wake_resume() whenever the device is idle again (App::update_wake).
#include "port.hpp"  // first: pulls in FreeRTOS.h ahead of task.h/queue.h

#include <atomic>
#include <cstring>

#include "esp_afe_config.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_ae_rate_cvt.h"
#include "esp_log.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "esp_process_sdkconfig.h"  // esp_mn_commands_update_from_sdkconfig: the Kconfig words
#include "model_path.h"

namespace hgp {
namespace {

const char* TAG = "hg.wake";
// Every codec board shares one I2S clock for the microphones and the speaker (CodecAudio::kRate),
// so the engine cannot ask for 16 kHz directly: the tap converts 24 kHz down with the official
// rate converter. This is the path the design settled on (notes/设计-语音唤醒.md §4).
constexpr uint32_t kCodecRate = CodecAudio::kRate;
constexpr uint32_t kAfeRate = 16000;
constexpr size_t kQueueBytes = 16 * 1024;  // ~0.5 s of 16 kHz PCM16, absorbs a slow fetch
constexpr int kCommandWindowMs = 6000;     // how long MultiNet keeps listening for a word
constexpr int16_t kMaxHitScore = 100;

hg::AudioIn* g_mic = nullptr;
srmodel_list_t* g_models = nullptr;
const esp_afe_sr_iface_t* g_afe = nullptr;
esp_afe_sr_data_t* g_afe_data = nullptr;
esp_mn_iface_t* g_mn = nullptr;
model_iface_data_t* g_mn_data = nullptr;
esp_ae_rate_cvt_handle_t g_cvt = nullptr;
StreamBufferHandle_t g_queue = nullptr;
int g_feed_chunk = 0;  // samples per AFE feed call, at kAfeRate
std::atomic<bool> g_ready{false};
// Diagnostics for the first bring-up: counts on the tap side and on the AFE side.
uint32_t g_tap_calls = 0;
uint32_t g_tap_samples = 0;
uint32_t g_feeds = 0;
int g_feed_errors = 0;
int g_tap_peak = 0;
int g_frames_seen = 0;
std::atomic<bool> g_running{false};

// The tap runs on the microphone task, so both buffers live here rather than on its stack.
int16_t g_tap_out[kCodecRate / 10];  // 100 ms at 24 kHz worth of output: 1600 samples is plenty
int16_t g_feed[kAfeRate / 10];

// Microphone chunk -> 16 kHz -> the queue the engine task feeds the AFE from.
void on_mic(const int16_t* samples, size_t count) {
  if (!g_running.load(std::memory_order_relaxed) || !g_cvt || !g_queue) return;
  uint32_t room = 0;
  if (esp_ae_rate_cvt_get_max_out_sample_num(g_cvt, count, &room) != ESP_AE_ERR_OK) return;
  if (room * sizeof(int16_t) > sizeof(g_tap_out)) room = sizeof(g_tap_out) / sizeof(int16_t);
  uint32_t produced = room;
  // esp_ae_sample_t is void*: the converter neither writes the input nor outlives the call.
  auto* in = const_cast<int16_t*>(samples);
  if (esp_ae_rate_cvt_process(g_cvt, reinterpret_cast<esp_ae_sample_t>(in), count,
                              reinterpret_cast<esp_ae_sample_t>(g_tap_out), &produced) != ESP_AE_ERR_OK) {
    return;
  }
  size_t sent = 0;
  if (produced) sent = xStreamBufferSend(g_queue, g_tap_out, produced * sizeof(int16_t), 0);
#if defined(HG_WAKE_DIAG)
  int peak = 0;
  for (size_t i = 0; i < count; ++i) {
    const int v = samples[i] < 0 ? -samples[i] : samples[i];
    if (v > peak) peak = v;
  }
  if (peak > g_tap_peak) g_tap_peak = peak;
  ++g_tap_calls;
  g_tap_samples += static_cast<uint32_t>(count);
  if (g_tap_calls <= 3 || g_tap_calls % 200 == 0) {
    ESP_LOGI(TAG, "tap: %u calls, %u samples in, %u out, sent %zu B, peak %d, queue free %u B",
             static_cast<unsigned>(g_tap_calls), static_cast<unsigned>(g_tap_samples),
             static_cast<unsigned>(produced), sent, static_cast<int>(g_tap_peak),
             static_cast<unsigned>(xStreamBufferSpacesAvailable(g_queue)));
    g_tap_peak = 0;
  }
#else
  (void)sent;  // only the diagnostics print it
#endif
}

void report_hit() {
  esp_mn_results_t* results = g_mn->get_results(g_mn_data);
  if (!results || results->num <= 0) return;
  WakeHitSample hit{};
  hit.command_id = static_cast<int16_t>(results->command_id[0]);
  hit.score = results->prob[0];
  ESP_LOGI(TAG, "hit: command %d at %d percent", hit.command_id,
           static_cast<int>(hit.score * kMaxHitScore + 0.5f));
  events::post(EventType::WakeHit, &hit, sizeof(hit));
}

void task(void*) {
  ESP_LOGI(TAG, "task: started, feed chunk %d samples (%u B)", g_feed_chunk,
           static_cast<unsigned>(g_feed_chunk * 2));
  const size_t want = static_cast<size_t>(g_feed_chunk) * sizeof(int16_t);
  size_t filled = 0;  // a stream buffer hands back whatever is queued, so frames are assembled here
  for (;;) {
    if (!g_running.load()) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    const size_t got = xStreamBufferReceive(g_queue, reinterpret_cast<uint8_t*>(g_feed) + filled,
                                            want - filled, pdMS_TO_TICKS(100));
    if (!got) continue;
    filled += got;
    if (filled < want) continue;  // keep the partial frame for the next round
    filled = 0;

    const int fed = g_afe->feed(g_afe_data, g_feed);
    if (fed < 0) {
      if (g_feed_errors < 5) {
        ESP_LOGE(TAG, "feed failed: %d (frame %d samples)", fed, g_feed_chunk);
        ++g_feed_errors;
      }
      continue;
    }
#if defined(HG_WAKE_DIAG)
    if (++g_feeds % 200 == 0) {
      ESP_LOGI(TAG, "feeds %u", static_cast<unsigned>(g_feeds));
    }
#endif
    // One fetch per feed: the frame just queued is what comes back, and a pause does not have
    // to wait out a blocking fetch in the AFE.
    afe_fetch_result_t* fetched = g_afe->fetch_with_delay(g_afe_data, pdMS_TO_TICKS(50));
    if (!fetched || fetched->ret_value == ESP_FAIL || !fetched->data || fetched->data_size <= 0) continue;
    if (g_frames_seen < 2) {
      ESP_LOGI(TAG, "afe frame: %d samples", fetched->data_size / 2);
      ++g_frames_seen;
    }
    const esp_mn_state_t state = g_mn->detect(g_mn_data, fetched->data);
    if (state == ESP_MN_STATE_DETECTED) {
      report_hit();
      g_mn->clean(g_mn_data);  // the same utterance must not fire twice
    } else if (state == ESP_MN_STATE_TIMEOUT) {
      g_mn->clean(g_mn_data);
    }
  }
}

}  // namespace

}  // namespace hgp

// ---------------------------------------------------------------------------
// The seam core links weakly (firmware/core/src/wake.cpp).
// These four MUST be in namespace hg, the one the core calls: hgp is a separate namespace,
// so defining them there leaves the weak stubs in core in force and the engine is never
// resumed - the board looks healthy and never hears anything.

namespace hg {

bool wake_available() { return hgp::g_ready.load(); }

void wake_pause() {
  if (!hgp::g_running.exchange(false)) return;
  ESP_LOGI(hgp::TAG, "pause: tap off");
  hgp::set_mic_tap(nullptr);  // no more samples reach the engine from here on
  // The microphone is deliberately left running. Measured on the moji board: closing the
  // ES8311 ADC here and re-opening it in start_listening(), right after the speaker played the
  // acknowledgement cue, hands the turn nothing but zeros for as long as it lasts ("recording:
  // still silent after 400 chunks"). Whoever paused the engine - a turn, or the cue before it -
  // owns the microphone from here on and closes it itself.
  // Forget what it heard before the pause: the user has been talking into the recording, and
  // those frames never reached the engine anyway.
  if (hgp::g_afe && hgp::g_afe_data) hgp::g_afe->reset_buffer(hgp::g_afe_data);
  if (hgp::g_mn && hgp::g_mn_data) hgp::g_mn->clean(hgp::g_mn_data);
}

void wake_resume() {
  if (!hgp::g_ready.load() || hgp::g_running.load()) return;
  if (hgp::g_mn && hgp::g_mn_data) hgp::g_mn->clean(hgp::g_mn_data);
  if (hgp::g_cvt) esp_ae_rate_cvt_reset(hgp::g_cvt);
  if (hgp::g_queue) xStreamBufferReset(hgp::g_queue);
  if (hgp::g_mic) {
    const bool started = hgp::g_mic->start(hgp::kCodecRate);
    ESP_LOGI(hgp::TAG, "resume: microphone %s", started ? "started" : "did NOT start");
    if (!started) return;
  }
  hgp::g_running = true;
  hgp::set_mic_tap(hgp::on_mic);  // the tap goes last: samples flow once everything is set
  ESP_LOGI(hgp::TAG, "resume: tap on");
}

void wake_feed(const int16_t*, size_t) {
  // Unused on this port: the engine taps the microphone itself (see on_mic).
}

}  // namespace hg

// ---------------------------------------------------------------------------
// Startup

namespace hgp {

// Release everything wake_attach built before it failed. The engine is optional — the board is
// still a reminder screen without it — so a half-built one must not keep its PSRAM, its stream
// buffer, or the model partition's SPIFFS handle open for the rest of the boot. Every failure
// path below calls this, so a retry starts from zero instead of leaking a second copy.
static void wake_release() {
  if (g_queue) {
    vStreamBufferDelete(g_queue);
    g_queue = nullptr;
  }
  if (g_cvt) {
    esp_ae_rate_cvt_close(g_cvt);
    g_cvt = nullptr;
  }
  if (g_mn_data && g_mn) g_mn->destroy(g_mn_data);
  g_mn_data = nullptr;
  if (g_afe_data && g_afe) g_afe->destroy(g_afe_data);
  g_afe_data = nullptr;
  g_afe = nullptr;
  if (g_models) {
    esp_srmodel_deinit(g_models);
    g_models = nullptr;
  }
  g_feed_chunk = 0;
}

void wake_attach(hg::AudioIn* mic) {
  g_mic = mic;
  if (g_ready.load()) return;

  srmodel_list_t* models = esp_srmodel_init("model");
  char* mn_name = models ? esp_srmodel_filter(models, ESP_MN_PREFIX, ESP_MN_CHINESE) : nullptr;
  if (!mn_name) {
    ESP_LOGE(TAG, "wake word: no Chinese MultiNet model in the `model` partition (%s)",
             models ? "model partition empty" : "partition `model` missing");
    if (models) esp_srmodel_deinit(models);
    return;
  }
  // Kept so a later failure can release it: the model list holds the SPIFFS handle open.
  g_models = models;

  afe_config_t* cfg = afe_config_init("M", models, AFE_TYPE_SR, AFE_MODE_LOW_COST);
  if (!cfg) {
    ESP_LOGE(TAG, "wake word: afe_config_init failed; the engine stays off");
    wake_release();
    return;
  }
  // One analog microphone and no clean playback reference, so no AEC: the app keeps the engine
  // paused while anything plays instead (App::update_wake). WakeNet stays off: MultiNet is the
  // detector, and its command words are Kconfig strings rather than a trained wake word.
  cfg->aec_init = false;
  cfg->se_init = false;
  cfg->wakenet_init = false;
  cfg->pcm_config.sample_rate = kAfeRate;  // the tap delivers 16 kHz
  g_afe = esp_afe_handle_from_config(cfg);
  g_afe_data = g_afe ? g_afe->create_from_config(cfg) : nullptr;
  afe_config_free(cfg);
  if (!g_afe_data) {
    ESP_LOGE(TAG, "wake word: the AFE did not start (out of PSRAM?); the engine stays off");
    wake_release();
    return;
  }

  g_mn = esp_mn_handle_from_name(mn_name);
  g_mn_data = g_mn ? g_mn->create(mn_name, kCommandWindowMs) : nullptr;
  if (!g_mn_data) {
    ESP_LOGE(TAG, "wake word: MultiNet model %s did not start; the engine stays off", mn_name);
    wake_release();
    return;
  }
  // Lower Kconfig value = more sensitive, which is the same direction as the model's probability
  // threshold. The words themselves come from esp-sr's CN_SPEECH_COMMAND_ID0/1 (set by the
  // board's sdkconfig.defaults), so changing a wake word needs no code change.
  // Nothing registers the sdkconfig command words on its own: without this call MultiNet has
  // zero phrases and never reports a detection, whatever the audio sounds like.
  esp_mn_error_t* rejected = esp_mn_commands_update_from_sdkconfig(g_mn, g_mn_data);
  if (rejected && rejected->num > 0) {
    ESP_LOGW(TAG, "wake word: %d command(s) rejected by the model", rejected->num);
  }
  ESP_LOGI(TAG, "wake word: %d samples per MultiNet frame", g_mn->get_samp_chunksize(g_mn_data));

  // Kconfig gives a percentage: lower is more sensitive, the same direction as the model's
  // probability threshold. Bring-up pinned this to 0.05 to print every command's score; the
  // shipped value is the Kconfig one again, and the log below prints the value actually set so
  // the README's expected log can't drift away from the code.
  const float threshold = CONFIG_HG_WAKE_THRESHOLD / 100.0f;
  g_mn->set_det_threshold(g_mn_data, threshold);

  esp_ae_rate_cvt_cfg_t cvt = {};
  cvt.src_rate = kCodecRate;
  cvt.dest_rate = kAfeRate;
  cvt.channel = 1;
  cvt.bits_per_sample = 16;
  cvt.complexity = 2;  // middle of 1..3: quality matters more than a few percent of one core
  cvt.perf_type = ESP_AE_RATE_CVT_PERF_TYPE_SPEED;
  if (esp_ae_rate_cvt_open(&cvt, &g_cvt) != ESP_AE_ERR_OK || !g_cvt) {
    ESP_LOGE(TAG, "wake word: the rate converter did not start; the engine stays off");
    g_cvt = nullptr;
    wake_release();
    return;
  }

  g_queue = xStreamBufferCreate(kQueueBytes, sizeof(int16_t));
  if (!g_queue) {
    ESP_LOGE(TAG, "wake word: out of memory for the sample queue; the engine stays off");
    wake_release();
    return;
  }
  g_feed_chunk = g_afe->get_feed_chunksize(g_afe_data);
  if (g_feed_chunk <= 0 || static_cast<size_t>(g_feed_chunk) > sizeof(g_feed) / sizeof(int16_t)) {
    ESP_LOGE(TAG, "wake word: unexpected AFE frame size %d; the engine stays off", g_feed_chunk);
    wake_release();
    return;
  }
  xTaskCreate(&task, "hg-wake", 4096, nullptr, 5, nullptr);
  g_ready = true;
  ESP_LOGI(TAG, "wake word ready: %s, threshold %.2f, frame %d samples at %u Hz", mn_name,
           static_cast<double>(threshold), g_feed_chunk, static_cast<unsigned>(kAfeRate));
}

}  // namespace hgp
