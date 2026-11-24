// VoxSynth IR —— 工程格式 / 中间格式
//
// 文本形态（与用户既有工程格式一致）：
//   {
//   [synthesizer_start]
//   [歌词,音素,时长ms,位置ms,特殊参数]
//   [ni,ni,1000,10,NaN]
//   [synthesizer_end]
//   }
//   {
//   [F0_start]
//   [起始ms,起始s,持续ms,持续s,Hz,kHz,特殊参数]
//   ["10,0.01","1000,1","1030,1.03",NaN]
//   [F0_end]
//   }
//
// 特殊参数（互斥，每控制点仅一个）：
//   TS 颤音力度 0~10      <"TS,1">
//   FP 共振峰位置 ×6      <"FP,'F1,50''F2,25'...">
//   FI 共振峰强度 ×6      <"FI,'F1,50'...">
//   GI 直线滑音           <"GI,'440''880'">
//   VI 震动               <"VI,'起始''结束''速度'">
//   BZCG 贝塞尔滑音       <"BZCG'0,200''1,300'">
//
// 二进制中间格式：magic "VXIR" + 版本 + 结构数组，见 saveBinary/loadBinary。
#pragma once
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <sstream>
#include "dsl_regex.h"
#include <cctype>
#include <vector>

namespace vx {

struct FpParam {           // FP / FI
    int f[6]{0, 0, 0, 0, 0, 0};
    int intensity{0};      // 1=FI, 0=FP
};

struct Special {
    int   kind{0};         // 0 无, 1 TS, 2 FP, 3 FI, 4 GI, 5 VI, 6 BZCG
    float ts{0.0f};
    int   fp[6]{0};        // FP 或 FI 的 6 个值
    float gi[2]{0, 0};
    float vi[3]{0, 0, 0};
    float bzcg[8][2]{};    // 编号, 音高
    int   bzcgN{0};
};

struct LyricLine {
    std::string lyric;
    std::string phoneme;
    double durMs{0.0};
    double posMs{0.0};
    Special sp;
};

struct F0Point {
    double posMs{0.0};
    double durMs{0.0};
    double hz{0.0};
    Special sp;
};

struct Project {
    std::vector<LyricLine> lyric;
    std::vector<F0Point> f0;

    double totalMs() const {
        double t = 0.0;
        for (const auto& l : lyric) t = std::max(t, l.posMs + l.durMs);
        return t;
    }

