// 多声线预设
//
// 声线不是「换个人重新录」，而是同一套音素表的参数缩放：
//   f0_scale       整体音高倍数
//   formant_scale  共振峰整体缩放（声道长度差异，别超过 ±15%）
//   breath         气声量
//   tilt_db        声源频谱倾斜（越大越柔）
//
// 注意：f0_start 是绝对起始频率，不再跟着 f0_scale 一起缩，
// 否则男声会掉到 60Hz 变成恐龙叫。
#pragma once
#include "tts/config.h"
#include <string>

namespace vx {

struct VoicePreset {
    std::string name;
    float f0Scale = 1.0f;
    float formantScale = 1.0f;
    float breath = 0.0f;
    float tiltDb = 6.0f;
    float f0Start = 118.0f;
};

inline const VoicePreset& voiceByName(const std::string& n) {
    static const VoicePreset kMale{"male", 0.95f, 0.96f, 0.02f, 4.0f, 118.0f};
    static const VoicePreset kFemale{"female", 1.15f, 1.10f, 0.03f, 8.0f, 180.0f};
    static const VoicePreset kChild{"child", 1.35f, 1.18f, 0.03f, 7.0f, 210.0f};
    static const VoicePreset kNeutral{"neutral", 1.0f, 1.0f, 0.02f, 6.0f, 118.0f};
    if (n == "male") return kMale;
    if (n == "female") return kFemale;
    if (n == "child") return kChild;
    return kNeutral;
}

// 把预设写进引擎的 VoiceParams
inline void applyVoice(const VoicePreset& v, tts::VoiceParams& p) {
    p.f0_scale = v.f0Scale;
    p.formant_scale = v.formantScale;
    p.breath = v.breath;
    p.source_tilt_db = v.tiltDb;
    p.f0_start = v.f0Start;
    p.f0_end = v.f0Start * 0.97f;      // 句尾轻微下降，别太狠
    p.final_fall_hz = 6.0f;
}

}  // namespace vx
