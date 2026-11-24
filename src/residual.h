// 残差模型：建模共振峰合成丢掉的那部分
//
// 纯 Klatt 输出只有「规则的周期成分 + 简单噪声」，缺少：
//   1. 声门脉冲的真实不规则性（微抖动、开相噪声）
//   2. 高频细节（唇辐射后的谱包络）
//   3. 气声成分随响度变化
//
// 这里先做可解释的残差层（不用神经网络也能用）：
//   残差 = 白噪声 × 频谱包络(由帧参数决定) × 呼吸度(av/ah)
// 之后可以把这个映射换成训练好的小网络，接口不变。
#pragma once
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace vx {

struct ResidualFrame {
    float av = 0.0f;      // 浊音幅度
    float ah = 0.0f;      // 气声幅度
    float f1 = 500.0f;    // 用第一共振峰估计谱倾
    float f0 = 200.0f;
};

class ResidualModel {
public:
    explicit ResidualModel(double fs) : fs_(fs), rng_(98765u) {}

    // 每帧生成一段残差噪声（与帧长 samples 相同）
    void render(const ResidualFrame& fr, size_t samples, std::vector<float>& out) {
        out.resize(samples);
        // 呼吸度：气声越大，残差越明显
        const float breathiness = std::min(1.0f, fr.ah * 2.0f + 0.05f);
        const float base = fr.av * 0.035f * breathiness;
        // 谱倾：F1 越低越闷
        const float tilt = std::min(0.98f, std::max(0.2f, fr.f1 / 1500.0f));
        double lp = 0.0;
        std::uniform_real_distribution<float> uni(-1.0f, 1.0f);
        for (size_t i = 0; i < samples; ++i) {
            const float w = uni(rng_);
            lp = lp * (0.7 * tilt) + (1.0 - 0.7 * tilt) * w;   // 一阶低通塑形
            out[i] = lp * base;
        }
    }

private:
    double fs_;
    std::mt19937 rng_;
};

}  // namespace vx
