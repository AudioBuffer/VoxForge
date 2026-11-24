// rt_test - 往返测试：波形 -> 梅尔 -> 声码器 -> 波形
//   用法: rt_test <in.wav> <out.wav> [mels]
#include "vocoder.h"
#include "wav.h"
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <cstdlib>
#include <complex>

namespace {

bool loadWav(const char* path, std::vector<float>& out, int& sr) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    unsigned char hdr[44];
    if (std::fread(hdr, 1, 44, f) != 44) { std::fclose(f); return false; }
    sr = hdr[24] | (hdr[25] << 8) | (hdr[26] << 16) | (hdr[27] << 24);
    const int ch = hdr[22] | (hdr[23] << 8);
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
    if (argc < 3) { std::fprintf(stderr, "用法: rt_test <in.wav> <out.wav> [mels]\n"); return 2; }
    std::vector<float> x;
    int sr = 44100;
    if (!loadWav(argv[1], x, sr)) { std::fprintf(stderr, "读取失败\n"); return 1; }
    const int nMels = (argc > 3) ? std::atoi(argv[3]) : 80;
    const int nfft = 1024, hop = 256;

    vx::MelBank bank(sr, nfft, nMels);

    // 分析：波形 -> 梅尔
    std::vector<std::vector<float>> mels;
    const size_t nFrames = (x.size() > (size_t)nfft) ? (x.size() - nfft) / hop : 0;
    for (size_t t = 0; t < nFrames; ++t) {
        std::vector<std::complex<float>> buf((size_t)nfft, 0.0f);
        const double w = 0.5 - 0.5 * std::cos(2 * M_PI / (nfft - 1));
        for (int i = 0; i < nfft; ++i) {
            double win = 0.5 - 0.5 * std::cos(2 * M_PI * i / (nfft - 1));
            (void)w;
            buf[(size_t)i] = x[t * hop + (size_t)i] * (float)win;
        }
        vx::Fft::forward(buf);
        std::vector<float> mag((size_t)bank.bins());
        for (int k = 0; k < bank.bins(); ++k) mag[(size_t)k] = std::abs(buf[(size_t)k]);
        mels.push_back(bank.toMel(mag));
    }
    std::printf("帧 %zu, 梅尔 %d\n", mels.size(), nMels);

    // 逐帧 F0（自相关，70–500Hz）
    std::vector<float> f0(mels.size(), 0.0f);
    for (size_t t = 0; t < mels.size(); ++t) {
        const size_t off = t * hop;
        const size_t W = 1024;
        if (off + W >= x.size()) break;
        double best = 0.0; int bestLag = 0;
        for (int lag = sr / 500; lag <= sr / 70; ++lag) {
            double c = 0.0;
            for (int i = 0; i < W - lag; i += 2) c += (double)x[off + i] * x[off + i + lag];
            if (c > best) { best = c; bestLag = lag; }
        }
        f0[t] = bestLag > 0 ? (float)((double)sr / bestLag) : 0.0f;
    }

    const bool useF0 = (std::getenv("NO_F0") == nullptr);
    const int iters = std::getenv("GL_ITERS") ? std::atoi(std::getenv("GL_ITERS")) : 40;
    auto y = vx::melToWave(bank, mels, hop, iters, useF0 ? &f0 : nullptr);
    float pk = 1e-6f;
    for (float v : y) pk = std::max(pk, std::fabs(v));
    for (float& v : y) v = std::max(-0.95f, std::min(0.95f, v * (0.85f / pk)));

    vx::writeWav16(argv[2], y, sr);
    std::printf("%s  %.2fs\n", argv[2], y.size() / (double)sr);
    return 0;
}
