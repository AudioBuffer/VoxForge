// 可逆性验证：forward 后 inverse 必须还原（误差 < 1e-4）
#include "inn.h"
#include <cmath>
#include <cstdio>
#include <random>

int main() {
    const int dim = 256;
    vx::AffineCoupling c;
    c.init(dim, 0.05f, 12345u);

    std::mt19937 rng(7);
    std::normal_distribution<float> nd(0.0f, 1.0f);
    std::vector<float> x((size_t)dim), y, back;
    for (auto& v : x) v = nd(rng);

    c.forward(x, y);
    c.inverse(y, back);

    double err = 0.0;
    for (int i = 0; i < dim; ++i) err = std::max(err, (double)std::fabs(back[(size_t)i] - x[(size_t)i]));
    std::printf("维度 %d  参数量 %zu  forward->inverse 最大误差 %.2e\n", dim, c.params(), err);
    if (err > 1e-4) { std::printf("FAIL\n"); return 1; }
    std::printf("PASS\n");
    return 0;
}
