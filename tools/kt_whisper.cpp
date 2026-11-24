// kt_whisper - 气声说话（全曲无浊音）+ 大量吸气/呼气
//   用法: kt_whisper <out.wav> "<ipa 用 | 分隔短语>"
#include "g2p.h"
#include "breath.h"
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

namespace {

// 把一段 IPA 渲染成气声
std::vector<float> whisperPhrase(const std::string& ipa, const tts::VoiceParams& vp,
                                 double baseHz, bool heavy) {
    auto phones = g2p::TokenizeIpa(ipa);
    if (phones.empty()) return {};
    auto segs = tts::synth_detail::BuildSegments(phones, vp);
    const size_t n = tts::synth_detail::CountFrames(segs, 1.0f);
    if (n == 0) return {};

    std::vector<float> f1(n), f2(n), f3(n), b1(n), b2(n), b3(n), av(n), af(n), ah(n),
        nasal(n), fnp(n), fnz(n), fcf(n), accent(n), f0(n);
    std::vector<uint8_t> major(n);
    tts::synth_detail::ParamTracks tr;
    tr.f1 = f1.data(); tr.f2 = f2.data(); tr.f3 = f3.data();
    tr.b1 = b1.data(); tr.b2 = b2.data(); tr.b3 = b3.data();
    tr.av = av.data(); tr.af = af.data(); tr.ah = ah.data();
    tr.nasal = nasal.data(); tr.fnp = fnp.data(); tr.fnz = fnz.data();
    tr.fric_cf = fcf.data(); tr.accent = accent.data(); tr.f0 = f0.data();
    tr.major = major.data(); tr.n = n;
    tts::synth_detail::FillParamTracks(segs, vp, 1.0f, false, tr);

    // 气声：浊音关掉，送气拉满
    for (size_t i = 0; i < n; ++i) {
        av[i] = 0.0f;
        ah[i] = std::min(1.0f, ah[i] * (heavy ? 3.0f : 2.0f) + (heavy ? 0.45f : 0.30f));
        if (af[i] > 0.0f) af[i] *= 0.7f;
        f0[i] = (float)baseHz;
    }

    tts::KlattSynth synth(44100.0f, tts::synth_detail::MakeKlattParams(vp));
    std::vector<float> pcm;
    pcm.reserve(n * 220);
    std::vector<float> buf(220);
    for (size_t i = 0; i < n; ++i) {
        const auto cur = tts::synth_detail::FrameAt(tr, i);
        const auto nxt = tts::synth_detail::FrameAt(tr, std::min(n - 1, i + 1));
        synth.RenderFrame(cur, nxt, 220, buf.data());
        pcm.insert(pcm.end(), buf.begin(), buf.end());
    }
    return pcm;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "用法: kt_whisper <out.wav> \"<ipa|ipa|...>\"\n");
        return 2;
    }
    const std::string out = argv[1];
    const std::string script = argv[2];
    const double baseHz = (argc > 3) ? std::atof(argv[3]) : 165.0;

    tts::VoiceParams vp = tts::DefaultVoiceParams();
    vx::applyVoice(vx::voiceByName("female"), vp);
    vp.lf_rd = 1.6f;              // 松一点，气声更自然
    vp.source_tilt_db = 2.0f;
    vx::BreathSynth breath(44100.0);

    // 按 | 切分短语
    std::vector<std::string> phrases;
    {
        size_t p = 0;
        while (p <= script.size()) {
            const size_t bar = script.find('|', p);
            const std::string ph = script.substr(p, bar == std::string::npos ? std::string::npos : bar - p);
            if (!ph.empty()) phrases.push_back(ph);
            if (bar == std::string::npos) break;
            p = bar + 1;
        }
    }

    std::vector<float> all;
    // 开场：长吸气
    auto in0 = breath.render(vx::BreathKind::kInhale, 1.2);
    all.insert(all.end(), in0.begin(), in0.end());

    for (size_t i = 0; i < phrases.size(); ++i) {
        auto seg = whisperPhrase(phrases[i], vp, baseHz, i % 2 == 0);
        all.insert(all.end(), seg.begin(), seg.end());

        // 句间：呼气 + 再吸气（大喘气）
        if (i + 1 < phrases.size()) {
            auto ex = breath.render(vx::BreathKind::kExhale, 0.9);
            all.insert(all.end(), ex.begin(), ex.end());
            auto in = breath.render(vx::BreathKind::kInhale, 0.85);
            all.insert(all.end(), in.begin(), in.end());
        }
    }

    // 收尾：长呼气
    auto exEnd = breath.render(vx::BreathKind::kSigh, 1.6);
    all.insert(all.end(), exEnd.begin(), exEnd.end());

    float pk = 1e-6f;
    for (float v : all) pk = std::max(pk, std::fabs(v));
    for (float& v : all) v = std::max(-0.95f, std::min(0.95f, v * (0.85f / pk)));

    vx::writeWav16(out, all, 44100);
    std::printf("%s  %.2fs  短语 %zu\n", out.c_str(), all.size() / 44100.0, phrases.size());
    return 0;
}
