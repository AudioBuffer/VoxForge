// kt_line - 直接喂 IPA 串合成一句话（1ms 帧，导出 f0 给声码器）
//   用法: kt_line <out.wav> "<ipa>" [基频Hz]
#include "g2p.h"
#include "voice.h"
#include "tts/config.h"
#include "tts/klatt.h"
#include "tts/phonemes.h"
#include "tts/synth_internal.h"
#include "wav.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "用法: kt_line <out.wav> \"<ipa>\" [基频Hz]\n");
        return 2;
    }
    const std::string out = argv[1];
    const std::string ipa = argv[2];
    const double baseHz = (argc > 3) ? std::atof(argv[3]) : 180.0;

    const float fs = 44100.0f;
    tts::VoiceParams vp = tts::DefaultVoiceParams();
    vx::applyVoice(vx::voiceByName("female"), vp);

    std::vector<std::string> phones = g2p::TokenizeIpa(ipa);
    if (phones.empty()) { std::fprintf(stderr, "音素为空\n"); return 1; }

    auto segs = tts::synth_detail::BuildSegments(phones, vp);
    const size_t nFrames = tts::synth_detail::CountFrames(segs, 1.0f);

    std::vector<float> f1(nFrames), f2(nFrames), f3(nFrames), b1(nFrames), b2(nFrames),
        b3(nFrames), av(nFrames), af(nFrames), ah(nFrames), nasal(nFrames),
        fnp(nFrames), fnz(nFrames), fcf(nFrames), accent(nFrames), f0(nFrames);
    std::vector<uint8_t> major(nFrames);
    tts::synth_detail::ParamTracks tr;
    tr.f1 = f1.data(); tr.f2 = f2.data(); tr.f3 = f3.data();
    tr.b1 = b1.data(); tr.b2 = b2.data(); tr.b3 = b3.data();
    tr.av = av.data(); tr.af = af.data(); tr.ah = ah.data();
    tr.nasal = nasal.data(); tr.fnp = fnp.data(); tr.fnz = fnz.data();
    tr.fric_cf = fcf.data(); tr.accent = accent.data(); tr.f0 = f0.data();
    tr.major = major.data(); tr.n = nFrames;
    tts::synth_detail::FillParamTracks(segs, vp, 1.0f, false, tr);

    // 音高：句尾轻微下降 + 轻颤音
    const double totalSec = nFrames * tts::synth_detail::kFrameMs / 1000.0;
    for (size_t i = 0; i < nFrames; ++i) {
        const double t = i * tts::synth_detail::kFrameMs / 1000.0;
        double h = baseHz * (1.0 - 0.06 * (t / std::max(0.1, totalSec)));
        if (t > 0.3) {
            const double depth = 12.0 * std::min(1.0, (t - 0.3) / 0.4);
            h *= std::pow(2.0, depth * std::sin(2 * M_PI * 5.2 * t) / 1200.0);
        }
        f0[i] = (float)h;
    }

    tts::KlattSynth synth(fs, tts::synth_detail::MakeKlattParams(vp));
    std::vector<float> pcm;
    pcm.reserve(nFrames * 220);
    std::vector<float> buf(220);
    for (size_t i = 0; i < nFrames; ++i) {
        const auto cur = tts::synth_detail::FrameAt(tr, i);
        const auto nxt = tts::synth_detail::FrameAt(tr, std::min(nFrames - 1, i + 1));
        synth.RenderFrame(cur, nxt, 220, buf.data());
        pcm.insert(pcm.end(), buf.begin(), buf.end());
    }

    float pk = 1e-6f;
    for (float v : pcm) pk = std::max(pk, std::fabs(v));
    for (float& v : pcm) v = std::max(-0.95f, std::min(0.95f, v * (0.85f / pk)));
    vx::writeWav16(out, pcm, (int)fs);

    std::string f0path = out + ".f0";
    if (FILE* ff = std::fopen(f0path.c_str(), "wb")) {
        std::fwrite(f0.data(), sizeof(float), f0.size(), ff);
        std::fclose(ff);
    }
    std::printf("%s  %.2fs  %zu 帧\n", out.c_str(), pcm.size() / fs, nFrames);
    return 0;
}
