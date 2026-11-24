// VoxSynth G2P —— TTS 神经网络版（接口）
//
// 与 SVS 的区别：TTS 需要「自然连续」的音素串，声母韵母不强制用 / 切开，
// 而是由模型输出带上下文的音素序列（含连读、变调）。
//
// 现在没有训练好的模型，走字符串回退；模型到位后用 ONNX 替换 forward。
#pragma once
#include <string>
#include <vector>
#include "g2p_svs.h"

namespace vx {

class G2PTts {
public:
    // 模型路径为空时使用规则回退
    bool load(const std::string& onnxPath) {
        modelPath_ = onnxPath;
        hasModel_ = false;         // 模型加载实现待接入 ORT
        return hasModel_;
    }
    bool hasModel() const { return hasModel_; }

    // 文本 -> 音素序列
    //   有模型：模型输出（带上下文）
    //   无模型：退回 SVS 切分，但保留 "/" 便于调试
    std::vector<std::string> toPhonemes(const std::string& text) const {
        if (!hasModel_) return g2pSvs(text);
        return g2pSvs(text);       // TODO: 模型推理
    }

private:
    std::string modelPath_;
    bool hasModel_{false};
};

}  // namespace vx
