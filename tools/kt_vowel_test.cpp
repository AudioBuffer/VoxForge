// 最小验证：直接喂一个稳态元音帧给 KlattSynth，看输出是否像人声
#include "klatt.h"
#include "wav.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char** argv) {
    const float fs = 44100.0f;
    const int spf = (int)(fs * 0.005f);
    tts::KlattParams p;                    // 默认参数
    if (argc > 2) p.voice_gain = (float)std::atof(argv[2]);
    tts::KlattSynth s(fs, p);

    tts::SynthFrame fr;
    fr.f0 = 200.0f;
    fr.f1 = 700.0f; fr.f2 = 1200.0f; fr.f3 = 2500.0f;
    fr.b1 = 70.0f;  fr.b2 = 110.0f;  fr.b3 = 160.0f;
    fr.av = 1.0f;
    fr.af = 0.0f; fr.ah = 0.0f; fr.nasal = 0.0f;

    std::vector<tts::SynthFrame> frames(100, fr);   // 0.5 秒
    auto pcm = s.Render(frames, spf);

    float pk = 1e-6f;
    for (float v : pcm) pk = std::max(pk, std::fabs(v));
    const float g = 0.8f / pk;
    for (float& v : pcm) v *= g;

    double rms = 0.0;
    int zc = 0;
    for (size_t i = 0; i < pcm.size(); ++i) {
        rms += (double)pcm[i] * pcm[i];
        if (i && ((pcm[i] < 0) != (pcm[i - 1] < 0))) ++zc;
    }
    rms = std::sqrt(rms / (double)pcm.size());
    std::printf("样本 %zu 峰值 %.3f RMS %.3f 过零率 %.4f\n",
                pcm.size(), pk, rms, (double)zc / (double)pcm.size());

    const char* out = (argc > 1) ? argv[1] : "/tmp/kt_vowel.wav";
    vx::writeWav16(out, pcm, (int)fs);
    std::printf("写出 %s\n", out);
    return 0;
}
