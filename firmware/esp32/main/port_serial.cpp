// USB Serial/JTAG link: the cable replaces Wi-Fi. Messages travel as SLIP
// frames on the same port the console log uses, so the host relay
// (tools/usb_relay.py) tells them apart by the 0xC0 delimiter. The device needs
// no IP address, and the console keeps working: bytes outside a frame are
// accumulated into lines and run through esp_console_run().
//
// Wire format, mirrored byte for byte by the relay:
//   0xC0 | type | payload | 0xC0
//   inside the payload: 0xC0 -> 0xDB 0xDC, 0xDB -> 0xDB 0xDD
// Types: 0x01 text, 0x02 binary, 0x03 close, 0x04 ping, 0x05 hello,
//        0x06 hello_ack.
#include "sdkconfig.h"

// Off: this file compiles to nothing and the board keeps the WebSocket-over-Wi-Fi
// transport. The declaration stays in port.hpp (an unused class costs nothing); the
// definition must not be compiled when CONFIG_HG_LINK_USB_PATH does not exist.
#if CONFIG_HG_LINK_USB_SERIAL

#include "port.hpp"  // first: pulls in FreeRTOS.h ahead of task.h/queue.h

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

#include "driver/usb_serial_jtag.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace hgp {
namespace {

const char* TAG = "hg.usb";

constexpr uint8_t kEnd = 0xC0;
constexpr uint8_t kEsc = 0xDB;
constexpr uint8_t kEscEnd = 0xDC;
constexpr uint8_t kEscEsc = 0xDD;

constexpr uint8_t kFrameText = 0x01;
constexpr uint8_t kFrameBinary = 0x02;
constexpr uint8_t kFrameClose = 0x03;
constexpr uint8_t kFramePing = 0x04;
constexpr uint8_t kFrameHello = 0x05;
constexpr uint8_t kFrameHelloAck = 0x06;

// A 40 ms audio frame at 24 kHz PCM16 is 1920 B; JSON stays well under this.
constexpr size_t kMaxFrame = 16 * 1024;
constexpr size_t kMaxLine = 256;  // same limit the console REPL uses
constexpr size_t kRxChunk = 512;
constexpr int kTxSlots = 32;

uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

// One writer at a time. A 40 ms audio frame is 1920 B and goes out in 64 B pieces,
// so a log line or a console reply landing between two pieces corrupts the frame
// (the relay then reads a broken payload: bad JSON, or audio that plays as noise).
// Recursive because a console command may log while it holds the lock.
SemaphoreHandle_t tx_lock() {
  static SemaphoreHandle_t lock = xSemaphoreCreateRecursiveMutex();
  return lock;
}

// Writes bytes as they are: caller holds tx_lock().
void raw_write(const void* data, size_t len) {
  if (!len) return;
  usb_serial_jtag_write_bytes(data, len, pdMS_TO_TICKS(100));
}

// Writes one whole buffer without letting anything else in between.
void tx_write(const void* data, size_t len) {
  SemaphoreHandle_t lock = tx_lock();
  if (lock) xSemaphoreTakeRecursive(lock, portMAX_DELAY);
  raw_write(data, len);
  if (lock) xSemaphoreGiveRecursive(lock);
}

// ESP_LOG goes through the same lock, so a log line can never split a frame.
int log_vprintf(const char* fmt, va_list args) {
  char buf[256];
  const int n = std::vsnprintf(buf, sizeof(buf), fmt, args);
  if (n > 0) tx_write(buf, std::min(static_cast<size_t>(n), sizeof(buf) - 1));
  return n;
}

// The link owns stdin when it runs, so the console REPL is not started. Whole
// lines go through the same registered commands, and the answers reach the
// host on stdout, which is this same USB port. The lock spans the command so a
// command's own printf output cannot land inside a frame either.
void run_console_line(const char* line, size_t len) {
  if (!len) return;
  const std::string text(line, len);
  int ret = 0;
  SemaphoreHandle_t lock = tx_lock();
  if (lock) xSemaphoreTakeRecursive(lock, portMAX_DELAY);
  const esp_err_t err = esp_console_run(text.c_str(), &ret);
  char reply[192];
  int n = 0;
  if (err == ESP_ERR_NOT_FOUND) {
    n = std::snprintf(reply, sizeof(reply), "@error unknown command\n");
  } else if (err != ESP_OK) {
    n = std::snprintf(reply, sizeof(reply), "@error %s\n", esp_err_to_name(err));
  }
  if (n > 0) raw_write(reply, std::min(static_cast<size_t>(n), sizeof(reply) - 1));
  if (lock) xSemaphoreGiveRecursive(lock);
}

// Escapes and writes one frame as a unit: everything the link sends waits for
// the frame in progress, and the frame waits for any log or console output.
void write_frame(uint8_t type, const void* data, size_t len) {
  SemaphoreHandle_t lock = tx_lock();
  if (lock) xSemaphoreTakeRecursive(lock, portMAX_DELAY);
  uint8_t buf[64];
  size_t n = 0;
  const auto flush = [&] {
    if (!n) return;
    raw_write(buf, n);
    n = 0;
  };
  const auto put = [&](uint8_t b) {
    if (n == sizeof(buf)) flush();
    buf[n++] = b;
  };
  put(kEnd);
  put(type);
  const auto* p = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < len; ++i) {
    const uint8_t b = p[i];
    if (b == kEnd) {
      put(kEsc);
      put(kEscEnd);
    } else if (b == kEsc) {
      put(kEsc);
      put(kEscEsc);
    } else {
      put(b);
    }
  }
  put(kEnd);
  flush();
  if (lock) xSemaphoreGiveRecursive(lock);
}

}  // namespace

