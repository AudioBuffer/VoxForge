// voxsynth-backend —— AGPLv3 核心进程
// stdin 读工程 IR（Project 格式）→ 解析 → Klatt 渲染 → 共享内存环形缓冲输出 PCM
#include "ir.h"
#include "ring.h"
#include "klatt.h"
#include "phonemes.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstdio>
#include <map>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

// 声母分类：0 无, 1 擦音, 2 塞音, 3 鼻音/流音
struct IniInfo { int cls = 0; float fc = 3000.0f; float gain = 0.0f; };
IniInfo initialOf(const std::string& ph) {
    IniInfo r;
    const size_t slash = ph.find('/');
    if (slash == std::string::npos || slash == 0) return r;
    const std::string ini = ph.substr(0, slash);
    r.cls = 1;
    if (ini == "s" || ini == "z" || ini == "c")          { r.fc = 6500; r.gain = 0.75f; }
    else if (ini == "sh" || ini == "zh" || ini == "ch")  { r.fc = 3200; r.gain = 0.70f; }
    else if (ini == "x" || ini == "j" || ini == "q")     { r.fc = 4200; r.gain = 0.65f; }
    else if (ini == "f" || ini == "h")                   { r.fc = 1600; r.gain = 0.55f; }
    else if (ini == "p" || ini == "t" || ini == "k" ||
             ini == "b" || ini == "d" || ini == "g")     { r.cls = 2; r.fc = 2400; r.gain = 0.85f; }
    else if (ini == "m" || ini == "n" || ini == "l" || ini == "r") { r.cls = 3; r.fc = 700; r.gain = 0.25f; }
    return r;
}

bool isApical(const std::string& ph) {
    const size_t slash = ph.find('/');
    if (slash == std::string::npos) return false;
    const std::string ini = ph.substr(0, slash);
    if (ph.substr(slash + 1) != "i") return false;
    return ini == "zh" || ini == "ch" || ini == "sh" || ini == "r" ||
           ini == "z"  || ini == "c"  || ini == "s";
}

// 韵母 -> IPA 元音（vendored 表里的键）
const char* finalToIpa(const std::string& fin) {
    static const std::map<std::string, const char*> kMap = {
        {"a","\u0251"}, {"o","o"}, {"e","\u028C"}, {"i","i"}, {"u","u"}, {"v","y"},
        {"ai","\u0251"}, {"ei","e"}, {"ao","\u0251"}, {"ou","o"},
        {"an","\u0251"}, {"en","\u0259"}, {"ang","\u0251"}, {"eng","\u0259"},
        {"ong","o"}, {"er","\u025D"},
        {"ia","\u0251"}, {"ie","\u025B"}, {"iao","\u0251"}, {"iu","o"},
        {"ian","\u025B"}, {"in","i"}, {"iang","\u0251"}, {"ing","i"}, {"iong","o"},
        {"ua","\u0251"}, {"uo","o"}, {"uai","\u0251"}, {"ui","e"},
        {"uan","\u0251"}, {"un","u"}, {"uang","\u0251"}, {"ueng","\u0259"},
        {"van","\u025B"}, {"ve","\u025B"}, {"vn","y"},
    };
    auto it = kMap.find(fin);
    return it == kMap.end() ? "\u0259" : it->second;
}

int vowelOf(const std::string& ph) {
    // 取音素串里最后的元音字母；"h/ao" 这类取斜杠后
    std::string s = ph;
    const size_t slash = s.find('/');
    if (slash != std::string::npos) s = s.substr(slash + 1);
    for (size_t i = 0; i < s.size(); ++i) {
        switch (s[i]) {
            case 'a': return 0;
            case 'o': return 1;
            case 'e': return 2;
            case 'i': return 3;
            case 'u': return 4;
            case 'v': case 'y': return 5;
            default: break;
        }
    }
    return 0;
}

