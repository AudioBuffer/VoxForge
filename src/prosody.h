// 韵律层：按声调调整音素时长，并驱动交叉重叠
//
// 问题：引擎给每个音素的时长是固定的，念出来像机器人打拍子。
// 做法：
//   1. 元音按声调拉长/缩短（上声最长、轻声最短）
//   2. 时长用「重复元音符号」表达（引擎按 token 数计时长）
//   3. 共振峰平滑加大，让相邻音素交叉重叠（协同发音）
#pragma once
#include <string>
#include "pinyin_ipa.h"

namespace vx {

// 声调 -> 元音重复次数（1=基准，2=加长）
inline int toneStretch(int tone) {
    switch (tone) {
        case 1: return 1;   // 阴平，中等
        case 2: return 1;   // 阳平
        case 3: return 2;   // 上声，最长
        case 4: return 1;   // 去声，短促
        default: return 1;  // 轻声
    }
}

// 把 IPA 串最后一个元音重复 n 次，实现拉长
inline std::string stretchVowel(const std::string& ipa, int n) {
    if (n <= 1 || ipa.empty()) return ipa;
    // 找出末尾的元音字符（UTF-8 三字节为主的 IPA 元音）
    static const char* kVowels[] = {
        "i", "\u026A", "e", "\u025B", "\u00E6", "\u0251", "\u0254", "o",
        "\u028A", "u", "\u028C", "\u025D", "\u0259", "\u0268", "y",
    };
    size_t best = std::string::npos;
    size_t bestLen = 0;
    for (const char* v : kVowels) {
        const std::string s(v);
        const size_t p = ipa.rfind(s);
        if (p != std::string::npos && (best == std::string::npos || p > best)) {
            best = p;
            bestLen = s.size();
        }
    }
    if (best == std::string::npos) return ipa;
    const std::string v = ipa.substr(best, bestLen);
    std::string rep;
    for (int i = 0; i < n; ++i) rep += v;
    return ipa.substr(0, best) + rep + ipa.substr(best + bestLen);
}

// 单音节按声调展开
inline std::string pinyinToIpaToned(const std::string& py, int tone) {
    return stretchVowel(pinyinToIpa(py), toneStretch(tone));
}

}  // namespace vx
