// VoxSynth - 共振峰合成核心
//
// 性能设计：
//   1. 音符游标 O(1)：整段渲染只扫一遍音符，不做逐采样搜索
//   2. 系数分块更新：共振峰系数每 BLOCK(8) 个采样更新一次，
//      三角函数开销降到 1/8，插值仍然连续（块内样本用同一组系数）
//   3. 声门源 4× 过采样 + 降采样低通：消除脉冲周期量化毛刺
//   4. 所有状态值语义，无裸指针、无堆分配进入热路径
//
// 只用标准库。

#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

namespace vx {

enum Cons : int {
    kNone = 0, kS = 1, kSh = 2, kF = 3, kP = 4, kB = 5,
    kM = 6, kL = 7, kSX = 8, kSHX = 9, kJX = 10, kXF = 11
};

struct Note {
    double start = 0.0;     // 秒
    double dur   = 0.4;     // 秒
    double f0    = 200.0;   // Hz
    int    vowel = 0;       // 0..5
    int    cons  = kNone;   // 声母类别
    double consDur = 0.05;  // 声母时长
    int    coda  = 0;       // 0 无 / 1 -n / 2 -ng
    double codaDur = 0.0;
    int    glideTo = -1;    // 复韵母收尾
    double glideDur = 0.0;
};

struct Config {
    std::array<std::array<double, 6>, 6> formant{{
        {{800, 1250, 2550, 3400, 4300, 5200}},
        {{520,  880, 2500, 3350, 4250, 5150}},
        {{520, 1250, 2400, 3300, 4200, 5100}},
        {{300, 2300, 3000, 3400, 4300, 5200}},
        {{320,  780, 2400, 3300, 4200, 5100}},
        {{300, 1900, 2300, 3250, 4150, 5050}},
    }};
    std::array<double, 6> bw{{60, 80, 120, 180, 240, 300}};
    double legato = 0.045;
    double breath = 0.04;
    double oq     = 0.6;      // 开商（张力）
    double jitter = 0.002;
    double shimmer = 0.010;
};

class Synth {
public:
    explicit Synth(double fs = 48000.0) : fs_(fs) {}
    void setConfig(const Config& c) { cfg_ = c; }
    const Config& config() const { return cfg_; }

    // 渲染 [0,total) 秒；返回单声道 float，幅度约 ±0.9
    std::vector<float> render(const std::vector<Note>& score, double total);

    // 分块长度：系数更新粒度
    static constexpr int kBlock = 8;

private:
    struct Res { double y1 = 0, y2 = 0; };
    struct Fric { double x1 = 0, x2 = 0, y1 = 0, y2 = 0; };

    void updateCoeffs(const std::array<double, 6>& f,
                      const std::array<double, 6>& b);
    double source(double f0);
    double track(double x);
    double noise(int kind, double t, double dur);

    double fs_;
    Config cfg_;

    // 声道
    std::array<double, 6> cg_{}, ca1_{}, ca2_{};
    std::array<Res, 6> res_{};
    double rad_ = 0;

    // 声源
    double phase_ = 0, jit_ = 1, shim_ = 1;
    bool periodOpen_ = false;
    double ds1_ = 0, ds2_ = 0, lp1_ = 0, lp2_ = 0;

    // 辅音
    std::array<Fric, 12> fst_{};

