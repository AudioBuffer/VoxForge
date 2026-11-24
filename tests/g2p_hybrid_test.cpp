#include "g2p_hybrid.h"
#include "g2p_svs.h"
#include <cstdio>

int main() {
    vx::G2PHybrid g;
    if (!g.loadDict("data/g2p_dict.txt")) { std::printf("FAIL dict\n"); return 1; }
    if (!g.loadPolyphones("data/polyphones.txt")) { std::printf("FAIL poly\n"); return 1; }
    const char* s = "银行行走";
    auto ws = g.analyze(s);
    for (const auto& w : ws)
        std::printf("%s -> %s %d%s\n", w.ch.c_str(), vx::svsSplit(w.syl).c_str(), w.tone,
                    w.poly ? "  [多音字]" : "");
    std::printf("PASS\n");
    return 0;
}
