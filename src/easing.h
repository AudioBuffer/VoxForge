// 缓动曲线：用三次贝塞尔控制「当前值逼近目标值」的节奏
//
// CSS 那套 cubic-bezier(x1,y1,x2,y2)，控制点固定在 (0,0) 和 (1,1)：
//
//   (0,0) ──P1── P2── (1,1)
//
//   linear      (0, 0, 1, 1)        匀速
//   ease        (0.25, 0.1, 0.25, 1) 两头慢中间快（默认）
//   ease-in     (0.42, 0, 1, 1)     起步慢
//   ease-out    (0, 0, 0.58, 1)     收尾慢
//   sharp       (0.4, 0, 0.2, 1)    起步快收尾慢，转音最常用
//   soft        (0.5, 0, 0.5, 1)    对称柔和
#pragma once
#include <algorithm>
#include <cmath>
#include <string>

namespace vx {

struct Bezier {
    double x1 = 0.25, y1 = 0.1, x2 = 0.25, y2 = 1.0;

    Bezier() = default;
    Bezier(double a, double b, double c, double d) : x1(a), y1(b), x2(c), y2(d) {}

    // 给定时间比例 t∈[0,1]，返回曲线上的值
    double eval(double t) const {
        t = std::clamp(t, 0.0, 1.0);
        // 先由 x 反解参数 u，再取 y
        const double u = solveU(t);
        return sampleY(u);
    }

    // 预设
    static Bezier linear() { return {0, 0, 1, 1}; }
    static Bezier ease() { return {0.25, 0.1, 0.25, 1.0}; }
    static Bezier easeIn() { return {0.42, 0, 1, 1}; }
    static Bezier easeOut() { return {0, 0, 0.58, 1}; }
    static Bezier sharp() { return {0.4, 0, 0.2, 1}; }
    static Bezier soft() { return {0.5, 0, 0.5, 1}; }

    static Bezier byName(const std::string& n) {
        if (n == "linear") return linear();
        if (n == "ease-in") return easeIn();
        if (n == "ease-out") return easeOut();
        if (n == "sharp") return sharp();
        if (n == "soft") return soft();
        return ease();
    }

private:
    double sampleX(double u) const {
        const double v = 1 - u;
        return 3 * v * v * u * x1 + 3 * v * u * u * x2 + u * u * u;
    }
    double sampleY(double u) const {
        const double v = 1 - u;
        return 3 * v * v * u * y1 + 3 * v * u * u * y2 + u * u * u;
    }
    // 二分求 u 使 sampleX(u) ≈ x
    double solveU(double x) const {
        double lo = 0.0, hi = 1.0;
        for (int i = 0; i < 24; ++i) {
            const double mid = 0.5 * (lo + hi);
            if (sampleX(mid) < x) lo = mid;
            else hi = mid;
        }
        return 0.5 * (lo + hi);
    }
};

// 一阶逼近器：让当前值以时间常数 tauMs 逼近目标
class Slew {
public:
    explicit Slew(double tauMs = 20.0) : tau_(tauMs) { }

    void setTau(double tauMs) { tau_ = std::max(0.01, tauMs); }

    // dtMs：这一帧的时长
    double step(double current, double target, double dtMs) {
        const double a = std::exp(-dtMs / tau_);
        return target + (current - target) * a;
    }

private:
    double tau_;
};

}  // namespace vx