    std::mt19937 rng_{20260101u};
    std::uniform_real_distribution<double> uni_{-1.0, 1.0};
    std::normal_distribution<double> nrm_{0.0, 1.0};
};

// ---------------------------------------------------------------------------

inline void Synth::updateCoeffs(const std::array<double, 6>& f,
                                const std::array<double, 6>& b) {
    for (int k = 0; k < 6; ++k) {
        const double bw = std::max(20.0, b[(size_t)k]);
        const double r  = std::exp(-M_PI * bw / fs_);
        const double w  = 2.0 * M_PI * std::clamp(f[(size_t)k], 40.0, fs_ * 0.45) / fs_;
        const double a1 = 2.0 * r * std::cos(w);
        const double a2 = -r * r;
        ca1_[(size_t)k] = a1;
        ca2_[(size_t)k] = a2;
        cg_[(size_t)k]  = 1.0 - a1 - a2;   // 直流增益归一
    }
}

inline double Synth::source(double f0) {
    if (!periodOpen_) {
        jit_  = 1.0 + cfg_.jitter * nrm_(rng_);
        shim_ = 1.0 + cfg_.shimmer * nrm_(rng_);
        periodOpen_ = true;
    }
    const double fs4 = fs_ * 4.0;
    const double T0  = fs4 / std::max(20.0, f0) * jit_;
    const double to  = std::clamp(cfg_.oq, 0.2, 0.95);
    const double tp  = to * 0.5, tn = to * 0.5;

    // 降采样低通（f=0.09fs4, bw=0.06fs4，常数预先算好）
    static const double kR = std::exp(-M_PI * 0.06);
    static const double kW = 2.0 * M_PI * 0.09;
    static const double kA1 = 2.0 * kR * std::cos(kW);
    static const double kA2 = -kR * kR;
    static const double kG  = 1.0 - kA1 - kA2;

    double acc = 0.0;
    for (int i = 0; i < 4; ++i) {
        double v = 0.0;
        if (phase_ < to) {
            if (phase_ < tp) v = 0.5 * (1.0 - std::cos(M_PI * phase_ / tp));
            else             v = std::cos(M_PI * 0.5 * (phase_ - tp) / tn);
        }
        phase_ += 1.0 / T0;
        if (phase_ >= 1.0) { phase_ -= 1.0; periodOpen_ = false; }

        const double y = kG * v + kA1 * ds1_ + kA2 * ds2_;
        ds2_ = ds1_; ds1_ = y;
        acc += y;
    }
    double s = acc * 0.25 * shim_;
    if (cfg_.breath > 1e-4) s += cfg_.breath * 0.05 * uni_(rng_);

    // 声源平滑（4kHz 一阶低通，磨掉闭相棱角）
    const double r2 = std::exp(-M_PI * 1200.0 / fs_);
    const double w2 = 2.0 * M_PI * 4000.0 / fs_;
    const double a1 = 2.0 * r2 * std::cos(w2), a2 = -r2 * r2;
    const double y = (1.0 - a1 - a2) * s + a1 * lp1_ + a2 * lp2_;
    lp2_ = lp1_; lp1_ = y;
    return y;
}

inline double Synth::track(double x) {
    for (int k = 0; k < 6; ++k) {
        Res& s = res_[(size_t)k];
        const double y = cg_[(size_t)k] * x + ca1_[(size_t)k] * s.y1 + ca2_[(size_t)k] * s.y2;
        s.y2 = s.y1; s.y1 = y;
        x = y;
    }
    const double out = x - rad_;   // 唇辐射
    rad_ = x;
    return out;
}

inline double Synth::noise(int kind, double t, double dur) {
    if (kind <= 0 || kind > 11 || dur <= 1e-6 || t < 0.0 || t >= dur) return 0.0;

    struct P { double fc, bw, gain, atk; };
    static const P kP[12] = {
        {0, 0, 0, 0},
        {6500, 3000, 0.45, 0.006}, {3000, 1600, 0.40, 0.008},
        {1500, 1200, 0.32, 0.008}, {2500, 2600, 0.45, 0.002},
        {1500, 2200, 0.38, 0.002}, {400,    500, 0.16, 0.020},
        {700,   600, 0.10, 0.020}, {6000, 3000, 0.42, 0.003},
        {3200, 1800, 0.38, 0.004}, {2500, 1600, 0.34, 0.004},
        {4500, 2400, 0.32, 0.006},
    };
    const P& p = kP[kind];

    double env = 1.0;
    if (t < p.atk) env = t / std::max(1e-6, p.atk);
    else {
        const double tail = std::max(p.atk, dur - 0.030);
        if (t > tail) env = std::max(0.0, (dur - t) / 0.030);
    }
    if (env <= 0.0) return 0.0;

    const double fc = std::clamp(p.fc, 120.0, fs_ * 0.45);
    const double q  = std::max(0.5, p.fc / std::max(120.0, p.bw));
    const double w0 = 2.0 * M_PI * fc / fs_;
    const double alpha = std::sin(w0) / (2.0 * q);
    const double b0 = alpha, b2 = -alpha;
    const double a0 = 1.0 + alpha, a1 = -2.0 * std::cos(w0), a2 = 1.0 - alpha;

    Fric& s = fst_[(size_t)kind];
    const double nz = uni_(rng_);
    const double y = (b0 / a0) * nz + (b2 / a0) * s.x2 - (a1 / a0) * s.y1 - (a2 / a0) * s.y2;
    s.x2 = s.x1; s.x1 = nz;
    s.y2 = s.y1; s.y1 = y;
    return env * p.gain * y;
}

inline std::vector<float> Synth::render(const std::vector<Note>& score, double total) {
    const size_t N = (size_t)std::max(0.0, total * fs_);
    std::vector<float> out(N, 0.0f);
    if (score.empty() || N == 0) return out;

    // 状态复位
    for (auto& r : res_) r = Res{};
    for (auto& s : fst_) s = Fric{};
    rad_ = ds1_ = ds2_ = lp1_ = lp2_ = 0.0;
    phase_ = 0.0; periodOpen_ = false;

    // 当前共振峰（从第一个音开始）
    const int v0 = std::clamp(score[0].vowel, 0, 5);
    std::array<double, 6> f = cfg_.formant[(size_t)v0];
    std::array<double, 6> b = cfg_.bw;
    updateCoeffs(f, b);

    size_t noteIdx = 0;
    double amp = 0.0;
    const size_t block = (size_t)kBlock;

    for (size_t i = 0; i < N; i += block) {
        const size_t end = std::min(N, i + block);
        const double t = (double)i / fs_;

        // 音符游标（O(1) 均摊）
        while (noteIdx + 1 < score.size() && t >= score[noteIdx + 1].start) ++noteIdx;
        const Note& n = score[noteIdx];
        const double tt = t - n.start;

        // 块内参数（块首计算一次）
        std::array<double, 6> ft = cfg_.formant[(size_t)std::clamp(n.vowel, 0, 5)];
        double codaMix = 0.0;
        if (n.coda > 0 && n.codaDur > 1e-6 && tt > n.dur - n.codaDur) {
            codaMix = std::clamp((tt - (n.dur - n.codaDur)) / (n.codaDur * 0.6), 0.0, 1.0);
            static const double kN[6]  = {250, 1600, 2500, 3400, 4400, 5600};
            static const double kNG[6] = {250, 2000, 2600, 3400, 4400, 5600};
            const double* nf = (n.coda == 1) ? kN : kNG;
            for (int k = 0; k < 6; ++k)
                ft[(size_t)k] = ft[(size_t)k] * (1.0 - codaMix) + nf[k] * codaMix;
        }
        if (n.glideTo >= 0 && n.glideDur > 1e-6 && tt > n.dur - n.glideDur) {
            const double g = std::clamp((tt - (n.dur - n.glideDur)) / n.glideDur, 0.0, 1.0);
            const double w = g * g * (3.0 - 2.0 * g);
            const int gi = std::clamp(n.glideTo, 0, 5);
            for (int k = 0; k < 6; ++k)
                ft[(size_t)k] = ft[(size_t)k] * (1.0 - w) + cfg_.formant[(size_t)gi][(size_t)k] * w;
        }
        for (int k = 0; k < 6; ++k) {
            f[(size_t)k] += (ft[(size_t)k] - f[(size_t)k]) * 0.002;
            b[(size_t)k] += (cfg_.bw[(size_t)k] - b[(size_t)k]) * 0.002;
        }
        updateCoeffs(f, b);

        // 基频（滑音）
        double f0 = n.f0;
        if (cfg_.legato > 1e-6 && noteIdx > 0 && tt < cfg_.legato) {
            const double x = tt / cfg_.legato;
            const double s = x * x * (3.0 - 2.0 * x);
            f0 = score[noteIdx - 1].f0 + (n.f0 - score[noteIdx - 1].f0) * s;
        }

        // 幅度包络
        const double atk = std::min(0.008, n.dur * 0.2);
        const double rel = std::min(0.025, n.dur * 0.4);
        double env = std::min(1.0, tt / std::max(1e-6, atk));
        if (tt > n.dur - rel) env *= std::max(0.0, (n.dur - tt) / std::max(1e-6, rel));

        double vg = 1.0;
        if (n.cons > 0 && tt < n.consDur && n.consDur > 1e-6) {
            const bool voiceless = (n.cons == kS || n.cons == kSh || n.cons == kF || n.cons == kP ||
                                    n.cons == kSX || n.cons == kSHX || n.cons == kJX || n.cons == kXF);
            if (voiceless) vg = 0.10 + 0.90 * std::min(1.0, (tt / n.consDur) / 0.85);
        }
        const double ampTgt = 0.85 * env * vg * (1.0 - 0.25 * codaMix);
        amp += (ampTgt - amp) * 0.02;

        for (size_t j = i; j < end; ++j) {
            const double tj = (double)j / fs_ - n.start;
            out[j] = (float)(track(source(f0) * amp) + noise(n.cons, tj, n.consDur));
        }
    }

    // 幅值定标：按 99.9 分位，避免离群样本决定整体音量
    std::vector<float> mag(N);
    for (size_t i = 0; i < N; ++i) mag[i] = std::fabs(out[i]);
    const size_t k = (size_t)(N * 0.999);
    std::nth_element(mag.begin(), mag.begin() + (long)k, mag.end());
    const float g = (float)(0.85 / std::max(1e-6, (double)mag[k]));
    for (float& v : out) v = std::clamp(v * g, -0.98f, 0.98f);
    return out;
}

}  // namespace vx
