// VoxSynth G2P —— 文本到音素
//
// 中文：逐字查拼音表（zh_pinyin.h，来自 pypinyin，MIT）
// 英文/其他：暂按字母直出（后续接 CMUdict）
//
// 输出音节串，供 IR 的「歌词音素切分」字段使用。
#pragma once
#include <string>
#include <vector>
#include "zh_pinyin.h"

namespace vx {

struct G2PW {
    std::string text;      // 原字
    std::string phoneme;   // 拼音音节（无调）
};

inline bool isHanChar(const std::string& ch) {
    if (ch.size() < 3) return false;
    const unsigned char c0 = (unsigned char)ch[0];
    return c0 >= 0xE4 && c0 <= 0xE9;      // UTF-8 汉字首字节范围
}

// 按 UTF-8 拆成单字（简单版：3 字节一个字，ASCII 单独处理）
inline std::vector<std::string> utf8Chars(const std::string& s) {
    std::vector<std::string> out;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = (unsigned char)s[i];
        size_t len = 1;
        if (c >= 0xF0) len = 4;
        else if (c >= 0xE0) len = 3;
        else if (c >= 0xC0) len = 2;
        if (i + len > s.size()) len = 1;
        out.push_back(s.substr(i, len));
        i += len;
    }
    return out;
}

// 文本 -> 音节序列
inline std::vector<G2PW> g2p(const std::string& text) {
    std::vector<G2PW> out;
    for (const auto& ch : utf8Chars(text)) {
        if (isHanChar(ch)) {
            std::string py;
            if (vox::zhLookupPinyin(ch, py) && !py.empty()) {
                out.push_back({ch, py});
                continue;
            }
        }
        if (ch == " " || ch == "\n" || ch == "\t") continue;
        // 非汉字：原样保留（英文按字母映射留给后续）
        out.push_back({ch, ch});
    }
    return out;
}

}  // namespace vx
