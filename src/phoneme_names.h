// 音素命名：对外用拼音（ASCII），引擎内部才用 IPA
//
//   ascii 名      IPA        例字
//   b             b         爸
//   p             pʰ        怕
//   m             m         妈
//   f             f         发
//   d             t         大
//   t             tʰ        他
//   n             n         那
//   l             l         拉
//   g             k         哥
//   k             kʰ        可
//   h             x         喝
//   j             tɕ        鸡
//   q             tɕʰ       七
//   x             ɕ         西
//   zh            ʈʂ        知
//   ch            ʈʂʰ       吃
//   sh            ʂ         诗
//   r             ɻ         日
//   z             ts        资
//   c             tsʰ       次
//   s             s         思
//   i             i         衣
//   ii            ɨ         知/资 的韵母
//   u             u         乌
//   v (ü)         y         鱼
//   a             ɑ         阿
//   o             ɔ         哦
//   e             ʌ         鹅
//   ai            ɑi        爱
//   ei            ei        诶
//   ao            ɑʊ        奥
//   ou            ou        欧
//   an            ɑn        安
//   en            ən        恩
//   ang           ɑŋ        昂
//   eng           əŋ        鞥
//   ong           ɔŋ        翁
//   er            ɝ         儿
//
// 日志、报错、调试一律打印 ascii 名；IPA 不出现在用户可见的地方。
#pragma once
#include <string>
#include <unordered_map>

namespace vx {

// IPA -> 拼音名（用于日志可读）
inline std::string ipaToAscii(const std::string& ipa) {
    static const std::unordered_map<std::string, std::string> m = {
        {"p", "b"}, {"p\u02B0", "p"}, {"m", "m"}, {"f", "f"},
        {"t", "d"}, {"t\u02B0", "t"}, {"n", "n"}, {"l", "l"},
        {"k", "g"}, {"k\u02B0", "k"}, {"x", "h"},
        {"t\u0255", "j"}, {"t\u0255\u02B0", "q"}, {"\u0255", "x"},
        {"\u0288\u0282", "zh"}, {"\u0288\u0282\u02B0", "ch"}, {"\u0282", "sh"},
        {"\u027B", "r"}, {"ts", "z"}, {"ts\u02B0", "c"}, {"s", "s"},
        {"i", "i"}, {"\u0268", "ii"}, {"u", "u"}, {"y", "v"},
        {"\u0251", "a"}, {"\u0254", "o"}, {"\u028C", "e"},
        {"\u025B", "e"}, {"\u0259", "\u0259"}, {"\u025D", "er"},
        {"\u0251i", "ai"}, {"ei", "ei"}, {"\u0251\u028A", "ao"}, {"ou", "ou"},
        {"\u0251n", "an"}, {"\u0259n", "en"}, {"\u0251\u014B", "ang"},
        {"\u0259\u014B", "eng"}, {"\u0254\u014B", "ong"},
        {"\u014B", "ng"},
    };
    std::string out;
    for (size_t i = 0; i < ipa.size();) {
        bool hit = false;
        for (int len = 4; len >= 1 && !hit; --len) {
            if (i + (size_t)len <= ipa.size()) {
                const std::string k = ipa.substr(i, (size_t)len);
                if (auto it = m.find(k); it != m.end()) {
                    out += it->second;
                    i += (size_t)len;
                    hit = true;
                }
            }
        }
        if (!hit) { out += ipa[i]; ++i; }
    }
    return out;
}

}  // namespace vx
