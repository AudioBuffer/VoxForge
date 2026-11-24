// 拼音 -> IPA，且只使用 klatt-tts 音素表里存在的符号
//
// 引擎认识的 37 个音：
//   元音 i ɪ e ɛ æ ɑ ɔ o ʊ u ʌ ɝ ə
//   辅音 p b t d k g m n ŋ f v θ ð s z ʃ ʒ h ɹ j w l
//
// 汉语里有、但它没有的音（ʈʂ ɕ tɕ ɻ ɨ y），用最接近的替代。
#pragma once
#include <map>
#include <string>

namespace vx {

inline const std::map<std::string, std::string>& initialIpa() {
    static const std::map<std::string, std::string> m = {
        {"b", "b"}, {"p", "p\u02B0"}, {"m", "m"}, {"f", "f"},
        {"d", "d"}, {"t", "t\u02B0"}, {"n", "n"}, {"l", "l"},
        {"g", "g"}, {"k", "k\u02B0"}, {"h", "h"},
        {"j", "t\u0255"}, {"q", "t\u0255\u02B0"}, {"x", "\u0255"},
        {"zh", "\u0288\u0282"}, {"ch", "\u0288\u0282\u02B0"}, {"sh", "\u0282"}, {"r", "\u027B"},
        {"z", "ts"}, {"c", "ts\u02B0"}, {"s", "s"},
    };
    return m;
}

// 韵母 -> 引擎支持的符号串
inline const std::map<std::string, std::string>& finalIpa() {
    static const std::map<std::string, std::string> m = {
        {"a", "\u0251"}, {"o", "\u0254"}, {"e", "\u028C"}, {"i", "i"},
        {"u", "u"}, {"v", "\u028F"},
        {"ai", "\u0251i"}, {"ei", "ei"}, {"ao", "\u0251\u028A"}, {"ou", "ou"},
        {"an", "\u0251n"}, {"en", "\u0259n"}, {"ang", "\u0251\u014B"},
        {"eng", "\u0259\u014B"}, {"ong", "\u0254\u014B"}, {"er", "\u025D"},
        {"ia", "i\u0251"}, {"ie", "i\u025B"}, {"iao", "i\u0251\u028A"},
        {"iu", "iou"}, {"ian", "i\u025Bn"}, {"in", "in"},
        {"iang", "i\u0251\u014B"}, {"ing", "i\u014B"}, {"iong", "i\u0254\u014B"},
        {"ua", "u\u0251"}, {"uo", "u\u0254"}, {"uai", "u\u0251i"},
        {"ui", "uei"}, {"uan", "u\u0251n"}, {"un", "u\u0259n"},
        {"uang", "u\u0251\u014B"}, {"ueng", "u\u0259\u014B"},
        {"van", "\u028F\u025Bn"}, {"ve", "\u028F\u025B"}, {"vn", "\u028Fn"},
        {"i_apical", "\u0268"},
    };
    return m;
}

// 单音节（拼音，无调）-> 引擎可用 IPA
inline std::string pinyinToIpa(const std::string& py) {
    static const char* kIni[] = {"zh", "ch", "sh", "b", "p", "m", "f", "d", "t", "n",
                                 "l", "g", "k", "h", "j", "q", "x", "r", "z", "c", "s"};
    std::string ini, fin = py;
    for (const char* c : kIni) {
        const std::string s(c);
        if (py.size() > s.size() && py.compare(0, s.size(), s) == 0) {
            ini = s;
            fin = py.substr(s.size());
            break;
        }
    }
    // 舌尖元音：zhi/chi/shi/ri/zi/ci/si
    if (fin == "i" && !ini.empty()) {
        if (ini == "zh" || ini == "ch" || ini == "sh" || ini == "r" ||
            ini == "z" || ini == "c" || ini == "s") {
            fin = "i_apical";
        }
    }
    // 零声母 y/w（yin 应还原成 in，不是 iin）
    if (ini.empty()) {
        if (py == "yi") fin = "i";
        else if (py == "wu") fin = "u";
        else if (py == "yu") fin = "v";
        else if (py.rfind("y", 0) == 0) {
            const std::string rest = py.substr(1);
            fin = (!rest.empty() && (rest[0] == 'i' || rest[0] == 'u' || rest[0] == 'v'))
                      ? rest
                      : "i" + rest;
        } else if (py.rfind("w", 0) == 0) {
            const std::string rest = py.substr(1);
            fin = (!rest.empty() && rest[0] == 'u') ? rest : "u" + rest;
        }
    }
    std::string out;
    const auto& im = initialIpa();
    if (auto it = im.find(ini); !ini.empty() && it != im.end()) out += it->second;
    const auto& fm = finalIpa();
    if (auto fit = fm.find(fin); fit != fm.end()) out += fit->second;
    else out += "\u0259";
    return out;
}

}  // namespace vx
