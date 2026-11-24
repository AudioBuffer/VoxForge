// 潜空间管线自测：Mel -> z -> Mel' 的形状与数值健全性
#include "latent.h"
#include <cmath>
#include <cstdio>
#include <random>

int main() {
    const int T = 400, bins = 128;
    std::vector<std::vector<float>> mel((size_t)T, std::vector<float>((size_t)bins));
    // 真实 mel 在时间上平滑，用低通噪声模拟（白噪声下帧平均必然丢方差）
    std::mt19937 rng(1);
    std::normal_distribution<float> nd(0.0f, 1.0f);
    for (int t = 0; t < T; ++t)
        for (int b = 0; b < bins; ++b) {
            const float prev = (t > 0) ? mel[(size_t)t - 1][(size_t)b] : 0.0f;
            mel[(size_t)t][(size_t)b] = 0.92f * prev + 0.08f * nd(rng);
        }

    vx::LatentPipeline p;
    p.setShape({256, 4});
    auto z = p.encode(mel);
    if ((int)z.size() != T / 4 || (int)z[0].size() != 256) {
        std::printf("FAIL 形状 %zu x %zu\n", z.size(), z.empty() ? 0 : z[0].size());
        return 1;
    }
    p.diffuse(z, 0);
    auto back = p.decode(z, T, bins);
    if ((int)back.size() != T || (int)back[0].size() != bins) {
        std::printf("FAIL 重建形状\n");
        return 1;
    }
    // 能量应同量级（不是全零、不过度放大）
    double e0 = 0, e1 = 0;
    for (int t = 0; t < T; ++t) for (int b = 0; b < bins; ++b) {
        e0 += mel[(size_t)t][(size_t)b] * mel[(size_t)t][(size_t)b];
        e1 += back[(size_t)t][(size_t)b] * back[(size_t)t][(size_t)b];
    }
    const double ratio = e1 / std::max(1e-9, e0);
    std::printf("z %zux%zu  Mel' %zux%zu  能量比 %.3f\n",
                z.size(), z[0].size(), back.size(), back[0].size(), ratio);
    if (!(ratio > 0.5 && ratio < 2.0)) { std::printf("FAIL 能量比异常\n"); return 1; }
    std::printf("PASS\n");
    return 0;
}
