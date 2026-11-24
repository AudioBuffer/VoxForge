// compare - 两段语音的频谱相似度（对数梅尔 + DTW）
//
// 用法: compare a.wav b.wav
// 输出 0~100 的相似度百分比。
//
// 做法：
//   1. 重采样到 16k（线性插值）
//   2. 25ms 帧 / 10ms 帧移，Hamming 窗，512 点 FFT
//   3. 26 个梅尔滤波器取对数能量
//   4. 逐帧余弦相似度 + DTW 对齐（代价 = 1 - 余弦）
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr int kSR = 16000;
constexpr int kNfft = 512;
constexpr int kFrame = 400;     // 25ms
constexpr int kHop = 160;       // 10ms
constexpr int kMel = 26;

struct Wav {
    std::vector<float> x;
};

bool loadWav(const char* path, Wav& out) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    unsigned char hdr[44];
    if (std::fread(hdr, 1, 44, f) != 44) { std::fclose(f); return false; }
    const int ch = hdr[22] | (hdr[23] << 8);
    const int sr = hdr[24] | (hdr[25] << 8) | (hdr[26] << 16) | (hdr[27] << 24);
    const int bits = hdr[34] | (hdr[35] << 8);
    std::vector<unsigned char> raw;
    unsigned char buf[8192];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) raw.insert(raw.end(), buf, buf + n);
    std::fclose(f);
    if (bits != 16) return false;

    std::vector<float> mono;
    const size_t frames = raw.size() / (2 * (size_t)ch);
    mono.resize(frames);
    for (size_t i = 0; i < frames; ++i) {
        float acc = 0;
        for (int c = 0; c < ch; ++c) {
            int16_t s;
            std::memcpy(&s, &raw[(i * ch + c) * 2], 2);
            acc += s / 32768.0f;
        }
        mono[i] = acc / ch;
    }
    // 重采样到 16k
    if (sr != kSR) {
        const size_t m = (size_t)((double)mono.size() * kSR / sr);
        out.x.resize(m);
        for (size_t i = 0; i < m; ++i) {
            const double t = (double)i * sr / kSR;
            const size_t i0 = (size_t)t;
            const size_t i1 = std::min(i0 + 1, mono.size() - 1);
            const double fr = t - i0;
            out.x[i] = (float)(mono[i0] * (1 - fr) + mono[i1] * fr);
        }
    } else {
        out.x = mono;
    }
    return true;
}

void fft(std::vector<std::complex<float>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2 * M_PI / (double)len;
        const std::complex<float> w((float)std::cos(ang), (float)std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> wk(1);
            for (size_t j = 0; j < len / 2; ++j) {
                const auto u = a[i + j];
                const auto v = a[i + j + len / 2] * wk;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                wk *= w;
            }
        }
    }
}

double hzToMel(double hz) { return 2595.0 * std::log10(1.0 + hz / 700.0); }
double melToHz(double m) { return 700.0 * (std::pow(10.0, m / 2595.0) - 1.0); }

std::vector<std::vector<float>> melFrames(const std::vector<float>& x) {
    const int half = kNfft / 2;
    // 梅尔滤波器组
    std::vector<std::vector<float>> fb((size_t)kMel, std::vector<float>((size_t)half + 1, 0.0f));
    const double lo = hzToMel(80.0), hi = hzToMel(7600.0);
    std::vector<double> centers((size_t)kMel + 2);
    for (int i = 0; i < kMel + 2; ++i)
        centers[(size_t)i] = melToHz(lo + (hi - lo) * i / (kMel + 1));
    for (int m = 0; m < kMel; ++m) {
        for (int k = 0; k <= half; ++k) {
            const double f = k * (double)kSR / kNfft;
            const double a = centers[(size_t)m], b = centers[(size_t)m + 1], c = centers[(size_t)m + 2];
            double w = 0.0;
            if (f >= a && f <= b && b > a) w = (f - a) / (b - a);
            else if (f > b && f <= c && c > b) w = (c - f) / (c - b);
            fb[(size_t)m][(size_t)k] = (float)w;
        }
    }

    std::vector<float> win((size_t)kFrame);
    for (int i = 0; i < kFrame; ++i)
        win[(size_t)i] = (float)(0.54 - 0.46 * std::cos(2 * M_PI * i / (kFrame - 1)));

    std::vector<std::vector<float>> out;
    for (size_t s = 0; s + kFrame < x.size(); s += kHop) {
        std::vector<std::complex<float>> buf((size_t)kNfft, 0.0f);
        for (int i = 0; i < kFrame; ++i) buf[(size_t)i] = x[s + (size_t)i] * win[(size_t)i];
        fft(buf);
        std::vector<float> feat((size_t)kMel, 0.0f);
        for (int m = 0; m < kMel; ++m) {
            double e = 0;
            for (int k = 0; k <= half; ++k)
                e += (double)fb[(size_t)m][(size_t)k] * std::norm(buf[(size_t)k]);
            feat[(size_t)m] = (float)std::log(std::max(1e-10, e));
        }
        out.push_back(std::move(feat));
    }
    return out;
}

