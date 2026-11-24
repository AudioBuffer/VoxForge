// 音高曲线：OpenUtau 风格的转音与颤音
//
// 每个音符可以带：
//   - portamento：与前一个音之间的滑音时长（ms，0 表示硬切）
//   - vibrato：深度（音分）、频率（Hz）、渐入时长（ms）
//   - bend：音符内的音高偏移（音分），首尾线性插值
//
// 输出逐帧 f0（Hz）。
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>
#include "easing.h"

namespace vx {

struct PitchNote {
    double startSec = 0.0;
    double durSec = 0.5;
    double hz = 220.0;

    double portamentoMs = 60.0;   // 从前一个音滑过来
    Bezier portamentoCurve = Bezier::sharp();   // 转音曲线
    // 类人度 0~1：0 = 完全直线滑，1 = 带曲线 + 过冲 + 自然颤音
    double humanity = 1.0;
    double overshootHz = 18.0;    // 过冲峰值（humanity=1 时）
    double vibratoCents = 25.0;   // 颤音深度（约 1/4 半音）
    double vibratoHz = 5.5;
    double vibratoFadeMs = 200.0;
    double bendStartCents = 0.0;
    double bendEndCents = 0.0;
};

// S 型（smoothstep）插值
inline double smoothstep(double u) {
    u = std::clamp(u, 0.0, 1.0);
    return u * u * (3.0 - 2.0 * u);
}

// 生成整首的逐帧 f0
inline std::vector<float> renderPitchCurve(const std::vector<PitchNote>& notes,
                                           double totalSec, double frameMs = 5.0,
                                           double vibratoPhase0 = 0.0) {
    const size_t n = (size_t)std::max(1.0, totalSec * 1000.0 / frameMs);
    std::vector<float> f0(n, 0.0f);

    double phase = vibratoPhase0;   // 颤音相位跨音符连续
    double lastTime = 0.0;

    for (size_t i = 0; i < n; ++i) {
        const double t = i * frameMs / 1000.0;

        // 找当前音符
        const PitchNote* cur = nullptr;
        const PitchNote* prev = nullptr;
        for (size_t k = 0; k < notes.size(); ++k) {
            if (t >= notes[k].startSec && t < notes[k].startSec + notes[k].durSec) {
                cur = &notes[k];
                if (k > 0) prev = &notes[k - 1];
                break;
            }
        }
        if (!cur) continue;

        const double rel = t - cur->startSec;
        const double relMs = rel * 1000.0;

        // 1) portamento：类人度决定曲线形状与过冲
        double hz = cur->hz;
        if (prev && cur->portamentoMs > 1.0 && relMs < cur->portamentoMs) {
            const double u = relMs / cur->portamentoMs;
            const double h = std::clamp(cur->humanity, 0.0, 1.0);
            // 曲线：线性 <-> 贝塞尔
            const double e = (1.0 - h) * u + h * cur->portamentoCurve.eval(u);
            hz = prev->hz + (cur->hz - prev->hz) * e;
            // 过冲：在 u≈0.8 附近鼓一下，再回落到目标
            const double bump = std::exp(-std::pow((u - 0.80) / 0.20, 2.0));
            const double dir = (cur->hz >= prev->hz) ? 1.0 : -1.0;
            hz += dir * cur->overshootHz * h * bump;
        }

        // 2) 音符内 bend：首尾线性
        const double bend = cur->bendStartCents +
                            (cur->bendEndCents - cur->bendStartCents) *
                                (cur->durSec > 0 ? rel / cur->durSec : 0.0);
        hz *= std::pow(2.0, bend / 1200.0);

        // 3) 颤音：渐入 + 相位连续
        double depth = cur->vibratoCents * std::clamp(cur->humanity, 0.0, 1.0);
        if (cur->vibratoFadeMs > 1.0 && relMs < cur->vibratoFadeMs)
            depth *= smoothstep(relMs / cur->vibratoFadeMs);
        if (depth > 0.0) {
            const double dt = frameMs / 1000.0;
            phase += 2 * M_PI * cur->vibratoHz * dt;
            hz *= std::pow(2.0, (depth * std::sin(phase)) / 1200.0);
        }

        f0[i] = (float)hz;
        lastTime = t;
    }
    (void)lastTime;
    return f0;
}

// OpenUtau「未画音高线」时的默认行为：
//   1. 音符之间自动滑音，长度随音程增大（30–90ms）
//   2. 曲线用 smoothstep，不过冲
//   3. 不加颤音
//   4. 句尾轻微下滑（约 2%）
inline std::vector<float> autoPitchCurve(const std::vector<PitchNote>& notes,
                                         double totalSec, double frameMs = 5.0) {
    std::vector<PitchNote> tuned = notes;
    for (size_t i = 0; i < tuned.size(); ++i) {
        auto& n = tuned[i];
        n.vibratoCents = 0.0;
        n.humanity = 0.0;          // 走直线插值，无过冲
        n.bendStartCents = 0.0;
        n.bendEndCents = 0.0;
        if (i == 0) {
            n.portamentoMs = 0.0;
        } else {
            const double a = tuned[i - 1].hz, b = n.hz;
            const double ratio = (a > 1.0 && b > 1.0) ? std::fabs(std::log2(b / a)) : 0.0;
            n.portamentoMs = std::clamp(30.0 + 60.0 * ratio, 30.0, 90.0);
        }
    }
    // 句尾下滑：最后一个音加 -2% 的 bend
    if (!tuned.empty()) {
        tuned.back().bendStartCents = 0.0;
        tuned.back().bendEndCents = -35.0;   // 约 -2%
    }
    return renderPitchCurve(tuned, totalSec, frameMs);
}

}  // namespace vx
