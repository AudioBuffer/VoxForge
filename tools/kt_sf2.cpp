// kt_sf2 - 用 TinySoundFont 渲染 GM 音色
//   用法: kt_sf2 <out.wav> <font.sf2> [program] [音高...]
#define TSF_IMPLEMENTATION
#include "tsf.h"
#include "wav.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

// 把音名（C4/A#3）转频率
double noteToHz(const std::string& s) {
    if (s.empty()) return 440.0;
    static const int kSemis[7] = {9, 11, 0, 2, 4, 5, 7};   // A B C D E F G
    char letter = s[0];
    char up = (char)std::toupper(letter);
    int idx = -1;
    if (up >= 'A' && up <= 'G') idx = up - 'A';
    if (idx < 0) return std::atof(s.c_str());
    int semi = kSemis[idx];
    size_t p = 1;
    if (p < s.size() && s[p] == '#') { ++semi; ++p; }
    else if (p < s.size() && s[p] == 'b') { --semi; ++p; }
    int octave = 4;
    if (p < s.size()) octave = std::atoi(s.c_str() + p);
    const int midi = (octave + 1) * 12 + semi;
    return 440.0 * std::pow(2.0, (midi - 69) / 12.0);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "用法: kt_sf2 <out.wav> <font.sf2> <program> [C4 D4 E4 ...]\n");
        return 2;
    }
    const std::string out = argv[1];
    const std::string font = argv[2];
    const int program = std::atoi(argv[3]);

    tsf* sf = tsf_load_filename(font.c_str());
    if (!sf) { std::fprintf(stderr, "加载失败: %s\n", font.c_str()); return 1; }
    tsf_set_output(sf, TSF_STEREO_INTERLEAVED, 44100, 0.0f);
    std::printf("音色库: %s  预设数 %d\n", font.c_str(), tsf_get_presetcount(sf));
    const int presetIdx = tsf_get_presetindex(sf, 0, program);
    if (presetIdx < 0) { std::fprintf(stderr, "找不到 program %d\n", program); return 1; }
    std::printf("预设: %s\n", tsf_get_presetname(sf, presetIdx));

    std::vector<short> pcm;
    const int block = 512;
    std::vector<short> buf(block * 2);
    for (int i = 4; i < argc; ++i) {
        const double hz = noteToHz(argv[i]);
        // MIDI 音高
        const int midi = (int)std::lround(69.0 + 12.0 * std::log2(hz / 440.0));
        tsf_note_on(sf, presetIdx, midi, 1.0f);
        // 渲染 0.6 秒
        const int frames = (int)(0.6 * 44100);
        int done = 0;
        while (done < frames) {
            const int n = std::min(block, frames - done);
            tsf_render_short(sf, buf.data(), n, 0);
            for (int k = 0; k < n * 2; ++k) pcm.push_back(buf[(size_t)k]);
            done += n;
        }
        tsf_note_off(sf, presetIdx, midi);
        // 尾巴
        for (int k = 0; k < 6; ++k) {
            tsf_render_short(sf, buf.data(), block, 0);
            for (int k2 = 0; k2 < block * 2; ++k2) pcm.push_back(buf[(size_t)k2]);
        }
    }
    tsf_close(sf);

    // 立体声短整型 -> 单声道 float
    std::vector<float> mono(pcm.size() / 2);
    for (size_t i = 0; i < mono.size(); ++i)
        mono[i] = (pcm[i * 2] + pcm[i * 2 + 1]) / 65536.0f;
    float pk = 1e-6f;
    for (float v : mono) pk = std::max(pk, std::fabs(v));
    for (float& v : mono) v = std::max(-0.95f, std::min(0.95f, v * (0.85f / pk)));

    vx::writeWav16(out, mono, 44100);
    std::printf("%s  %.2fs\n", out.c_str(), mono.size() / 44100.0);
    return 0;
}
