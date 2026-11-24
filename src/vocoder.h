// 声码器：梅尔 -> 线性频谱 -> 波形
//
// 经典做法（非神经网络）：
//   1. 逆梅尔：解 Mel · x = m 的非负最小二乘（乘法更新迭代）
//   2. 相位重建：Griffin-Lim 交替投影
//   3. iSTFT 重叠相加
//
// 全程单声道、无外部依赖。
#pragma once
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

namespace vx {

class Fft {
public:
    // 原地基 2 FFT
    static void forward(std::vector<std::complex<float>>& a) {
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
    static void inverse(std::vector<std::complex<float>>& a) {
        for (auto& v : a) v = std::conj(v);
        forward(a);
        const float n = (float)a.size();
        for (auto& v : a) v = std::conj(v) / n;
    }
};

// 梅尔滤波器组
class MelBank {
public:
    MelBank(int sampleRate, int nFft, int nMels)
        : sr_(sampleRate), nfft_(nFft), nmels_(nMels) {
        const int half = nFft / 2;
        auto hz2mel = [](double hz) { return 2595.0 * std::log10(1.0 + hz / 700.0); };
        auto mel2hz = [](double m) { return 700.0 * (std::pow(10.0, m / 2595.0) - 1.0); };
        const double lo = hz2mel(40.0), hi = hz2mel(sampleRate * 0.5);
        std::vector<double> c((size_t)nMels + 2);
        for (int i = 0; i < nMels + 2; ++i) c[(size_t)i] = mel2hz(lo + (hi - lo) * i / (nMels + 1));

        fb_.assign((size_t)nMels, std::vector<float>((size_t)half + 1, 0.0f));
        for (int m = 0; m < nMels; ++m) {
            for (int k = 0; k <= half; ++k) {
                const double f = k * (double)sampleRate / nFft;
                const double a = c[(size_t)m], b = c[(size_t)m + 1], d = c[(size_t)m + 2];
                double w = 0.0;
                if (f >= a && f <= b && b > a) w = (f - a) / (b - a);
                else if (f > b && f <= d && d > b) w = (d - f) / (d - b);
                fb_[(size_t)m][(size_t)k] = (float)w;
            }
        }
    }

    int bins() const { return nfft_ / 2 + 1; }
    int mels() const { return nmels_; }
    int nfft() const { return nfft_; }
    int sampleRate() const { return sr_; }

    // 线性幅度谱 -> 梅尔能量（对数）
    std::vector<float> toMel(const std::vector<float>& mag) const {
        std::vector<float> out((size_t)nmels_, 0.0f);
        for (int m = 0; m < nmels_; ++m) {
            double e = 0.0;
            for (int k = 0; k < bins(); ++k) e += (double)fb_[(size_t)m][(size_t)k] * mag[(size_t)k] * mag[(size_t)k];
            out[(size_t)m] = (float)std::log(std::max(1e-10, e));
        }
        return out;
    }