double f0At(const vx::Project& p, double tMs) {
    double hz = 0.0;
    for (const auto& q : p.f0) {
        if (q.durMs <= 0) continue;
        if (tMs >= q.posMs && tMs < q.posMs + q.durMs) return q.hz;
        if (q.posMs <= tMs) hz = q.hz;         // 最近一个已过点
    }
    return hz > 0 ? hz : 200.0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "用法: voxsynth-backend <shm_name> <ring_floats>\n");
        return 2;
    }
    const char* shmName = argv[1];
    const size_t capFloats = (size_t)std::strtoull(argv[2], nullptr, 10);

    const size_t bytes = sizeof(vx::RingHeader) + capFloats * sizeof(float);
    const int fd = shm_open(shmName, O_CREAT | O_RDWR, 0600);
    if (fd < 0) { std::perror("shm_open"); return 1; }
    if (ftruncate(fd, (off_t)bytes) != 0) { std::perror("ftruncate"); return 1; }
    void* base = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (base == MAP_FAILED) { std::perror("mmap"); return 1; }
    auto* hdr = static_cast<vx::RingHeader*>(base);
    auto* data = reinterpret_cast<float*>(static_cast<char*>(base) + sizeof(vx::RingHeader));
    hdr->capacity.store((uint32_t)capFloats, std::memory_order_relaxed);

    std::string text((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
    vx::Project proj;
    std::string err;
    if (!proj.parseText(text, err)) {
        std::fprintf(stderr, "IR 解析失败: %s\n", err.c_str());
        return 1;
    }

    const double fs = 44100.0;
    hdr->sampleRate.store((uint32_t)fs, std::memory_order_relaxed);

    tts::KlattParams p;
    tts::KlattSynth synth((float)fs, p);

    const int spf = (int)(fs * 0.001);
    const double totalMs = proj.totalMs();
    const int nFrames = (int)(totalMs / 5.0) + 1;
    // 拼音主元音 -> IPA，再用 vendored 调好的音素表取共振峰
    static const char* kIpa[6] = {"\u0251", "o", "e", "i", "u", "y"};
    static const float kV[6][3] = {
        {800, 1250, 2550}, {520, 880, 2500}, {520, 1250, 2400},
        {300, 2300, 3000}, {320, 780, 2400}, {300, 1900, 2300},
    };
    // ---- 段结构：成阻 → 爆破 → 送气 → 过渡 → 稳态元音 ----
    std::vector<tts::SynthFrame> frames;
    frames.reserve((size_t)(totalMs / 5.0) + 8);
    const tts::Phone* vowelPhone[6] = {
        tts::LookupPhone("\u0251"), tts::LookupPhone("o"), tts::LookupPhone("e"),
        tts::LookupPhone("i"), tts::LookupPhone("u"), tts::LookupPhone("y"),
    };
    // 辅音位点（locus）：爆破/擦音的起始共振峰，向元音过渡
    auto pushFrame = [&](float f0, float f1, float f2, float f3,
                         float b1, float b2, float b3,
                         float av, float af, float ah, float fcf, float nasal) {
        tts::SynthFrame fr;
        fr.f0 = f0; fr.f1 = f1; fr.f2 = f2; fr.f3 = f3;
        fr.b1 = b1; fr.b2 = b2; fr.b3 = b3;
        fr.av = av; fr.af = af; fr.ah = ah;
        fr.fric_cf = fcf; fr.nasal = nasal;
        frames.push_back(fr);
    };

    for (const auto& l : proj.lyric) {
        const size_t sl = l.phoneme.find('/');
        const std::string fin = (sl == std::string::npos) ? l.phoneme : l.phoneme.substr(sl + 1);
        const tts::Phone* vp = tts::LookupPhone(finalToIpa(fin));
        const int v = vowelOf(l.phoneme);
        (void)v;
        float vf1 = vp ? vp->f1 : 600.0f, vf2 = vp ? vp->f2 : 1200.0f, vf3 = vp ? vp->f3 : 2500.0f;
        float vb1 = vp ? vp->b1 : 70.0f,  vb2 = vp ? vp->b2 : 110.0f, vb3 = vp ? vp->b3 : 160.0f;
        if (isApical(l.phoneme)) { vf1 = 400; vf2 = 1400; vf3 = 2000; }

        const IniInfo ini = initialOf(l.phoneme);
        const float f0 = (float)f0At(proj, l.posMs + l.durMs * 0.5);
        const int nFrames = (int)(l.durMs / 5.0);

        // ---- 段时长（毫秒）----
        // 塞音：成阻 30 + 爆破 8 + 送气 22 + 过渡 40
        // 擦音：噪声 90 + 过渡 40
        // 鼻/流音：浊段 70 + 过渡 40
        // 零声母：过渡 35
        double tClose = 0, tBurst = 0, tAsp = 0, tNoise = 0, tNasal = 0, tTrans = 35;
        if (ini.cls == 2)      { tClose = 30; tBurst = 8; tAsp = 22; tTrans = 40; }
        else if (ini.cls == 1) { tNoise = 90; tTrans = 40; }
        else if (ini.cls == 3) { tNasal = 70; tTrans = 40; }

        // 起始共振峰位点（按发音部位）
        float L1 = vf1, L2 = vf2, L3 = vf3;
        if (ini.cls == 2 || ini.cls == 1) {
            const std::string ini0 = l.phoneme.substr(0, l.phoneme.find('/'));
            if (ini0 == "b" || ini0 == "p" || ini0 == "m" || ini0 == "f") { L1 = 300; L2 = 800;  L3 = 2300; }
            else if (ini0 == "d" || ini0 == "t" || ini0 == "n" || ini0 == "l") { L1 = 300; L2 = 1800; L3 = 2600; }
            else if (ini0 == "g" || ini0 == "k" || ini0 == "h")               { L1 = 300; L2 = 2000; L3 = 2400; }
            else { L1 = 350; L2 = 1600; L3 = 2400; }
        }

        const double tSeg1 = tClose + tBurst + tAsp + tNoise + tNasal;
        const double tVowelStart = tSeg1 + tTrans;

        for (int i = 0; i < nFrames; ++i) {
            const double rel = i * 5.0;
            float av = 0, af = 0, ah = 0, nasalAmt = 0;
            float cf1 = vf1, cf2 = vf2, cf3 = vf3;

            if (rel < tClose) {
                // 成阻：静音，共振峰停在位点
                cf1 = L1; cf2 = L2; cf3 = L3;
            } else if (rel < tClose + tBurst) {
                // 爆破
                af = ini.gain;
                cf1 = L1; cf2 = L2; cf3 = L3;
            } else if (rel < tClose + tBurst + tAsp) {
                // 送气
                av = 0.12f; ah = 0.30f; af = ini.gain * 0.35f;
                const double u = (rel - tClose - tBurst) / std::max(1.0, tAsp);
                cf1 = (float)(L1 + (vf1 - L1) * u * 0.5);
                cf2 = (float)(L2 + (vf2 - L2) * u * 0.5);
                cf3 = (float)(L3 + (vf3 - L3) * u * 0.5);
            } else if (rel < tClose + tBurst + tAsp + tNoise) {
                // 擦音噪声
                const double u = (rel - tClose - tBurst - tAsp) / std::max(1.0, tNoise);
                const double env = std::min(1.0, u * 8.0) * std::min(1.0, (1.0 - u) * 6.0);
                af = (float)(ini.gain * env);
                av = (float)(0.10 * env);
                cf1 = vf1; cf2 = vf2; cf3 = vf3;
            } else if (rel < tClose + tBurst + tAsp + tNoise + tNasal) {
                // 鼻音/流音
                av = 0.70f; af = ini.gain * 0.3f; nasalAmt = 0.85f;
                cf1 = 250; cf2 = (l.phoneme[0] == 'm') ? 1100 : 1600; cf3 = 2400;
            } else if (rel < tVowelStart) {
                // 过渡段：位点 → 元音
                const double u = (rel - tSeg1) / std::max(1.0, tTrans);
                av = (float)(0.55 + 0.30 * u);
                cf1 = (float)(L1 + (vf1 - L1) * u);
                cf2 = (float)(L2 + (vf2 - L2) * u);
                cf3 = (float)(L3 + (vf3 - L3) * u);
            } else {
                // 稳态元音 + 收尾
                const double left = l.durMs - rel;
                av = (float)(0.85 * std::min(1.0, left / 30.0));
            }

            pushFrame(f0, cf1, cf2, cf3, vb1, vb2, vb3, av, af, ah, ini.fc, nasalAmt);
        }
    }

    auto pcm = synth.Render(frames, spf);
    {
        float pk = 1e-6f;
        for (float v : pcm) pk = std::max(pk, std::fabs(v));
        const float g = 0.85f / pk;
        for (float& v : pcm) v = std::max(-0.98f, std::min(0.98f, v * g));
    }

    vx::RingWriter w(hdr, data);
    size_t off = 0;
    while (off < pcm.size()) {
        const size_t k = w.write(pcm.data() + off, pcm.size() - off);
        off += k;
        if (k == 0) usleep(1000);
    }
    w.close();
    return 0;
}
