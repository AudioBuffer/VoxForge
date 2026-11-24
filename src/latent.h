// VoxSynth - 潜空间扩散管线（接口层）
//
//   Mel ──► Encoder ──► z ──► Diffusion ──► z' ──► Decoder ──► Mel' ──► Vocoder
//
// 现在没有训练好的模型，所以每一段都有「空操作」回退：
//   Encoder: 4 帧平均下采样          Decoder: 4 倍线性上采样
//   Diffusion: 直接透传
// 模型到位后逐个替换实现，调用方代码不变。
#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace vx {

struct LatentShape {
    int channels = 256;     // 潜变量通道数（mel 512 → 压到 256，谱分辨 1024/2048）
    int downsample = 4;     // 时间下采样倍数
};

// Mel: [frames][bins]；z: [frames/ds][channels]
class LatentPipeline {
public:
    void setShape(const LatentShape& s) { shape_ = s; }
    const LatentShape& shape() const { return shape_; }

    // ---- 编码：Mel -> z（当前为线性投影 + 帧平均）----
    std::vector<std::vector<float>> encode(const std::vector<std::vector<float>>& mel) const {
        const int T = (int)mel.size();
        const int bins = T ? (int)mel[0].size() : 0;
        const int ds = std::max(1, shape_.downsample);
        const int zT = (T + ds - 1) / ds;
        std::vector<std::vector<float>> z((size_t)zT, std::vector<float>((size_t)shape_.channels, 0.0f));
        const int C = shape_.channels;
        for (int t = 0; t < zT; ++t) {
            for (int c = 0; c < C; ++c) {
                // 通道位置映射到 bin 位置（线性插值），channels 与 bins 不必相等
                const double pos = (C > 1 && bins > 1) ? (double)c * (bins - 1) / (C - 1) : 0.0;
                const int b0 = (int)pos;
                const int b1 = std::min(bins - 1, b0 + 1);
                const double fw = pos - b0;
                float acc = 0.0f;
                int cnt = 0;
                for (int k = 0; k < ds; ++k) {
                    const int src = t * ds + k;
                    if (src >= T) break;
                    const float v0 = mel[(size_t)src][(size_t)b0];
                    const float v1 = mel[(size_t)src][(size_t)b1];
                    acc += (float)(v0 * (1.0 - fw) + v1 * fw);
                    ++cnt;
                }
                z[(size_t)t][(size_t)c] = cnt ? acc / (float)cnt : 0.0f;
            }
        }
        return z;
    }

    // ---- 扩散：没有模型时透传（后续接 U-Net/Diffusion 采样循环）----
    void diffuse(std::vector<std::vector<float>>& z, int steps = 0) const {
        (void)z;
        (void)steps;   // 0 = 不做事；模型到位后在这里跑去噪循环
    }

    // ---- 解码：z -> Mel（4 倍线性上采样，通道映射回 bins）----
    std::vector<std::vector<float>> decode(const std::vector<std::vector<float>>& z,
                                           int targetFrames, int bins) const {
        const int ds = std::max(1, shape_.downsample);
        std::vector<std::vector<float>> mel((size_t)targetFrames, std::vector<float>((size_t)bins, 0.0f));
        if (z.empty()) return mel;
        const int C = (int)z[0].size();
        for (int t = 0; t < targetFrames; ++t) {
            const int zt = std::min((int)z.size() - 1, t / ds);
            for (int bi = 0; bi < bins; ++bi) {
                const double pos = (bins > 1 && C > 1) ? (double)bi * (C - 1) / (bins - 1) : 0.0;
                const int c0 = (int)pos;
                const int c1 = std::min(C - 1, c0 + 1);
                const double fw = pos - c0;
                mel[(size_t)t][(size_t)bi] =
                    (float)(z[(size_t)zt][(size_t)c0] * (1.0 - fw) + z[(size_t)zt][(size_t)c1] * fw);
            }
        }
        return mel;
    }

private:
    LatentShape shape_{};
};

}  // namespace vx
