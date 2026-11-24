// kt_sing - 歌唱合成（整首一次性建段，避免逐音符重置包络）
//
// 用法: kt_sing <out.wav> <out.mel> <voice> <旋律|midi:路径> [歌词]
//   旋律: "音高:时长:拼音" 用分号分隔，如 "60:0.6:yi;62:0.6:shan"
//   也支持 IN/EX 呼吸事件
#include "g2p.h"
#include "pinyin_ipa.h"
#include "japanese.h"
#include "voice.h"
#include "breath.h"
#include "residual.h"
#include "midi.h"
#include "pitch_curve.h"
#include "tts/config.h"
#include "tts/klatt.h"
#include "tts/phonemes.h"
#include "tts/synth_internal.h"
#include "wav.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr float kSR = 44100.0f;
constexpr int kHop = 256;
constexpr int kNfft = 1024;
constexpr int kMel = 80;

double midiToHz(int m) { return 440.0 * std::pow(2.0, (m - 69) / 12.0); }

struct Note {
    int midi = 60;
    double dur = 0.5;
    std::string lyric;
    bool breath = false;
};

std::vector<Note> parseMelody(const std::string& s) {
    std::vector<Note> out;
    size_t p = 0;
    while (p < s.size()) {
        const size_t semi = s.find(';', p);
        const std::string item = s.substr(p, semi == std::string::npos ? std::string::npos : semi - p);
        if (!item.empty()) {
            const size_t c1 = item.find(':');
            const size_t c2 = item.find(':', c1 + 1);
            if (c1 != std::string::npos && c2 != std::string::npos) {
                Note n;
                const std::string head = item.substr(0, c1);
                n.dur = std::atof(item.substr(c1 + 1, c2 - c1 - 1).c_str());
                n.lyric = item.substr(c2 + 1);
                if (head == "IN" || head == "EX") {
                    n.breath = true;
                    n.lyric = head;
                } else {
                    n.midi = std::atoi(head.c_str());
                }
                out.push_back(n);
            }
        }
        if (semi == std::string::npos) break;
        p = semi + 1;
    }
    return out;
}

void fft(std::vector<std::complex<float>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2 * M_PI / (double)len;
        const std::complex<float> w((float)std::cos(ang), (float)std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> wk(1);
            for (size_t j = 0; j < len / 2; ++j) {
                const auto u = a[i + j];
                const auto v = a[i + j + len / 2] * wk;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                wk *= w;
            }
        }
    }
}

