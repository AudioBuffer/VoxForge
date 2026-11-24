// kt_multi - 多轨 MIDI：每轨指定声线，分别渲染后混音
//
// 用法: kt_multi <out.wav> <song.mid> <歌词文件|-> [女声male,female,child]
//   歌词文件：每行一句，按音符顺序分配到各轨
#include "japanese.h"
#include "midi.h"
#include "pinyin_ipa.h"
#include "voice.h"
#include "g2p.h"
#include "pitch_curve.h"
#include "tts/config.h"
#include "tts/klatt.h"
#include "tts/phonemes.h"
#include "tts/synth_internal.h"
#include "wav.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

struct Options {
    double tilt = 8.0;
    double open = 0.40;
    bool autoPitch = true;
};

// 单轨渲染
std::vector<float> renderTrack(const std::vector<vx::MidiNote>& notes,
                               const std::vector<std::string>& lyrics,
                               const std::string& voiceName,
                               const Options& opt, double totalSec) {
    if (notes.empty()) return {};
    tts::VoiceParams vp = tts::DefaultVoiceParams();
    vx::applyVoice(vx::voiceByName(voiceName), vp);
    vp.source_tilt_db = (float)opt.tilt;
    vp.glottal_open = (float)opt.open;
    vp.formant_smooth_ms = 32.0f;
    vp.av_smooth_ms = 12.0f;
    vp.af_release_ms = 14.0f;

    std::vector<std::string> phones;
    for (size_t i = 0; i < notes.size(); ++i) {
        const std::string& w = lyrics.empty() ? "la" : lyrics[i % lyrics.size()];
        const bool kana = w.size() >= 3 && (unsigned char)w[0] >= 0xE0;
        const std::string ipa = kana ? vx::kanaToIpa(w) : vx::pinyinToIpa(w);
        for (const auto& t : g2p::TokenizeIpa(ipa)) phones.push_back(t);
    }
    if (phones.empty()) return {};

    auto segs = tts::synth_detail::BuildSegments(phones, vp);
    float natural = 0.0f;
    for (const auto& s : segs) natural += s.dur_ms;
    const double noteTotal = totalSec * 1000.0;
    const float scale = natural > 1.0f ? (float)(noteTotal / natural) : 1.0f;
    const size_t nFrames = tts::synth_detail::CountFrames(segs, scale);
    if (nFrames == 0) return {};

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
    for (size_t i = 0; i < notes.size(); ++i) {
        vx::PitchNote p;
        p.startSec = notes[i].startSec;
        p.durSec = notes[i].durSec;
        p.hz = 440.0 * std::pow(2.0, (notes[i].pitch - 69) / 12.0);
        p.portamentoMs = (i == 0) ? 0.0 : 50.0;
        p.portamentoCurve = vx::Bezier::sharp();
        p.vibratoCents = 22.0;
        p.vibratoFadeMs = 180.0;
        p.humanity = 1.0;
        pn.push_back(p);
    }
    auto curve = opt.autoPitch ? vx::autoPitchCurve(pn, totalSec, tts::synth_detail::kFrameMs)
                               : vx::renderPitchCurve(pn, totalSec, tts::synth_detail::kFrameMs);
    for (size_t i = 0; i < nFrames && i < curve.size(); ++i) f0[i] = curve[i];

    tts::KlattSynth synth(44100.0f, tts::synth_detail::MakeKlattParams(vp));
    std::vector<float> pcm(nFrames * 220, 0.0f);
    std::vector<float> buf(220);
    for (size_t i = 0; i < nFrames; ++i) {
        const auto cur = tts::synth_detail::FrameAt(tr, i);
        const auto nxt = tts::synth_detail::FrameAt(tr, std::min(nFrames - 1, i + 1));
        synth.RenderFrame(cur, nxt, 220, buf.data());
        std::copy(buf.begin(), buf.end(), pcm.begin() + (long)(i * 220));
    }
    return pcm;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "用法: kt_multi <out.wav> <song.mid> <lyrics.txt|-> [voices]\n");
        return 2;
    }
    const std::string out = argv[1];
    const std::string mid = argv[2];
    const std::string lyrPath = argv[3];
    std::string voicesArg = (argc > 4) ? argv[4] : "female";

    vx::MidiSong song;
    std::string err;
    if (!vx::loadMidi(mid, song, err)) {
        std::fprintf(stderr, "MIDI: %s\n", err.c_str());
        return 1;
    }

    // 歌词
    std::vector<std::string> lyrics;
    if (lyrPath != "-") {
        std::ifstream f(lyrPath);
        std::string line;
        while (std::getline(f, line)) {
            size_t p = 0;
            while (p < line.size()) {
                const size_t sp = line.find(' ', p);
                const std::string w = line.substr(p, sp == std::string::npos ? std::string::npos : sp - p);
                if (!w.empty()) lyrics.push_back(w);
                if (sp == std::string::npos) break;
                p = sp + 1;
            }
        }
    }

    // 声线列表
    std::vector<std::string> voices;
    {
        size_t p = 0;
        while (p <= voicesArg.size()) {
            const size_t comma = voicesArg.find(',', p);
            const std::string v = voicesArg.substr(p, comma == std::string::npos ? std::string::npos : comma - p);
            if (!v.empty()) voices.push_back(v);
            if (comma == std::string::npos) break;
            p = comma + 1;
        }
    }

    // 按轨道分组
    std::map<int, std::vector<vx::MidiNote>> byTrack;
    for (const auto& n : song.notes) byTrack[n.track].push_back(n);

    double totalSec = 0.0;
    for (const auto& n : song.notes) totalSec = std::max(totalSec, n.startSec + n.durSec);
    totalSec += 0.3;

    std::printf("MIDI %zu 音符，%zu 轨，总长 %.2fs\n", song.notes.size(), byTrack.size(), totalSec);

    Options opt;
    std::vector<float> mix((size_t)(totalSec * 44100.0) + 220, 0.0f);
    int idx = 0;
    for (auto& [track, notes] : byTrack) {
        const std::string& v = voices[(size_t)idx % voices.size()];
        std::printf("  轨 %d: %zu 音符，声线 %s\n", track, notes.size(), v.c_str());
        auto pcm = renderTrack(notes, lyrics, v, opt, totalSec);
        for (size_t i = 0; i < pcm.size() && i < mix.size(); ++i) mix[i] += pcm[i] * 0.7f;
        ++idx;
    }

    float pk = 1e-6f;
    for (float s : mix) pk = std::max(pk, std::fabs(s));
    for (float& s : mix) s = std::max(-0.95f, std::min(0.95f, s * (0.85f / pk)));

    vx::writeWav16(out, mix, 44100);
    std::printf("%s  %.2fs\n", out.c_str(), mix.size() / 44100.0);
    return 0;
}
