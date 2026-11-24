// WSOLA 变速（保持音高）
//
// 原理：按分析步长 hop_a 取帧，按合成步长 hop_s 叠加，
// 每次叠加前在搜索窗内找波形最相似的位置对齐，避免相位抵消。
//
// 用法：先慢速合成（发音完整），再压到目标速度。
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace vx {

// rate < 1 表示加快（时长乘以 rate），rate > 1 表示放慢
inline std::vector<float> timeStretch(const std::vector<float>& in, double rate,
                                      int sampleRate = 44100) {
    if (in.empty() || rate <= 0.0) return in;
    const size_t n = in.size();
    const size_t outLen = (size_t)(n * rate);
    if (outLen < 64) return in;

    const size_t frame = (size_t)(0.040 * sampleRate);   // 40ms 帧
    const size_t hopS = frame / 2;                       // 合成步长
    const size_t hopA = (size_t)(hopS / rate);           // 分析步长
    const size_t search = (size_t)(0.010 * sampleRate);  // 相似性搜索窗 ±10ms

    std::vector<float> win(frame);
    for (size_t i = 0; i < frame; ++i)
        win[i] = (float)(0.5 - 0.5 * std::cos(2 * M_PI * i / (frame - 1)));

    std::vector<float> out(outLen + frame, 0.0f);
    std::vector<float> norm(outLen + frame, 0.0f);

    size_t aPos = 0;
    size_t oPos = 0;
    size_t prevA = 0;

    while (oPos + frame < outLen + frame && aPos + frame < n) {
        // 在搜索窗内找与上一帧最相似的位置
        size_t best = aPos;
        if (aPos >= search) {
            double bestCorr = -1e30;
            for (size_t off = 0; off <= search * 2; off += 4) {
                const size_t cand = aPos - search + off;
                if (cand + frame >= n) break;
                double corr = 0.0;
                for (size_t i = 0; i < frame; i += 8)
                    corr += (double)in[prevA + i] * in[cand + i];
                if (corr > bestCorr) { bestCorr = corr; best = cand; }
            }
        }
        for (size_t i = 0; i < frame; ++i) {
            out[oPos + i] += in[best + i] * win[i];
            norm[oPos + i] += win[i];
        }
        prevA = best;
        aPos += hopA;
        oPos += hopS;
    }

    out.resize(outLen);
    for (size_t i = 0; i < outLen; ++i)
        if (norm[i] > 1e-6f) out[i] /= norm[i];
    return out;
}

}  // namespace vx