bool SerialLink::begin() {
  if (tx_) return true;
  usb_serial_jtag_driver_config_t cfg = {};
  cfg.rx_buffer_size = 8192;
  cfg.tx_buffer_size = 4096;
  // The console already drives this peripheral; installing the driver on it is
  // still allowed and is what gives us read access.
  const esp_err_t err = usb_serial_jtag_driver_install(&cfg);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "usb_serial_jtag_driver_install: %s", esp_err_to_name(err));
    return false;
  }
  tx_ = xQueueCreate(kTxSlots, sizeof(Frame));
  if (!tx_) {
    ESP_LOGE(TAG, "no memory for the USB link queue");
    return false;
  }
  // From here on, log output shares the frame lock: a log line must never land
  // inside a frame that is half written.
  esp_log_set_vprintf(&log_vprintf);
  if (xTaskCreate(&SerialLink::rx_task, "hg-usb-rx", 6144, this, 6, nullptr) != pdPASS ||
      xTaskCreate(&SerialLink::tx_task, "hg-usb-tx", 4096, this, 5, nullptr) != pdPASS) {
    ESP_LOGE(TAG, "can't start the USB link tasks");
    return false;
  }
  ESP_LOGI(TAG, "USB link ready; run tools/usb_relay.py on the host");
  // The cable is the network, so the app must try to connect: without this the
  // device waits in Phase::NoNetwork for a Wi-Fi event that never comes, and the
  // relay never gets the hello it needs to open the WebSocket.
  events::post(EventType::NetUp, "USB", 3);
  return true;
}

void SerialLink::connect(const std::string& url, const std::string& subprotocol) {
  gens_.leave();  // whatever the old link still delivers is now stale
  linked_ = false;
  subprotocol_ = subprotocol.empty() ? "hermes-gadget.v1" : subprotocol;
  // The relay owns host and port; the device only says which path to ask for,
  // so the same `server` setting works over Wi-Fi and over the cable.
  path_ = CONFIG_HG_LINK_USB_PATH;
  const auto scheme = url.find("://");
  if (scheme != std::string::npos) {
    const auto slash = url.find('/', scheme + 3);
    if (slash != std::string::npos && slash + 1 < url.size()) path_ = url.substr(slash);
  } else if (!url.empty() && url.front() == '/') {
    path_ = url;
  }
  const std::string hello =
      "{\"v\":1,\"sub\":\"" + subprotocol_ + "\",\"path\":\"" + path_ + "\"}";
  write_frame(kFrameHello, hello.data(), hello.size());
  ESP_LOGI(TAG, "asked the relay for %s (%s)", path_.c_str(), subprotocol_.c_str());
}

void SerialLink::close() {
  gens_.leave();
  linked_ = false;
  // Nothing queued belongs to the link the app just left.
  if (tx_) xQueueReset(tx_);
  write_frame(kFrameClose, nullptr, 0);
}

bool SerialLink::stuck() const { return watch_.stuck(now_ms()); }

