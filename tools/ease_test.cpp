// ease_test - 对比不同转音曲线的贝塞尔形状（输出数值表 + wav）
#include "pitch_curve.h"
#include "easing.h"
#include "wav.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

std::vector<float> glideTone(const vx::Bezier& curve, double fromHz, double toHz,
                             double glideMs, double holdMs) {
    const double fs = 44100.0;
    const double frameMs = 5.0;
    vx::PitchNote a;
    a.startSec = 0.0;
    a.durSec = holdMs / 1000.0;
    a.hz = fromHz;
    a.portamentoMs = 0;          // 第一个音不滑
    a.vibratoCents = 0;

    vx::PitchNote b;
    b.startSec = a.durSec;
    b.durSec = (glideMs + holdMs) / 1000.0;
    b.hz = toHz;
    b.portamentoMs = glideMs;
    b.portamentoCurve = curve;
    b.vibratoCents = 0;

    const std::vector<vx::PitchNote> notes{a, b};
    auto f0 = vx::renderPitchCurve(notes, b.startSec + b.durSec, frameMs);

    // 简单正弦合成，方便听音高轨迹
    std::vector<float> pcm;
    const int spf = (int)(fs * frameMs / 1000.0);
    double phase = 0.0;
    for (float hz : f0) {
        for (int i = 0; i < spf; ++i) {
            phase += 2 * M_PI * hz / fs;
            pcm.push_back((float)(0.3 * std::sin(phase)));
        }
    }
    return pcm;
}

}  // namespace

int main(int argc, char** argv) {
    const char* name = (argc > 1) ? argv[1] : "sharp";
    const auto curve = vx::Bezier::byName(name);
    std::printf("曲线 %s  (%.2f,%.2f,%.2f,%.2f)\n", name, curve.x1, curve.y1, curve.x2, curve.y2);
    std::printf("t     value\n");
    for (int i = 0; i <= 10; ++i) {
        const double t = i / 10.0;
        std::printf("%.1f   %.3f\n", t, curve.eval(t));
    }

    auto pcm = glideTone(curve, 220.0, 330.0, 80.0, 400.0);
    const std::string out = std::string("/sdcard/Download/DSH-Go/ease_") + name + ".wav";
    vx::writeWav16(out, pcm, 44100);
    std::printf("%s  %.2fs\n", out.c_str(), pcm.size() / 44100.0);
    return 0;
}
