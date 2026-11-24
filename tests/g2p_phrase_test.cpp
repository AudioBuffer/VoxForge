#include "g2p_phrase.h"
#include "g2p_svs.h"
#include <cstdio>

int main() {
    vx::G2PPhrase g;
    if (!g.loadChars("data/g2p_char.txt")) { std::printf("FAIL char\n"); return 1; }
    if (!g.loadPhrases("data/g2p_phrase.txt")) { std::printf("FAIL phrase\n"); return 1; }
    std::printf("单字 %zu | 词组 %zu\n", g.charCount(), g.phraseCount());

    const char* cases[] = {"银行", "行走", "重庆", "重量", "音乐", "快乐", "长大", "长度"};
    for (const char* c : cases) {
        auto rs = g.analyze(c);
        std::printf("%-6s ->", c);
        for (const auto& r : rs) std::printf(" %s(%d)", r.syl.c_str(), r.tone);
        std::printf("\n");
    }
    // 关键对照
    auto a = g.analyze("银行");
    auto b = g.analyze("行走");
    const bool ok = (a.size() == 2 && a[1].syl == "hang") && (b.size() == 2 && b[0].syl == "xing");
    std::printf(ok ? "PASS\n" : "FAIL\n");
    return ok ? 0 : 1;
}
