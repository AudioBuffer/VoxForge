// bench - 渲染 60 分钟不重复音频，测纯共振峰合成的速度（不含声码器）
#include "g2p.h"
#include "pinyin_ipa.h"
#include "voice.h"
#include "pitch_curve.h"
#include "tts/config.h"
#include "tts/klatt.h"
#include "tts/phonemes.h"
#include "tts/synth_internal.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    const double targetSec = (argc > 1) ? std::atof(argv[1]) : 3600.0;

    static const char* kSyl[] = {"ni", "hao", "shi", "jie", "zhong", "guo",
                                 "wo", "ai", "tian", "qi", "bu", "cuo"};

    tts::VoiceParams vp = tts::DefaultVoiceParams();
    vx::applyVoice(vx::voiceByName("female"), vp);
    vp.formant_smooth_ms = 32.0f;

    double renderedSec = 0.0;
    size_t notes = 0;
    const auto t0 = std::chrono::steady_clock::now();

    while (renderedSec < targetSec) {
        // 每轮生成一小段：3~6 个音节，音高随机走
        const int nSyl = 3 + (int)(notes % 4);
        std::vector<std::string> phones;
        for (int i = 0; i < nSyl; ++i) {
            const char* s = kSyl[(notes + (size_t)i) % 12];
            for (const auto& t : g2p::TokenizeIpa(vx::pinyinToIpa(s))) phones.push_back(t);
        }
        auto segs = tts::synth_detail::BuildSegments(phones, vp);
        float natural = 0.0f;
        for (const auto& s : segs) natural += s.dur_ms;
        const double dur = 0.42 * nSyl;
        const float scale = natural > 1.0f ? (float)(dur * 1000.0 / natural) : 1.0f;
        const size_t nFrames = tts::synth_detail::CountFrames(segs, scale);

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
        tts::synth_detail::FillParamTracks(segs, vp, scale, false, tr);

        std::vector<vx::PitchNote> pn;
        for (int i = 0; i < nSyl; ++i) {
            vx::PitchNote p;
            p.startSec = i * 0.42;
            p.durSec = 0.42;
            p.hz = 180.0 + 20.0 * ((notes + (size_t)i) % 7);
            p.portamentoMs = (i == 0) ? 0.0 : 50.0;
            p.humanity = 1.0;
            pn.push_back(p);
        }
        auto curve = vx::autoPitchCurve(pn, dur, tts::synth_detail::kFrameMs);
        for (size_t i = 0; i < nFrames && i < curve.size(); ++i) f0[i] = curve[i];

        tts::KlattSynth synth(44100.0f, tts::synth_detail::MakeKlattParams(vp));
        std::vector<float> buf(220);
        for (size_t i = 0; i < nFrames; ++i) {
            const auto cur = tts::synth_detail::FrameAt(tr, i);
            const auto nxt = tts::synth_detail::FrameAt(tr, std::min(nFrames - 1, i + 1));
            synth.RenderFrame(cur, nxt, 220, buf.data());
        }
        renderedSec += dur;
        notes += (size_t)nSyl;
    }

    const double ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count();
    std::printf("渲染 %.1f 秒（%.1f 分钟）音频\n", renderedSec, renderedSec / 60.0);
    std::printf("耗时 %.2f 秒（%.1f 分钟）\n", ms / 1000.0, ms / 60000.0);
    std::printf("RTF %.3f  实时倍数 %.0fx\n", ms / 1000.0 / renderedSec, renderedSec / (ms / 1000.0));
    std::printf("音符 %zu\n", notes);
    return 0;
}