    bool parseText(const std::string& s, std::string& error);
    bool loadBinary(const std::vector<uint8_t>& buf, std::string& error);
    std::vector<uint8_t> saveBinary() const;
};

// ---------------------------------------------------------------------------
// 解析实现
// ---------------------------------------------------------------------------
namespace detail {

inline std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

// 顶层逗号切分：忽略 <> 与单/双引号内部
inline std::vector<std::string> splitTop(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    int angle = 0;
    char quote = 0;
    for (char c : s) {
        if (quote) { if (c == quote) quote = 0; cur += c; continue; }
        if (c == '\'' || c == '"') { quote = c; cur += c; continue; }
        if (c == '<') { ++angle; cur += c; continue; }
        if (c == '>') { angle = std::max(0, angle - 1); cur += c; continue; }
        if (c == ',' && angle == 0) { out.push_back(trim(cur)); cur.clear(); continue; }
        cur += c;
    }
    if (!cur.empty() || !out.empty()) out.push_back(trim(cur));
    return out;
}

// 从 pos 起读一个整数（跳过空格与非数字前缀），失败返回 def
inline int readIntAt(const std::string& s, size_t pos, int def = 0) {
    while (pos < s.size() && !std::isdigit((unsigned char)s[pos]) && s[pos] != '-') ++pos;
    if (pos >= s.size()) return def;
    size_t end = pos;
    if (s[end] == '-') ++end;
    while (end < s.size() && std::isdigit((unsigned char)s[end])) ++end;
    try { return std::stoi(s.substr(pos, end - pos)); } catch (...) { return def; }
}

inline double toNum(const std::string& s, double def = 0.0) {
    // 支持 "10,0.01" 这种引号包裹的双单位写法：取引号内第一个数
    std::string t = s;
    const size_t q1 = t.find_first_of("\"'");
    if (q1 != std::string::npos) {
        const size_t q2 = t.find_first_of("\"'", q1 + 1);
        if (q2 != std::string::npos) t = t.substr(q1 + 1, q2 - q1 - 1);
    }
    const size_t comma = t.find(',');
    if (comma != std::string::npos) t = t.substr(0, comma);
    try { return std::stod(t); } catch (...) { return def; }
}

// 解析 <"TS,1" "FP,'F1,50'..."> 这类特殊参数
inline Special parseSpecial(const std::string& raw) {
    // 统一走正则解析器
    return parseSpecialRegex<Special>(raw);
#if 0
    Special sp;
    if (raw == "NaN" || raw.empty()) return sp;
    const size_t lt = raw.find('<');
    if (lt == std::string::npos) return sp;
    // 取所有引号内片段，拼成串
    std::string body;
    char quote = 0;
    const size_t q0 = raw.find_first_of("\"'", lt);
    for (size_t i = (q0 == std::string::npos ? lt : q0); i < raw.size(); ++i) {
        const char c = raw[i];
        if (quote) { if (c == quote) quote = 0; else body += c; continue; }
        if (c == '"' || c == '\'') { quote = c; continue; }
        if (c == '>') break;
        body += c;
    }
    const std::string key = trim(body.substr(0, body.find_first_of(" ,")));
    if (key == "TS") { sp.kind = 1; sp.ts = (float)toNum(trim(body.substr(body.find(',') + 1)), 0); }
    else if (key == "FP" || key == "FI") {
        sp.kind = (key == "FI") ? 3 : 2;
        // 形如 'F1,50''F2,25' -> 提取数字对
        int idx = 0;
        for (size_t i = 0; i < body.size() && idx < 6; ++i) {
            if (body[i] == 'F' && i + 1 < body.size() && std::isdigit((unsigned char)body[i + 1])) {
                const size_t comma = body.find(',', i);
                if (comma == std::string::npos) break;
                // 编号由 'F' 后那位数字决定，值取逗号后的数
                const int slot = body[i + 1] - '1';
                if (slot >= 0 && slot < 6)
                    sp.fp[slot] = readIntAt(body, comma + 1, 0);
                ++idx;
            }
        }
    } else if (key == "GI") {
        sp.kind = 4;
        int idx = 0;
        size_t p = 0;
        while (idx < 2) {
            const size_t q1 = body.find('\'', p);
            if (q1 == std::string::npos) break;
            const size_t q2 = body.find('\'', q1 + 1);
            if (q2 == std::string::npos) break;
            sp.gi[idx++] = (float)toNum(body.substr(q1 + 1, q2 - q1 - 1), 0);
            p = q2 + 1;
        }
    } else if (key == "VI") {
        sp.kind = 5;
        int idx = 0;
        size_t p = 0;
        while (idx < 3) {
            const size_t q1 = body.find('\'', p);
            if (q1 == std::string::npos) break;
            const size_t q2 = body.find('\'', q1 + 1);
            if (q2 == std::string::npos) break;
            sp.vi[idx++] = (float)toNum(body.substr(q1 + 1, q2 - q1 - 1), 0);
            p = q2 + 1;
        }
    } else if (key.rfind("BZCG", 0) == 0) {
        sp.kind = 6;
        int n = 0;
        size_t p = 0;
        while (n < 8) {
            const size_t q1 = body.find('\'', p);
            if (q1 == std::string::npos) break;
            const size_t q2 = body.find('\'', q1 + 1);
            if (q2 == std::string::npos) break;
            const std::string item = body.substr(q1 + 1, q2 - q1 - 1);
            const size_t c = item.find(',');
            if (c != std::string::npos) {
                sp.bzcg[n][0] = (float)toNum(item.substr(0, c), 0);
                sp.bzcg[n][1] = (float)toNum(item.substr(c + 1), 0);
                ++n;
            }
            p = q2 + 1;
        }
        sp.bzcgN = n;
    }
    return sp;
#endif
}

}  // namespace detail

inline bool Project::parseText(const std::string& src, std::string& error) {
    lyric.clear(); f0.clear();
    enum { kNone, kLyric, kF0 } mode = kNone;
    std::string line;
    std::istringstream in(src);
    while (std::getline(in, line)) {
        const std::string t = detail::trim(line);
        if (t.empty() || t == "{" || t == "}") continue;
        if (t == "[synthesizer_start]") { mode = kLyric; continue; }
        if (t == "[synthesizer_end]")   { mode = kNone; continue; }
        if (t == "[F0_start]")          { mode = kF0; continue; }
        if (t == "[F0_end]")            { mode = kNone; continue; }
        if (t.front() != '[' || t.back() != ']') continue;
        const std::string body = t.substr(1, t.size() - 2);
        auto f = detail::splitTop(body);
        if (mode == kLyric) {
            if (f.size() < 4) continue;
            if (f[0] == "歌词") continue;          // 表头
            LyricLine l;
            l.lyric = f[0];
            l.phoneme = f[1];
            l.durMs = detail::toNum(f[2]);
            l.posMs = detail::toNum(f[3]);
            l.sp = (f.size() > 4) ? detail::parseSpecial(f[4]) : Special{};
            lyric.push_back(l);
        } else if (mode == kF0) {
            if (f.size() < 3) continue;
            if (f[0].find("按毫秒计") != std::string::npos) continue;   // 表头
            F0Point p;
            p.posMs = detail::toNum(f[0]);
            p.durMs = detail::toNum(f[1]);
            p.hz    = detail::toNum(f[2]);
            p.sp    = (f.size() > 3) ? detail::parseSpecial(f[3]) : Special{};
            f0.push_back(p);
        }
    }
    if (lyric.empty()) { error = "没有歌词行"; return false; }
    return true;
}

// 二进制中间格式：magic + 版本 + 结构数组（小端）
inline std::vector<uint8_t> Project::saveBinary() const {
    std::vector<uint8_t> out;
    auto put = [&](const void* p, size_t n) {
        const auto* b = static_cast<const uint8_t*>(p);
        out.insert(out.end(), b, b + n);
    };
    const uint32_t magic = 0x52495856;   // "VXIR"
    const uint32_t ver = 1;
    const uint32_t nl = (uint32_t)lyric.size();
    const uint32_t nf = (uint32_t)f0.size();
    put(&magic, 4); put(&ver, 4); put(&nl, 4); put(&nf, 4);
    for (const auto& l : lyric) {
        const uint32_t dl = (uint32_t)l.lyric.size(), dp = (uint32_t)l.phoneme.size();
        put(&dl, 4); put(l.lyric.data(), dl);
        put(&dp, 4); put(l.phoneme.data(), dp);
        put(&l.durMs, 8); put(&l.posMs, 8);
        put(&l.sp, sizeof(Special));
    }
    for (const auto& p : f0) {
        put(&p.posMs, 8); put(&p.durMs, 8); put(&p.hz, 8);
        put(&p.sp, sizeof(Special));
    }
    return out;
}

inline bool Project::loadBinary(const std::vector<uint8_t>& buf, std::string& error) {
    lyric.clear(); f0.clear();
    if (buf.size() < 16) { error = "二进制过短"; return false; }
    uint32_t magic = 0, ver = 0, nl = 0, nf = 0;
    std::memcpy(&magic, buf.data(), 4);
    std::memcpy(&ver, buf.data() + 4, 4);
    std::memcpy(&nl, buf.data() + 8, 4);
    std::memcpy(&nf, buf.data() + 12, 4);
    if (magic != 0x52495856) { error = "magic 不匹配"; return false; }
    if (ver != 1) { error = "版本不支持"; return false; }
    size_t off = 16;
    auto get = [&](void* p, size_t n) {
        if (off + n > buf.size()) return false;
        std::memcpy(p, buf.data() + off, n); off += n; return true;
    };
    for (uint32_t i = 0; i < nl; ++i) {
        LyricLine l;
        uint32_t dl = 0, dp = 0;
        if (!get(&dl, 4)) { error = "截断"; return false; }
        l.lyric.resize(dl); if (!get(&l.lyric[0], dl)) { error = "截断"; return false; }
        if (!get(&dp, 4)) { error = "截断"; return false; }
        l.phoneme.resize(dp); if (!get(&l.phoneme[0], dp)) { error = "截断"; return false; }
        if (!get(&l.durMs, 8) || !get(&l.posMs, 8) || !get(&l.sp, sizeof(Special))) { error = "截断"; return false; }
        lyric.push_back(l);
    }
    for (uint32_t i = 0; i < nf; ++i) {
        F0Point p;
        if (!get(&p.posMs, 8) || !get(&p.durMs, 8) || !get(&p.hz, 8) || !get(&p.sp, sizeof(Special))) {
            error = "截断"; return false;
        }
        f0.push_back(p);
    }
    return true;
}

}  // namespace vx
