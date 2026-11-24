#include "g2p_model.h"
#include <cstdio>
int main(){
    vx::G2PModel m;
    if (!m.load("data/g2p_model.bin")) { std::printf("FAIL 加载模型\n"); return 1; }
    struct C { std::vector<std::string> ctx; int pos; };
    const C cases[] = {
        {{"你","好"}, 0}, {{"你","好"}, 1}, {{"世","界"}, 0},
        {{"中","国"}, 0}, {{"银","行"}, 1},
    };
    for (const auto& c : cases) {
        std::string syl; int tone = 0;
        if (!m.predict(c.ctx, c.pos, syl, tone)) { std::printf("FAIL 预测\n"); return 1; }
        std::printf("%s%s -> %s 声调 %d\n", c.ctx[0].c_str(), c.ctx[1].c_str(), syl.c_str(), tone);
    }
    std::printf("PASS\n");
    return 0;
}
