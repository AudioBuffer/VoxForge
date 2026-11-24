// 空气感层：补上高频气声
//
// 真人的「空气感」来自 6–14kHz 的弱噪声，能量只有主体的 -30dB 左右，
// 但耳朵对这段极其敏感，缺了就显得闷、死。
//
// 做法：白噪声 -> 带通(8kHz) -> 乘上语音包络 -> 低电平叠加
#pragma once
#include <cmath>
#include <cstddef>
#include <random>
#include <vector>

namespace vx {

class AirLayer {
public:
    explicit AirLayer(double fs) : fs_(fs), rng_(20260401u) {}

    // pcm: 原始干声；amount: 叠加量（0.01~0.05 比较自然）
    void apply(std::vector<float>& pcm, double amount = 0.03,
               double centerHz = 8000.0, double q = 0.7) {
        if (pcm.empty() || amount <= 0.0) return;

        // RBJ 带通
        const double w0 = 2 * M_PI * centerHz / fs_;
        const double alpha = std::sin(w0) / (2 * q);
        const double b0 = alpha, b2 = -alpha;
        const double a0 = 1 + alpha, a1 = -2 * std::cos(w0), a2 = 1 - alpha;

        // 包络：语音能量的慢速跟随（10ms 时间常数）
        std::vector<float> env(pcm.size(), 0.0f);
        const double att = std::exp(-1.0 / (0.005 * fs_));
        const double rel = std::exp(-1.0 / (0.060 * fs_));
        float e = 0.0f;
        for (size_t i = 0; i < pcm.size(); ++i) {
            const float a = std::fabs(pcm[i]);
            e = (a > e) ? (float)(att * e + (1 - att) * a) : (float)(rel * e + (1 - rel) * a);
            env[i] = e;
        }

        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        std::uniform_real_distribution<double> uni(-1.0, 1.0);
        for (size_t i = 0; i < pcm.size(); ++i) {
            const double x = uni(rng_);
            const double y = (b0 / a0) * x + (b2 / a0) * x2 - (a1 / a0) * y1 - (a2 / a0) * y2;
            x2 = x1; x1 = x;
            y2 = y1; y1 = y;
            pcm[i] += (float)(y * env[i] * amount);
        }
    }

private:
    double fs_;
    std::mt19937 rng_;
};

}  // namespace vx