double cosine(const std::vector<float>& a, const std::vector<float>& b) {
    // 帧内去均值：否则所有语音帧都高度相似，评分失去区分度
    double ma = 0, mb = 0;
    for (size_t i = 0; i < a.size(); ++i) { ma += a[i]; mb += b[i]; }
    ma /= (double)a.size();
    mb /= (double)b.size();
    double d = 0, na = 0, nb = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        const double x = a[i] - ma, y = b[i] - mb;
        d += x * y; na += x * x; nb += y * y;
    }
    // 静音帧：两边都没内容算相同，一边有一边没有算部分相同
    const bool quietA = na < 1e-6, quietB = nb < 1e-6;
    if (quietA && quietB) return 1.0;
    if (quietA || quietB) return 0.35;
    return d / std::sqrt(na * nb);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "用法: compare a.wav b.wav\n"); return 2; }
    Wav a, b;
    if (!loadWav(argv[1], a) || !loadWav(argv[2], b)) {
        std::fprintf(stderr, "读取失败\n");
        return 1;
    }
    auto fa = melFrames(a.x);
    auto fb2 = melFrames(b.x);
    if (fa.empty() || fb2.empty()) { std::fprintf(stderr, "帧为空\n"); return 1; }

    const size_t N = fa.size(), M = fb2.size();
    std::vector<std::vector<float>> cost(N, std::vector<float>(M, 1.0f));
    for (size_t i = 0; i < N; ++i)
        for (size_t j = 0; j < M; ++j)
            cost[i][j] = (float)(1.0 - cosine(fa[i], fb2[j]));

    // DTW 同时累计路径长度，代价按路径长度归一（否则长音频会被低估）
    std::vector<std::vector<float>> dp(N, std::vector<float>(M, 1e9f));
    std::vector<std::vector<int>> len(N, std::vector<int>(M, 0));
    dp[0][0] = cost[0][0];
    len[0][0] = 1;
    for (size_t i = 1; i < N; ++i) { dp[i][0] = dp[i - 1][0] + cost[i][0]; len[i][0] = len[i - 1][0] + 1; }
    for (size_t j = 1; j < M; ++j) { dp[0][j] = dp[0][j - 1] + cost[0][j]; len[0][j] = len[0][j - 1] + 1; }
    for (size_t i = 1; i < N; ++i) {
        for (size_t j = 1; j < M; ++j) {
            const float d1 = dp[i - 1][j], d2 = dp[i][j - 1], d3 = dp[i - 1][j - 1];
            const float best = std::min({d1, d2, d3});
            int l = 0;
            if (best == d3) l = len[i - 1][j - 1];
            else if (best == d1) l = len[i - 1][j];
            else l = len[i][j - 1];
            dp[i][j] = cost[i][j] + best;
            len[i][j] = l + 1;
        }
    }

    const double avgCost = dp[N - 1][M - 1] / (double)std::max(1, len[N - 1][M - 1]);
    const double sim = std::max(0.0, std::min(1.0, 1.0 - avgCost));
    std::printf("帧数 %zu vs %zu  DTW 代价 %.3f  相似度 %.1f%%\n", N, M, avgCost, sim * 100.0);
    return 0;
}