std::vector<float> computeMel(const std::vector<float>& x) {
    const int half = kNfft / 2;
    auto hz2mel = [](double hz) { return 2595.0 * std::log10(1.0 + hz / 700.0); };
    auto mel2hz = [](double m) { return 700.0 * (std::pow(10.0, m / 2595.0) - 1.0); };
    const double lo = hz2mel(40.0), hi = hz2mel(kSR * 0.5);
    std::vector<double> c((size_t)kMel + 2);
    for (int i = 0; i < kMel + 2; ++i) c[(size_t)i] = mel2hz(lo + (hi - lo) * i / (kMel + 1));
    std::vector<std::vector<float>> fb((size_t)kMel, std::vector<float>((size_t)half + 1, 0.0f));
    for (int m = 0; m < kMel; ++m)
        for (int k = 0; k <= half; ++k) {
            const double f = k * (double)kSR / kNfft;
            const double a = c[(size_t)m], b = c[(size_t)m + 1], d = c[(size_t)m + 2];
            double w = 0.0;
            if (f >= a && f <= b && b > a) w = (f - a) / (b - a);
            else if (f > b && f <= d && d > b) w = (d - f) / (d - b);
            fb[(size_t)m][(size_t)k] = (float)w;
        }
    std::vector<float> win((size_t)kNfft);
    for (int i = 0; i < kNfft; ++i) win[(size_t)i] = (float)(0.5 - 0.5 * std::cos(2 * M_PI * i / (kNfft - 1)));
    std::vector<float> out;
    for (size_t s = 0; s + (size_t)kNfft <= x.size(); s += (size_t)kHop) {
        std::vector<std::complex<float>> buf((size_t)kNfft, 0.0f);
        for (int i = 0; i < kNfft; ++i) buf[(size_t)i] = x[s + (size_t)i] * win[(size_t)i];
        fft(buf);
        for (int m = 0; m < kMel; ++m) {
            double e = 0;
            for (int k = 0; k <= half; ++k) e += (double)fb[(size_t)m][(size_t)k] * std::norm(buf[(size_t)k]);
            out.push_back((float)std::log(std::max(1e-10, e)));
        }
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "用法: kt_sing <out.wav> <out.mel> <voice> <旋律|midi:路径> [歌词]\n");
        return 2;
    }
    const std::string outWav = argv[1];
    const std::string outMel = argv[2];
    const std::string voiceName = argv[3];
    const std::string melodyArg = argv[4];

    std::vector<Note> melody;
    if (melodyArg.rfind("midi:", 0) == 0) {
        vx::MidiSong song;
        std::string err;
        if (!vx::loadMidi(melodyArg.substr(5), song, err)) {
            std::fprintf(stderr, "MIDI 读取失败: %s\n", err.c_str());
            return 1;
        }
        std::vector<std::string> lyr;
        if (argc > 5) {
            std::string s5 = argv[5];
            size_t p = 0;
            while (p < s5.size()) {
                const size_t sp = s5.find(' ', p);
                const std::string w = s5.substr(p, sp == std::string::npos ? std::string::npos : sp - p);
                if (!w.empty()) lyr.push_back(w);
                if (sp == std::string::npos) break;
                p = sp + 1;
            }
        }
        for (size_t i = 0; i < song.notes.size(); ++i) {
            Note n;
            n.midi = song.notes[i].pitch;
            n.dur = song.notes[i].durSec;
            n.lyric = lyr.empty() ? "la" : lyr[i % lyr.size()];
            melody.push_back(n);
        }
    } else {
        melody = parseMelody(melodyArg);
    }
    if (melody.empty()) { std::fprintf(stderr, "旋律为空\n"); return 1; }

    tts::VoiceParams vp = tts::DefaultVoiceParams();
    vx::applyVoice(vx::voiceByName(voiceName), vp);
    vx::BreathSynth breath(kSR);

    // 全曲：一次性拼出所有音素，记录每个音素属于第几个音符
    std::vector<std::string> phones;
    std::vector<int> phoneNote;
    std::vector<double> noteStartMs;   // 每个音符在时间轴上的起点
    double tMs = 0.0;

    for (size_t i = 0; i < melody.size(); ++i) {
        const Note& n = melody[i];
        noteStartMs.push_back(tMs);
        tMs += n.dur * 1000.0;
        if (n.breath) continue;
        const bool isKana = n.lyric.size() >= 3 && (unsigned char)n.lyric[0] >= 0xE0;
        const std::string ipa = isKana ? vx::kanaToIpa(n.lyric) : vx::pinyinToIpa(n.lyric);
        for (const auto& t : g2p::TokenizeIpa(ipa)) {
            phones.push_back(t);
            phoneNote.push_back((int)i);
        }
        if (i + 1 == melody.size()) {
            phones.push_back(" ");
            phoneNote.push_back((int)i);
        }
    }

    auto segs = tts::synth_detail::BuildSegments(phones, vp);
    float segTotal = 0.0f;
    for (const auto& s : segs) segTotal += s.dur_ms;
    const float durScale = segTotal > 1.0f ? (float)(tMs / segTotal) : 1.0f;
    const size_t nFrames = tts::synth_detail::CountFrames(segs, durScale);
    if (nFrames == 0) { std::fprintf(stderr, "帧数为 0\n"); return 1; }

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

    // 音高曲线：OpenUtau 风格（portamento + 颤音 + 音高偏移）
    std::vector<vx::PitchNote> pn;
    double tAcc = 0.0;
    for (const auto& nt : melody) {
        if (nt.breath) { tAcc += nt.dur; continue; }
        vx::PitchNote p;
        p.startSec = tAcc;
        p.durSec = nt.dur;
        p.hz = midiToHz(nt.midi);
        p.portamentoMs = 60.0;
        p.vibratoCents = 25.0;
        p.vibratoHz = 5.5;
        p.vibratoFadeMs = 200.0;
        pn.push_back(p);
        tAcc += nt.dur;
    }
    auto curve = vx::renderPitchCurve(pn, tAcc, tts::synth_detail::kFrameMs);
    for (size_t i = 0; i < nFrames && i < curve.size(); ++i)
        if (curve[i] > 0.0f) f0[i] = curve[i];

    // 渲染
    tts::KlattSynth synth(kSR, tts::synth_detail::MakeKlattParams(vp));
    vx::ResidualModel residual(kSR);
    std::vector<float> pcm;
    pcm.reserve(nFrames * 220);
    std::vector<float> buf(220);
    for (size_t i = 0; i < nFrames; ++i) {
        const auto cur = tts::synth_detail::FrameAt(tr, i);
        const auto nxt = tts::synth_detail::FrameAt(tr, std::min(nFrames - 1, i + 1));
        synth.RenderFrame(cur, nxt, 220, buf.data());
        vx::ResidualFrame rf;
        rf.av = cur.av; rf.ah = cur.ah; rf.f1 = cur.f1; rf.f0 = cur.f0;
        std::vector<float> res;
        residual.render(rf, 220, res);
        for (size_t k = 0; k < buf.size(); ++k) buf[k] += res[k];
        pcm.insert(pcm.end(), buf.begin(), buf.end());
    }

    // 呼吸事件叠加（按音符位置插入）
    for (size_t i = 0; i < melody.size(); ++i) {
        const Note& n = melody[i];
        if (!n.breath) continue;
        auto br = breath.render(n.lyric == "IN" ? vx::BreathKind::kInhale : vx::BreathKind::kExhale, n.dur);
        const size_t at = (size_t)(noteStartMs[i] / 1000.0 * kSR);
        if (at + br.size() > pcm.size()) pcm.resize(at + br.size(), 0.0f);
        for (size_t k = 0; k < br.size(); ++k) pcm[at + k] += br[k];
    }

    float pk = 1e-6f;
    for (float v : pcm) pk = std::max(pk, std::fabs(v));
    for (float& v : pcm) v = std::max(-0.95f, std::min(0.95f, v * (0.85f / pk)));

    vx::writeWav16(outWav, pcm, (int)kSR);

    auto mel = computeMel(pcm);
    const size_t frames = mel.size() / (size_t)kMel;
    if (FILE* fm = std::fopen(outMel.c_str(), "wb")) {
        std::fwrite(mel.data(), sizeof(float), mel.size(), fm);
        std::fclose(fm);
    }
    // 导出逐帧 f0（给声码器，避免重新估音高造成卡顿）
    {
        std::string f0path = outMel;
        const size_t dot = f0path.find_last_of('.');
        if (dot != std::string::npos) f0path = f0path.substr(0, dot);
        f0path += ".f0";
        if (FILE* ff = std::fopen(f0path.c_str(), "wb")) {
            std::fwrite(f0.data(), sizeof(float), f0.size(), ff);
            std::fclose(ff);
        }
    }

    std::printf("%s  %.2fs  段 %zu 帧 %zu  mel %zux%d\n", outWav.c_str(),
                pcm.size() / kSR, segs.size(), nFrames, frames, kMel);
    return 0;
}
