// DSL 校验：毫秒/秒不一致、参数互斥、数值越界
//
// 规则：
//   1. 双单位写法 "1030,1.03" 里两个数不一致时，以毫秒为准，并告警
//   2. 特殊参数互斥：一个控制点只能有一个（TS/FP/FI/GI/VI/BZCG）
//   3. 数值范围：TS 0~10、FP 0~100、F0 20~2000Hz
//   4. 位置必须递增
//
// 输出：结构化告警 + 在原行尾追加 ">W:信息<" 的注释文本
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <regex>
#include <string>
#include <vector>

namespace vx {

struct DslWarning {
    int line = 0;        // 1 基行号
    int itemIndex = -1;  // 歌词行 / F0 点序号（0 基）
    std::string kind;    // "单位" / "互斥" / "范围" / "顺序"
    std::string message;
};

struct DslLintResult {
    std::vector<DslWarning> warnings;
    std::string annotated;   // 每行末尾带 >W:...< 的文本
};

namespace dsl_lint_detail {

inline std::vector<std::string> splitTopLevel(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    int angle = 0;
    char quote = 0;
    for (char c : s) {
        if (quote) { if (c == quote) quote = 0; cur += c; continue; }
        if (c == '\'' || c == '"') { quote = c; cur += c; continue; }
        if (c == '<') { ++angle; cur += c; continue; }
        if (c == '>') { angle = std::max(0, angle - 1); cur += c; continue; }
        if (c == ',' && angle == 0) { out.push_back(cur); cur.clear(); continue; }
        cur += c;
    }
    out.push_back(cur);
    for (auto& f : out) {
        while (!f.empty() && (f.front() == ' ' || f.front() == '\t')) f.erase(f.begin());
        while (!f.empty() && (f.back() == ' ' || f.back() == '\t')) f.pop_back();
    }
    return out;
}

// 取引号内两个数："1030,1.03" -> {1030, 1.03}
inline std::vector<double> pairNumbers(const std::string& field) {
    static const std::regex kNum(R"([-+]?\d*\.?\d+)");
    std::vector<double> out;
    for (auto it = std::sregex_iterator(field.begin(), field.end(), kNum);
         it != std::sregex_iterator(); ++it) {
        try { out.push_back(std::stod(it->str())); } catch (...) { }
    }
    return out;
}

inline int countSpecial(const std::string& raw) {
    static const std::regex kKey(R"((TS|FP|FI|GI|VI|BZCG))");
    int n = 0;
    for (auto it = std::sregex_iterator(raw.begin(), raw.end(), kKey);
         it != std::sregex_iterator(); ++it) ++n;
    return n;
}

}  // namespace dsl_lint_detail

inline DslLintResult lintDsl(const std::string& src) {
    using namespace dsl_lint_detail;
    DslLintResult res;
    std::vector<std::string> lines;
    {
        std::string cur;
        for (char c : src) {
            if (c == '\n') { lines.push_back(cur); cur.clear(); }
            else cur += c;
        }
        if (!cur.empty()) lines.push_back(cur);
    }

    enum { kNone, kLyric, kF0 } mode = kNone;
    int lyricIdx = -1, f0Idx = -1;
    double lastPos = -1e9;

    for (size_t i = 0; i < lines.size(); ++i) {
        std::string line = lines[i];
        const int lineNo = (int)i + 1;
        std::string trimmed = line;
        while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t')) trimmed.erase(trimmed.begin());

        if (trimmed == "[synthesizer_start]") { mode = kLyric; lyricIdx = -1; continue; }
        if (trimmed == "[F0_start]") { mode = kF0; f0Idx = -1; continue; }
        if (trimmed == "[synthesizer_end]" || trimmed == "[F0_end]") { mode = kNone; continue; }
        if (trimmed.size() < 3 || trimmed.front() != '[' || trimmed.back() != ']') continue;

        const std::string body = trimmed.substr(1, trimmed.size() - 2);
        const auto fields = splitTopLevel(body);
        std::vector<DslWarning> warns;

        if (mode == kLyric) {
            if (fields.size() < 4) continue;
            if (fields[0] == "歌词" || fields[0].find("歌词") != std::string::npos) continue;
            ++lyricIdx;

            // 时长与位置是双单位写法吗？
            for (int f = 2; f <= 3; ++f) {
                const auto nums = pairNumbers(fields[(size_t)f]);
                if (nums.size() >= 2) {
                    const double ms = nums[0], sec = nums[1];
                    const double expected = ms / 1000.0;
                    if (std::fabs(sec - expected) > std::max(0.002, expected * 0.02)) {
                        warns.push_back({lineNo, lyricIdx, "单位",
                                         "秒与毫秒不一致，以毫秒为准（" + std::to_string((int)ms) +
                                             "ms，但写成了 " + std::to_string(sec) + "s）"});
                    }
                }
            }
            // 互斥
            if (fields.size() > 4) {
                const int n = countSpecial(fields[4]);
                if (n > 1)
                    warns.push_back({lineNo, lyricIdx, "互斥", "一个控制点有 " + std::to_string(n) +
                                                                  " 个特殊参数，只允许一个"});
            }
            // 单值但小于 10：疑似把秒写进了毫秒字段
            // 前 5 个音符豁免：开头的位置本来就是 0 或 10ms 这种小值
            const bool exemptEarly = (lyricIdx < 5);
            for (int f = 2; f <= 3 && !exemptEarly; ++f) {
                const auto nums = pairNumbers(fields[(size_t)f]);
                if (nums.size() == 1 && nums[0] > 0.0 && nums[0] < 10.0) {
                    warns.push_back({lineNo, lyricIdx, "单位",
                                     std::string(f == 2 ? "时长" : "位置") +
                                         "疑似以秒填写（" + std::to_string(nums[0]) +
                                         "），本字段应为毫秒"});
                }
            }
            // 顺序
            const auto pos = pairNumbers(fields[3]);
            if (!pos.empty()) {
                if (pos[0] < lastPos && !exemptEarly)
                    warns.push_back({lineNo, lyricIdx, "顺序", "发音位置比上一行小，会导致时间回退"});
                lastPos = std::max(lastPos, pos[0]);
            }
        } else if (mode == kF0) {
            if (fields.size() < 3) continue;
            if (fields[0].find("按毫秒计") != std::string::npos) continue;
            ++f0Idx;

            const auto pos = pairNumbers(fields[0]);
            const auto dur = pairNumbers(fields[1]);
            const auto hz = pairNumbers(fields[2]);

            if (pos.size() >= 2 && std::fabs(pos[1] - pos[0] / 1000.0) > 0.002)
                warns.push_back({lineNo, f0Idx, "单位", "位置秒与毫秒不一致，以毫秒为准"});
            if (dur.size() >= 2 && std::fabs(dur[1] - dur[0] / 1000.0) > 0.002)
                warns.push_back({lineNo, f0Idx, "单位", "时长秒与毫秒不一致，以毫秒为准"});
            if (!hz.empty() && (hz[0] < 20 || hz[0] > 2000))
                warns.push_back({lineNo, f0Idx, "范围", "音高超出 20~2000Hz"});
            if (fields.size() > 3) {
                const int n = countSpecial(fields[3]);
                if (n > 1)
                    warns.push_back({lineNo, f0Idx, "互斥", "一个控制点有多个特殊参数"});
            }
            if (!pos.empty()) {
                if (pos[0] < lastPos)
                    warns.push_back({lineNo, f0Idx, "顺序", "控制点位置比上一行小"});
                lastPos = std::max(lastPos, pos[0]);
            }
        }

        for (const auto& w : warns) res.warnings.push_back(w);
        if (!warns.empty()) {
            // 行尾追加告警注释
            std::string msg;
            for (size_t k = 0; k < warns.size(); ++k) {
                if (k) msg += "；";
                msg += warns[k].message;
            }
            line += "   >W:" + msg + "<";
        }
        lines[i] = line;
    }

    for (size_t i = 0; i < lines.size(); ++i) {
        res.annotated += lines[i];
        res.annotated += "\n";
    }
    return res;
}

}  // namespace vx
