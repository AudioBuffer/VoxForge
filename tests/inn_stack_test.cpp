// 堆叠可逆块：互逆性 + 显存占用与层数无关
#include "inn_stack.h"
#include <cmath>
#include <cstdio>
#include <random>

int main() {
    const int dim = 256;
    bool ok = true;
    for (int layers : {1, 4, 8, 16}) {
        vx::InvertibleStack st;
        st.init(dim, layers, 0.05f, 4242u);

        std::mt19937 rng(11);
        std::normal_distribution<float> nd(0.0f, 1.0f);
        std::vector<float> x((size_t)dim), y, back;
        for (auto& v : x) v = nd(rng);

        st.forward(x, y);
        st.inverse(y, back);

        double err = 0.0;
        for (int i = 0; i < dim; ++i)
            err = std::max(err, (double)std::fabs(back[(size_t)i] - x[(size_t)i]));
        const size_t act = st.storedActivations();
        std::printf("层数 %2d  参数 %6zu  误差 %.2e  存储缓冲 %zu 个 float\n",
                    layers, st.params(), err, act);
        if (err > 1e-4) ok = false;
        if (act > (size_t)dim * 2) ok = false;
    }
    std::printf(ok ? "PASS\n" : "FAIL\n");
    return ok ? 0 : 1;
}
