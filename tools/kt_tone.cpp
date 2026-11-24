// kt_tone - 带声调的中文合成
//
// 与 kt_say 的区别：不用 BeginIpa（它只有固定句调），而是
//   1. 自己调 BuildSegments 拿到段序列
//   2. FillParamTracks 生成帧参数
//   3. 按声调改写每个韵母段的 F0 曲线
//   4. 逐帧喂给 KlattSynth
#include "g2p.h"
#include "pinyin_ipa.h"
#include "voice.h"
#include "g2p_phrase.h"
#include "tts/config.h"
#include "tts/klatt.h"
#include "tts/phonemes.h"
#include "tts/synth_internal.h"
#include "wav.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

// 声调相对音高曲线（五度标记归一化）
float toneCurve(int tone, float u) {
    static const float kT[5][3] = {
        {1.00f, 1.00f, 1.00f},   // 1 阴平
        {0.88f, 1.00f, 1.12f},   // 2 阳平
        {0.85f, 0.72f, 1.05f},   // 3 上声
        {1.15f, 0.95f, 0.75f},   // 4 去声
        {0.95f, 0.95f, 0.95f},   // 5 轻声
    };
    const int t = std::max(1, std::min(5, tone)) - 1;
    const float seg = std::min(2.0f, u * 3.0f);
    const int i = (int)seg;
    const float f = seg - i;
    const float a = kT[t][i], b = kT[t][std::min(2, i + 1)];
    return a + (b - a) * f;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "用法: kt_tone <out.wav> <voice> <文本>\n");
        return 2;
    }
    const std::string out = argv[1];
    const std::string voiceName = argv[2];
    const std::string text = argv[3];

    vx::G2PPhrase g2p;
    g2p.loadChars("data/g2p_char.txt");
    g2p.loadPhrases("data/g2p_phrase.txt");
    const auto words = g2p.analyze(text);

    // 音节 -> IPA 音素 token
    std::vector<std::vector<std::string>> sylTokens;
    std::vector<int> sylTone;
    std::string ipaAll;
    for (const auto& w : words) {
        const std::string ipa = vx::pinyinToIpa(w.syl);
        sylTokens.push_back(g2p::TokenizeIpa(ipa));
        sylTone.push_back(w.tone);
        ipaAll += ipa + " ";
    }
    std::printf("音素: %s\n", ipaAll.c_str());

    tts::VoiceParams vp = tts::DefaultVoiceParams();
    vx::applyVoice(vx::voiceByName(voiceName), vp);

    // 展开成完整 phone 序列（含音节间停顿）
    std::vector<std::string> phones;
    for (size_t i = 0; i < sylTokens.size(); ++i) {
        for (const auto& t : sylTokens[i]) phones.push_back(t);
        phones.push_back(" ");
    }

    auto segs = tts::synth_detail::BuildSegments(phones, vp);
    const float durScale = 1.0f;
    const size_t nFrames = tts::synth_detail::CountFrames(segs, durScale);

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
    tts::synth_detail::FillParamTracks(segs, vp, durScale, false, tr);

    // 按段推进帧位置，对每个韵母段套声调曲线
    size_t frame = 0;
    int vowelIdx = 0;
    for (const auto& s : segs) {
        const size_t n = (size_t)std::max(0.0f, std::round(s.dur_ms * durScale / tts::synth_detail::kFrameMs));
        if (s.is_vowel) {
            const int tone = (vowelIdx < (int)sylTone.size()) ? sylTone[(size_t)vowelIdx] : 1;
            for (size_t k = 0; k < n && frame + k < nFrames; ++k) {
                const float u = n > 1 ? (float)k / (float)(n - 1) : 0.0f;
                f0[frame + k] *= toneCurve(tone, u);
            }
            ++vowelIdx;
        }
        frame += n;
    }

    tts::KlattSynth synth(44100.0f, tts::synth_detail::MakeKlattParams(vp));
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

    vx::writeWav16(out, pcm, 44100);
    std::printf("%s  %.2fs  段 %zu 帧 %zu\n", out.c_str(), pcm.size() / 44100.0, segs.size(), nFrames);
    return 0;
}
