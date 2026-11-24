// VoxSynth G2P —— SVS 字符串版
//
// 规则切分：声母 + "/" + 韵母，零声母时只有韵母。
//   ni   -> n/i
//   hao  -> h/ao
//   shi  -> sh/i
//   an   -> an
//   zhong-> zh/ong
//
// 这套给 SVS（歌声合成）用：音节边界清晰，便于按音素分配时长。
#pragma once
#include <string>
#include <vector>
#include "zhtext.h"

namespace vx {

// 声母表（按长度降序匹配，先双字母再单字母）
inline std::string splitInitial(const std::string& py, std::string& final) {
    static const char* kInitials[] = {
        "zh", "ch", "sh", "b", "p", "m", "f", "d", "t", "n", "l",
        "g", "k", "h", "j", "q", "x", "r", "z", "c", "s", "y", "w",
    };
    // y/w 是零声母的书写规则，要还原成真正的韵母（yi->i, wu->u, yu->v）
    if (!py.empty() && (py[0] == 'y' || py[0] == 'w')) {
        const std::string rest = py.substr(1);
        if (py[0] == 'y') {
            if (rest.empty())      { final = "i"; return std::string(); }
            if (rest[0] == 'i')    { final = rest; return std::string(); }
            if (rest[0] == 'u')    { final = "v" + rest.substr(1); return std::string(); }
            if (rest[0] == 'v')    { final = rest; return std::string(); }
            final = "i" + rest;    return std::string();
        }
        if (rest.empty())          { final = "u"; return std::string(); }
        if (rest[0] == 'u')        { final = rest; return std::string(); }
        final = "u" + rest;        return std::string();
    }
    final = py;
    for (const char* ini : kInitials) {
        const std::string s(ini);
        if (py.size() > s.size() && py.compare(0, s.size(), s) == 0) {
            // y/w 在拼音里是介音标记，归入韵母更自然（yi -> i, wu -> u）
            if (s == "y" || s == "w") continue;
            final = py.substr(s.size());
            return s;
        }
    }
    return std::string();
}

// 单音节 -> "声母/韵母" 或 "韵母"
inline std::string svsSplit(const std::string& py) {
    std::string fin;
    const std::string ini = splitInitial(py, fin);
    if (ini.empty()) return fin;
    return ini + "/" + fin;
}

// 整句 -> 音节串（每音之间用空格或调用方决定分隔）
inline std::vector<std::string> g2pSvs(const std::string& text) {
    std::vector<std::string> out;
    for (const auto& w : g2p(text)) out.push_back(svsSplit(w.phoneme));
    return out;
}

}  // namespace vx
