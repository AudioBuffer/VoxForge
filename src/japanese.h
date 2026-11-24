// 日语假名 -> 音素（只用引擎已有的符号）
//
// 假名表直接映射到 IPA 串，覆盖五十音 + 浊音 + 半浊音 + 拗音常用组合。
// 特殊处理：
//   し ʃi、ち tʃi、つ tsu、ふ fu、を o、ん n
//   っ（促音）-> 停顿
//   ー（长音）-> 延长前一元音
#pragma once
#include <map>
#include <string>
#include <vector>

namespace vx {

// 单假名 -> IPA
inline const std::map<std::string, std::string>& kanaTable() {
    static const std::map<std::string, std::string> m = {
        // あ行
        {"あ", "ɑ"}, {"い", "i"}, {"う", "\u026F"}, {"え", "e"}, {"お", "o"},
        // か行
        {"か", "kɑ"}, {"き", "ki"}, {"く", "k\u026F"}, {"け", "ke"}, {"こ", "ko"},
        {"が", "gɑ"}, {"ぎ", "gi"}, {"ぐ", "gu"}, {"げ", "ge"}, {"ご", "go"},
        // さ行
        {"さ", "sɑ"}, {"し", "\u0283i"}, {"す", "s\u026F"}, {"せ", "se"}, {"そ", "so"},
        {"ざ", "zɑ"}, {"じ", "d\u0291i"}, {"ず", "zu"}, {"ぜ", "ze"}, {"ぞ", "zo"},
        // た行
        {"た", "tɑ"}, {"ち", "t\u0283i"}, {"つ", "ts\u026F"}, {"て", "te"}, {"と", "to"},
        {"だ", "dɑ"}, {"ぢ", "d\u0291i"}, {"づ", "zu"}, {"で", "de"}, {"ど", "do"},
        // な行
        {"な", "nɑ"}, {"に", "ni"}, {"ぬ", "n\u026F"}, {"ね", "ne"}, {"の", "no"},
        // は行
        {"は", "hɑ"}, {"ひ", "\u00E7i"}, {"ふ", "\u0278\u026F"}, {"へ", "he"}, {"ほ", "ho"},
        {"ば", "bɑ"}, {"び", "bi"}, {"ぶ", "bu"}, {"べ", "be"}, {"ぼ", "bo"},
        {"ぱ", "pɑ"}, {"ぴ", "pi"}, {"ぷ", "pu"}, {"ぺ", "pe"}, {"ぽ", "po"},
        // ま行
        {"ま", "mɑ"}, {"み", "mi"}, {"む", "m\u026F"}, {"め", "me"}, {"も", "mo"},
        // や行
        {"や", "jɑ"}, {"ゆ", "j\u026F"}, {"よ", "jo"},
        // ら行
        {"ら", "\u0279ɑ"}, {"り", "\u0279i"}, {"る", "\u0279\u026F"}, {"れ", "\u0279e"}, {"ろ", "\u0279o"},
        // わ行
        {"わ", "wɑ"}, {"を", "o"}, {"ん", "\u0274"},
        // 拗音
        {"きゃ", "kjɑ"}, {"きゅ", "kju"}, {"きょ", "kjo"},
        {"しゃ", "\u0283ɑ"}, {"しゅ", "\u0283u"}, {"しょ", "\u0283o"},
        {"ちゃ", "t\u0283ɑ"}, {"ちゅ", "t\u0283u"}, {"ちょ", "t\u0283o"},
        {"にゃ", "njɑ"}, {"にゅ", "nju"}, {"にょ", "njo"},
        {"ひゃ", "hjɑ"}, {"ひゅ", "hju"}, {"ひょ", "hjo"},
        {"みゃ", "mjɑ"}, {"みゅ", "mju"}, {"みょ", "mjo"},
        {"りゃ", "\u0279jɑ"}, {"りゅ", "\u0279ju"}, {"りょ", "\u0279jo"},
        {"ぎゃ", "gjɑ"}, {"ぎゅ", "gju"}, {"ぎょ", "gjo"},
        {"じゃ", "\u0292ɑ"}, {"じゅ", "\u0292u"}, {"じょ", "\u0292o"},
        {"びゃ", "bjɑ"}, {"びゅ", "bju"}, {"びょ", "bjo"},
        {"ぴゃ", "pjɑ"}, {"ぴゅ", "pju"}, {"ぴょ", "pjo"},
    };
    return m;
}

// 假名串 -> IPA 串（贪心最长匹配，处理促音与长音）
inline std::string kanaToIpa(const std::string& text) {
    // 先按 UTF-8 拆成字符
    std::vector<std::string> chars;
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = (unsigned char)text[i];
        size_t len = 1;
        if (c >= 0xF0) len = 4;
        else if (c >= 0xE0) len = 3;
        else if (c >= 0xC0) len = 2;
        if (i + len > text.size()) len = 1;
        chars.push_back(text.substr(i, len));
        i += len;
    }

    const auto& tbl = kanaTable();
    std::string out;
    for (size_t i = 0; i < chars.size(); ++i) {
        // 拗音：两字组合优先
        if (i + 1 < chars.size()) {
            const std::string two = chars[i] + chars[i + 1];
            if (auto it = tbl.find(two); it != tbl.end()) {
                out += it->second;
                ++i;
                continue;
            }
        }
        const std::string& ch = chars[i];
        if (ch == "っ") { out += " "; continue; }      // 促音：短停顿
        if (ch == "ー") { continue; }                   // 长音：简化处理
        if (auto it = tbl.find(ch); it != tbl.end()) { out += it->second; continue; }
        if (ch == " " || ch == "\n") { out += " "; continue; }
        // 未知字符（汉字）跳过
    }
    return out;
}

}  // namespace vx
