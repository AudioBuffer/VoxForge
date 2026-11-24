// DSL 特殊参数的正则解析
//
// 输入形如：
//   <"TS,1">
//   <"FP,'F1,50''F2,25''F3,89'">
//   <"GI,'440''880'">
//   <"VI,'180''220''6'">
//   <"BZCG'0,200''1,300'">
//
// 用 std::regex 抓取，不依赖 JSON，也不做手工括号匹配。
#pragma once
#include <regex>
#include <string>

namespace vx {
namespace detail {

// 抓第一个数字（含负号与小数）
inline double num(const std::string& s) {
    static const std::regex kNum(R"([-+]?\d*\.?\d+)");
    std::smatch m;
    if (std::regex_search(s, m, kNum)) {
        try { return std::stod(m.str()); } catch (...) { return 0.0; }
    }
    return 0.0;
}

// 解析特殊参数块
template <typename SpecialT>
inline SpecialT parseSpecialRegex(const std::string& raw) {
    SpecialT sp;
    if (raw.empty() || raw.find("NaN") != std::string::npos) return sp;

    static const std::regex kKey(R"((TS|FP|FI|GI|VI|BZCG))");
    std::smatch km;
    if (!std::regex_search(raw, km, kKey)) return sp;
    const std::string key = km.str();

    if (key == "TS") {
        sp.kind = 1;
        static const std::regex kVal(R"(TS\s*,?\s*([-+]?\d*\.?\d+))");
        std::smatch m;
        if (std::regex_search(raw, m, kVal)) sp.ts = (float)num(m.str(1));
        return sp;
    }

    if (key == "FP" || key == "FI") {
        sp.kind = (key == "FI") ? 3 : 2;
        static const std::regex kPair(R"(F([1-6])\s*,\s*([-+]?\d+))");
        for (auto it = std::sregex_iterator(raw.begin(), raw.end(), kPair);
             it != std::sregex_iterator(); ++it) {
            const int idx = std::stoi((*it)[1].str()) - 1;
            if (idx >= 0 && idx < 6) sp.fp[idx] = std::stoi((*it)[2].str());
        }
        return sp;
    }

    if (key == "GI" || key == "VI") {
        sp.kind = (key == "GI") ? 4 : 5;
        static const std::regex kQuoted(R"('([-+]?\d*\.?\d*)')");
        float vals[3] = {0, 0, 0};
        int n = 0;
        for (auto it = std::sregex_iterator(raw.begin(), raw.end(), kQuoted);
             it != std::sregex_iterator() && n < 3; ++it) {
            const std::string t = (*it)[1].str();
            if (t.empty()) continue;
            vals[n++] = (float)num(t);
        }
        if (sp.kind == 4) { sp.gi[0] = vals[0]; sp.gi[1] = vals[1]; }
        else { sp.vi[0] = vals[0]; sp.vi[1] = vals[1]; sp.vi[2] = vals[2]; }
        return sp;
    }

    if (key == "BZCG") {
        sp.kind = 6;
        static const std::regex kPoint(R"('(\d+)\s*,\s*([-+]?\d*\.?\d+)')");
        int n = 0;
        for (auto it = std::sregex_iterator(raw.begin(), raw.end(), kPoint);
             it != std::sregex_iterator() && n < 8; ++it) {
            sp.bzcg[n][0] = (float)num((*it)[1].str());
            sp.bzcg[n][1] = (float)num((*it)[2].str());
            ++n;
        }
        sp.bzcgN = n;
        return sp;
    }
    return sp;
}

}  // namespace detail
}  // namespace vx
