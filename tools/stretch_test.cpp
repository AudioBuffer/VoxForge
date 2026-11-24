// stretch_test - 把慢速合成压到目标速度
//   用法: stretch_test <in.wav> <out.wav> <rate>   (rate<1 加快)
#include "timestretch.h"
#include "wav.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
bool loadWav(const char* path, std::vector<float>& out, int& sr) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    unsigned char h[44];
    if (std::fread(h, 1, 44, f) != 44) { std::fclose(f); return false; }
    sr = h[24] | (h[25] << 8) | (h[26] << 16) | (h[27] << 24);
    const int ch = h[22] | (h[23] << 8);
    std::vector<unsigned char> raw;
    unsigned char buf[8192];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) raw.insert(raw.end(), buf, buf + n);
    std::fclose(f);
    const size_t frames = raw.size() / (2 * (size_t)ch);
    out.resize(frames);
    for (size_t i = 0; i < frames; ++i) {
        float acc = 0;
        for (int c = 0; c < ch; ++c) {
            int16_t s;
            std::memcpy(&s, &raw[(i * ch + c) * 2], 2);
            acc += s / 32768.0f;
        }
        out[i] = acc / ch;
    }
    return true;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) { std::fprintf(stderr, "用法: stretch_test <in.wav> <out.wav> <rate>\n"); return 2; }
    std::vector<float> x;
    int sr = 44100;
    if (!loadWav(argv[1], x, sr)) { std::fprintf(stderr, "读取失败\n"); return 1; }
    const double rate = std::atof(argv[3]);
    auto y = vx::timeStretch(x, rate, sr);
    float pk = 1e-6f;
    for (float v : y) pk = std::max(pk, std::fabs(v));
    for (float& v : y) v = std::max(-0.95f, std::min(0.95f, v * (0.85f / pk)));
    vx::writeWav16(argv[2], y, sr);
    std::printf("%s  %.2fs -> %.2fs (rate %.2f)\n", argv[2], x.size() / (double)sr,
                y.size() / (double)sr, rate);
    return 0;
}
