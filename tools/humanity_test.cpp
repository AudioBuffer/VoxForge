// humanity_test - 类人度滑块 A/B：0 / 0.5 / 1
#include "wav.h"
#include "pitch_curve.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

std::vector<float> glide(double humanity, double fromHz, double toHz, double glideMs, double holdMs) {
    const double fs = 44100.0, frameMs = 5.0;
    vx::PitchNote a;
    a.startSec = 0; a.durSec = holdMs / 1000.0; a.hz = fromHz;
    a.portamentoMs = 0; a.vibratoCents = 0; a.humanity = humanity;

    vx::PitchNote b;
    b.startSec = a.durSec;
    b.durSec = (glideMs + holdMs) / 1000.0;
    b.hz = toHz;
    b.portamentoMs = glideMs;
    b.portamentoCurve = vx::Bezier::sharp();
    b.vibratoCents = 25.0;
    b.vibratoFadeMs = 200.0;
    b.humanity = humanity;

    auto f0 = vx::renderPitchCurve({a, b}, b.startSec + b.durSec, frameMs);
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

int main() {
    for (double h : {0.0, 0.5, 1.0}) {
        auto pcm = glide(h, 220.0, 330.0, 90.0, 500.0);
        char name[64];
        std::snprintf(name, sizeof name, "/sdcard/Download/DSH-Go/human_%.0f.wav", h * 10);
        vx::writeWav16(name, pcm, 44100);
        std::printf("%s  %.2fs\n", name, pcm.size() / 44100.0);
    }
    return 0;
}
