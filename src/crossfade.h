// 音素交叠：前一元音的尾巴和后一辅音的头重叠
//
// OpenUtau / UTAU 的连续音（VCV）思路：音节之间不是硬切，
// 而是让前一个音的元音尾部慢慢淡出，同时下一个辅音已经起振。
//
// 这里对渲染后的单声道波形做交叠，长度可调（典型 40~80ms）。
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace vx {

// 把 b 的开头与 a 的结尾重叠 crossMs 毫秒，返回拼接结果
inline std::vector<float> crossfadeJoin(const std::vector<float>& a,
                                        const std::vector<float>& b,
                                        int crossMs, double fs) {
    const size_t x = (size_t)std::max(0.0, crossMs * fs / 1000.0);
    if (a.empty()) return b;
    if (b.empty()) return a;
    if (x == 0 || a.size() < x || b.size() < x) {
        std::vector<float> out = a;
        out.insert(out.end(), b.begin(), b.end());
        return out;
    }

    const size_t cut = a.size() - x;
    std::vector<float> out(cut + std::max(b.size(), x), 0.0f);

    std::copy(a.begin(), a.begin() + (long)cut, out.begin());

    // 交叠区：a 尾部淡出、b 头部淡入（等功率曲线）
    for (size_t i = 0; i < x; ++i) {
        const double u = (double)i / (double)x;
        const float gIn = (float)std::sin(u * M_PI * 0.5);
        const float gOut = (float)std::cos(u * M_PI * 0.5);
        out[cut + i] = a[cut + i] * gOut + b[i] * gIn;
    }
    // 剩余部分直接接上
    for (size_t i = x; i < b.size(); ++i) out[cut + i] = b[i];
    if (b.size() < x) out.resize(cut + x);
    return out;
}

}  // namespace vx
