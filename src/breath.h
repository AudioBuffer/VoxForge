// 呼吸声与非周期成分
//
// 人声里有两类非周期信号，纯共振峰合成做不出来：
//   吸气声 (inhale)：短、亮，中心 1.5–2.5kHz，多在句首
//   呼气声 (exhale)：长、闷，低频为主，多在句尾
//   气声   (breathy)：与浊音同时存在，开相噪声
//
// 这里用「噪声 + 带通 + 包络」生成，参数按听感调过。
#pragma once
#include <cmath>
#include <random>
#include <vector>

namespace vx {

enum class BreathKind { kInhale, kExhale, kSigh };

class BreathSynth {
public:
    explicit BreathSynth(double fs) : fs_(fs), rng_(12345u) {}

    // 生成一段呼吸声（单声道，幅度约 ±0.3）
    std::vector<float> render(BreathKind kind, double durSec) {
        const size_t n = (size_t)std::max(1.0, durSec * fs_);
        std::vector<float> out(n, 0.0f);

        // 参数：中心频率 / 带宽 / 增益 / 起振 / 衰减
        double fc = 1800.0, bw = 1400.0, gain = 0.30, attack = 0.06, release = 0.12;
        if (kind == BreathKind::kExhale) { fc = 700.0; bw = 900.0; gain = 0.22; attack = 0.12; release = 0.30; }
        if (kind == BreathKind::kSigh)   { fc = 1100.0; bw = 1600.0; gain = 0.26; attack = 0.10; release = 0.35; }

        // RBJ 带通
        const double w0 = 2 * M_PI * fc / fs_;
        const double q = std::max(0.5, fc / bw);
        const double alpha = std::sin(w0) / (2 * q);
        const double b0 = alpha, b2 = -alpha;
        const double a0 = 1 + alpha, a1 = -2 * std::cos(w0), a2 = 1 - alpha;

        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        std::uniform_real_distribution<double> uni(-1.0, 1.0);
        for (size_t i = 0; i < n; ++i) {
            const double t = i / fs_;
            const double left = durSec - t;
            double env = 1.0;
            if (t < attack) env = t / attack;
            if (left < release) env = std::min(env, std::max(0.0, left / release));
            // 呼吸不是纯平稳噪声，加一点缓慢起伏
            env *= 0.85 + 0.15 * std::sin(2 * M_PI * 1.7 * t);

            const double x = uni(rng_);
            const double y = (b0 / a0) * x + (b2 / a0) * x2 - (a1 / a0) * y1 - (a2 / a0) * y2;
            x2 = x1; x1 = x;
            y2 = y1; y1 = y;
            out[i] = (float)(y * gain * env);
        }
        return out;
    }

private:
    double fs_;
    std::mt19937 rng_;
};

}  // namespace vx