bool SerialLink::enqueue(uint8_t type, const void* data, size_t len) {
  if (!tx_ || !linked_ || len > kMaxFrame) return false;
  const uint8_t opcode = type == kFrameBinary ? hg::ws::kBinary : hg::ws::kText;
  if (!hg::ws::may_queue_message(uxQueueSpacesAvailable(tx_), opcode)) return false;  // the link isn't keeping up
  auto* copy = static_cast<uint8_t*>(heap_caps_malloc(len ? len : 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!copy) copy = static_cast<uint8_t*>(malloc(len ? len : 1));
  if (!copy) return false;
  if (len) std::memcpy(copy, data, len);
  const Frame frame{type, static_cast<uint16_t>(len), copy};
  if (xQueueSend(tx_, &frame, 0) != pdTRUE) {
    free(copy);
    return false;
  }
  return true;
}

bool SerialLink::send_text(std::string_view text) { return enqueue(kFrameText, text.data(), text.size()); }

bool SerialLink::send_binary(const uint8_t* data, size_t len) { return enqueue(kFrameBinary, data, len); }

void SerialLink::tx_task(void* arg) {
  auto* self = static_cast<SerialLink*>(arg);
  Frame frame{};
  while (true) {
    if (xQueueReceive(self->tx_, &frame, portMAX_DELAY) != pdTRUE) continue;
    self->watch_.begin(now_ms());
    write_frame(frame.type, frame.data, frame.len);
    self->watch_.end();
    if (frame.data) free(frame.data);
  }
}

void SerialLink::rx_task(void* arg) {
  auto* self = static_cast<SerialLink*>(arg);
  auto* frame = static_cast<uint8_t*>(heap_caps_malloc(kMaxFrame, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!frame) frame = static_cast<uint8_t*>(malloc(kMaxFrame));
  if (!frame) {
    ESP_LOGE(TAG, "no memory for a %u byte receive frame", static_cast<unsigned>(kMaxFrame));
    vTaskDelete(nullptr);
    return;
  }
  uint8_t chunk[kRxChunk];
  char line[kMaxLine];
  size_t line_len = 0;
  bool in_frame = false;
  bool need_type = false;
  bool escaped = false;
  bool overflow = false;
  uint8_t type = 0;
  size_t frame_len = 0;
  while (true) {
    const int n = usb_serial_jtag_read_bytes(chunk, sizeof(chunk), pdMS_TO_TICKS(100));
    for (int i = 0; i < n; ++i) {
      const uint8_t b = chunk[i];
      if (b == kEnd) {
        if (in_frame) {
          if (!overflow && !need_type) self->on_frame(type, frame, frame_len);
          in_frame = need_type = escaped = overflow = false;
          frame_len = 0;
        } else {
          // A line still waiting for its newline, then a frame: run it first.
          if (line_len) run_console_line(line, line_len);
          line_len = 0;
          in_frame = need_type = true;
          escaped = overflow = false;
          frame_len = 0;
        }
        continue;
      }
      if (in_frame) {
        if (need_type) {
          type = b;
          need_type = false;
        } else if (escaped) {
          escaped = false;
          const uint8_t v = b == kEscEnd ? kEnd : b == kEscEsc ? kEsc : b;
          if (frame_len < kMaxFrame) frame[frame_len++] = v;
          else overflow = true;
        } else if (b == kEsc) {
          escaped = true;
        } else if (frame_len < kMaxFrame) {
          frame[frame_len++] = b;
        } else {
          overflow = true;
        }
        continue;
      }
      // Outside a frame: a console line, one byte at a time.
      if (b == '\r') continue;
      if (b == '\n') {
        if (line_len) run_console_line(line, line_len);
        line_len = 0;
      } else if (line_len < kMaxLine) {
        line[line_len++] = static_cast<char>(b);
      } else {
        line_len = 0;  // overlong: the whole line is dropped
      }
    }
    if (in_frame && overflow) {
      ESP_LOGW(TAG, "dropped an oversized frame");
      in_frame = need_type = escaped = overflow = false;
      frame_len = 0;
    }
  }
}

void SerialLink::on_frame(uint8_t type, const uint8_t* data, size_t len) {
  switch (type) {
    case kFrameText:
      if (!events::post(EventType::WsText, data, len, gens_.current())) ESP_LOGW(TAG, "dropped a text frame");
      break;
    case kFrameBinary:
      if (!events::post(EventType::WsBinary, data, len, gens_.current())) ESP_LOGW(TAG, "dropped a binary frame");
      break;
    case kFramePing:
      write_frame(kFramePing, nullptr, 0);
      break;
    case kFrameClose:
      linked_ = false;
      gens_.lost(gens_.current());
      events::post(EventType::WsClosed, "closed", 6, gens_.current());
      break;
    case kFrameHelloAck: {
      const std::string ack(reinterpret_cast<const char*>(data), len);
      if (ack.find("\"ok\":1") != std::string::npos) {
        linked_ = true;
        events::post(EventType::WsOpen, nullptr, 0, gens_.current());
        ESP_LOGI(TAG, "relay connected");
      } else {
        linked_ = false;
        ESP_LOGW(TAG, "relay refused the connection: %s", ack.c_str());
        events::post(EventType::WsClosed, "relay refused", 13, gens_.current());
      }
      break;
    }
    case kFrameHello:
      break;  // only the device sends hello
    default:
      ESP_LOGW(TAG, "unknown frame type 0x%02x (%u bytes)", type, static_cast<unsigned>(len));
      break;
  }
}

}  // namespace hgp

#endif  // CONFIG_HG_LINK_USB_SERIAL
