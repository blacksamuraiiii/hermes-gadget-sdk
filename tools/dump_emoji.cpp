// Dump every pure-emoji look, frame by frame, to binary PPM for eyeballing and GIFs.
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "hg/canvas.hpp"
#include "hg/emoji_face.hpp"

using Frame = std::vector<uint16_t>;

int main() {
  struct {const char* name; hg::emoji::Look look;} looks[] = {
      {"neutral", hg::emoji::Look::Neutral},   {"happy", hg::emoji::Look::Happy},
      {"sad", hg::emoji::Look::Sad},           {"thinking", hg::emoji::Look::Thinking},
      {"sleeping", hg::emoji::Look::Sleeping}, {"listening", hg::emoji::Look::Listening},
      {"speaking", hg::emoji::Look::Speaking}, {"silly", hg::emoji::Look::Silly},
      {"laughing", hg::emoji::Look::Laughing}, {"loving", hg::emoji::Look::Loving},
      {"angry", hg::emoji::Look::Angry},       {"shocked", hg::emoji::Look::Shocked},
      {"embarrassed", hg::emoji::Look::Embarrassed}, {"winking", hg::emoji::Look::Winking},
      {"excited", hg::emoji::Look::Excited},   {"bored", hg::emoji::Look::Bored},
      {"cool", hg::emoji::Look::Cool},         {"hi", hg::emoji::Look::Hi},
      {"connecting", hg::emoji::Look::Connecting}, {"disconnected", hg::emoji::Look::Disconnected}};
  const uint16_t ink = hg::rgb565(232, 238, 242);
  const uint16_t bg = hg::rgb565(10, 14, 20);
  constexpr int W = 240, H = 240;
  for (auto& l : looks) {
    for (uint32_t f = 0; f < 20; ++f) {
      Frame fb(static_cast<size_t>(W) * H, bg);
      hg::Canvas c(fb.data(), W, H, false);
      hg::emoji::draw(c, 20, 20, 200, l.look, f, 0, ink, bg);
      char path[256];
      std::snprintf(path, sizeof(path), "/tmp/emoji-frames/%s-%02u.ppm", l.name, f);
      std::FILE* out = std::fopen(path, "wb");
      if (!out) { std::fprintf(stderr, "cannot open %s\n", path); return 1; }
      std::fprintf(out, "P6\n%d %d\n255\n", W, H);
      for (int i = 0; i < W * H; ++i) {
        const uint16_t p = fb[static_cast<size_t>(i)];
        const uint8_t r = static_cast<uint8_t>(((p >> 11) & 31) * 255 / 31);
        const uint8_t g = static_cast<uint8_t>(((p >> 5) & 63) * 255 / 63);
        const uint8_t b = static_cast<uint8_t>((p & 31) * 255 / 31);
        std::fwrite(&r, 1, 1, out); std::fwrite(&g, 1, 1, out); std::fwrite(&b, 1, 1, out);
      }
      std::fclose(out);
    }
  }
  std::printf("dumped %d looks x 20 frames\n", static_cast<int>(sizeof(looks) / sizeof(looks[0])));
  return 0;
}