    // 逆梅尔：非负最小二乘，乘法更新
    std::vector<float> fromMel(const std::vector<float>& melLog, int iters = 40) const {
        const int K = bins();
        std::vector<float> target((size_t)nmels_);
        for (int m = 0; m < nmels_; ++m) target[(size_t)m] = std::exp(melLog[(size_t)m]);

        std::vector<float> x((size_t)K, 1e-4f);
        std::vector<float> num((size_t)nmels_, 0.0f), den((size_t)K, 1e-9f);
        for (int it = 0; it < iters; ++it) {
            std::fill(num.begin(), num.end(), 0.0f);
            std::fill(den.begin(), den.end(), 1e-9f);
            // M^T (m / (M x))
            for (int m = 0; m < nmels_; ++m) {
                double acc = 0.0;
                for (int k = 0; k < K; ++k) acc += (double)fb_[(size_t)m][(size_t)k] * x[(size_t)k];
                num[(size_t)m] = (float)(target[(size_t)m] / std::max(1e-9, acc));
            }
            for (int m = 0; m < nmels_; ++m)
                for (int k = 0; k < K; ++k)
                    den[(size_t)k] += fb_[(size_t)m][(size_t)k] * num[(size_t)m];
            for (int k = 0; k < K; ++k) x[(size_t)k] *= den[(size_t)k];
        }
        // 解出的是功率谱（梅尔用的是 mag²），开方得到幅度
        for (int k = 0; k < K; ++k) x[(size_t)k] = std::sqrt(std::max(0.0f, x[(size_t)k]));

        // 高频段梅尔带很宽、解不稳定，做 3 点平滑 + 高频滚降，抑制「电流声」
        std::vector<float> sm(x);
        for (int k = 1; k + 1 < K; ++k)
            sm[(size_t)k] = 0.25f * x[(size_t)(k - 1)] + 0.5f * x[(size_t)k] + 0.25f * x[(size_t)(k + 1)];
        const double nyq = sr_ * 0.5;
        for (int k = 0; k < K; ++k) {
            const double f = k * nyq / (K - 1);
            double g = 1.0;
            if (f > 6000.0) g = std::exp(-(f - 6000.0) / 4000.0);   // 6kHz 以上渐降
            sm[(size_t)k] *= (float)g;
        }
        return sm;
    }

private:
    int sr_, nfft_, nmels_;
    std::vector<std::vector<float>> fb_;
};

// 梅尔 -> 波形（Griffin-Lim）
inline std::vector<float> melToWave(const MelBank& bank, const std::vector<std::vector<float>>& melFrames,
                                    int hop, int glIters = 40,
                                    const std::vector<float>* f0 = nullptr) {
    const int nfft = bank.nfft();
    const int bins = bank.bins();
    const size_t nFrames = melFrames.size();
    const size_t n = (nFrames - 1) * (size_t)hop + (size_t)nfft;

    // 1) 逆梅尔得到幅度谱
    std::vector<std::vector<float>> mag(nFrames, std::vector<float>((size_t)bins, 0.0f));
    for (size_t t = 0; t < nFrames; ++t) mag[t] = bank.fromMel(melFrames[t]);

    // 2) 初始相位：有 F0 时按谐波相位构造，否则为零
    std::vector<std::vector<float>> phase(nFrames, std::vector<float>((size_t)bins, 0.0f));
    if (f0 && f0->size() >= nFrames) {
        double acc = 0.0;
        for (size_t t = 0; t < nFrames; ++t) {
            const float hz = (float)(*f0)[t];
            acc += 2 * M_PI * hz * hop / bank.sampleRate();   // 每帧相位推进
            for (int k = 0; k < bins; ++k) {
                const double fk = k * (double)bank.sampleRate() / bank.nfft();
                if (hz > 40.0) {
                    const double h = std::round(fk / hz);        // 最近的谐波序号
                    phase[t][(size_t)k] = (float)(h * acc);
                }
            }
        }
    }
    // sqrt-Hann：分析/合成都用它，重叠相加可完美重建
    std::vector<float> win((size_t)nfft);
    for (int i = 0; i < nfft; ++i)
        win[(size_t)i] = (float)std::sqrt(0.5 - 0.5 * std::cos(2 * M_PI * i / (nfft - 1)));

    std::vector<float> out(n, 0.0f);
    for (int it = 0; it < glIters; ++it) {
        // 合成
        std::fill(out.begin(), out.end(), 0.0f);
        std::vector<float> norm(n, 0.0f);
        for (size_t t = 0; t < nFrames; ++t) {
            std::vector<std::complex<float>> buf((size_t)nfft, 0.0f);
            for (int k = 0; k < bins; ++k) {
                const float m = mag[t][(size_t)k];
                const float ph = phase[t][(size_t)k];
                buf[(size_t)k] = std::polar(m, ph);
                if (k > 0 && k < nfft - k)
                    buf[(size_t)(nfft - k)] = std::conj(buf[(size_t)k]);
            }
            Fft::inverse(buf);
            const size_t off = t * (size_t)hop;
            for (int i = 0; i < nfft; ++i) {
                out[off + (size_t)i] += buf[(size_t)i].real() * win[(size_t)i];
                norm[off + (size_t)i] += win[(size_t)i] * win[(size_t)i];
            }
        }
        for (size_t i = 0; i < n; ++i)
            if (norm[i] > 1e-6f) out[i] /= norm[i];

        // 分析：更新相位
        for (size_t t = 0; t < nFrames; ++t) {
            std::vector<std::complex<float>> buf((size_t)nfft, 0.0f);
            const size_t off = t * (size_t)hop;
            for (int i = 0; i < nfft; ++i) buf[(size_t)i] = out[off + (size_t)i] * win[(size_t)i];
            Fft::forward(buf);
            for (int k = 0; k < bins; ++k) phase[t][(size_t)k] = std::arg(buf[(size_t)k]);
        }
    }
    // 逐帧能量对齐：修掉对数梅尔重建造成的响度损失
    {
        std::vector<float> frameGain(nFrames, 1.0f);
        for (size_t t = 0; t < nFrames; ++t) {
            double target = 0.0, actual = 0.0;
            for (int k = 0; k < bins; ++k) target += mag[t][(size_t)k];
            const size_t off = t * (size_t)hop;
            for (int i = 0; i < nfft && off + (size_t)i < n; ++i) actual += std::fabs(out[off + (size_t)i]);
            if (actual > 1e-6) frameGain[t] = (float)std::min(4.0, std::max(0.25, target / actual));
        }
        // 平滑增益曲线，避免顿挫
        for (int pass = 0; pass < 2; ++pass)
            for (size_t t = 1; t + 1 < nFrames; ++t)
                frameGain[t] = 0.25f * frameGain[t - 1] + 0.5f * frameGain[t] + 0.25f * frameGain[t + 1];
        for (size_t t = 0; t < nFrames; ++t) {
            const size_t off = t * (size_t)hop;
            for (int i = 0; i < nfft && off + (size_t)i < n; ++i) out[off + (size_t)i] *= frameGain[t];
        }
    }

    return out;
}

}  // namespace vx
