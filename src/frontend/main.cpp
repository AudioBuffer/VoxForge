// voxsynth-frontend —— MIT 前端进程
//
// 职责：打包 IR → 拉起后端进程 → 从共享内存环形缓冲取 PCM → 落盘/播放
// 与后端之间只有 stdin（IR）与共享内存（PCM），没有 DSO 依赖。
#include "ir.h"
#include "zhtext.h"
#include "g2p_svs.h"
#include "g2p_phrase.h"
#include "ring.h"
#include "wav.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <array>
#include <algorithm>
#include <vector>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    const char* outPath = (argc > 1) ? argv[1] : "/tmp/voxsynth_ipc.wav";
    const char* backend = (argc > 2) ? argv[2] : "./voxsynth-backend";
    const size_t capFloats = 1u << 18;      // 256k 采样 ≈ 6 秒 @44.1k

    // 1) G2P：词库版（词组优先 + 单字兜底），带声调
    const char* lyric = (argc > 3) ? argv[3] : "你好世界";
    vx::G2PPhrase g2p;
    g2p.loadChars("data/g2p_char.txt");
    g2p.loadPhrases("data/g2p_phrase.txt");
    auto reads = g2p.analyze(lyric);
    const auto chars = vx::utf8Chars(lyric);

    // 声调 -> 相对音高轮廓（五度标记法：1=55 2=35 3=214 4=51）
    auto toneContour = [](int tone, double base) {
        static const float kT[5][3] = {
            {5, 5, 5},      // 1 阴平
            {3, 4, 5},      // 2 阳平
            {2, 1, 4},      // 3 上声
            {5, 3, 1},      // 4 去声
            {3, 3, 3},      // 5 轻声
        };
        const int t = std::max(1, std::min(5, tone));
        return std::array<float, 3>{ base * kT[t-1][0] / 5.0f,
                                     base * kT[t-1][1] / 5.0f,
                                     base * kT[t-1][2] / 5.0f };
    };

    std::string blocks = "{\n[synthesizer_start]\n[歌词,歌词音素切分,发音时长,发音位置,特殊参数]\n";
    double posMs = 10.0;
    const double baseF0 = 200.0;
    struct F0Pt { double ms, dur, hz; };
    std::vector<F0Pt> f0pts;
    double decl = 1.0;     // 句调下倾：整句基频逐渐下降
    for (size_t i = 0; i < reads.size(); ++i) {
        // 时长按声调变化：上声最长，去声最短；音节间留 30ms
        static const double kDur[5] = {520.0, 540.0, 600.0, 470.0, 420.0};
        const int tIdx = std::max(1, std::min(5, reads[i].tone)) - 1;
        const double dur = kDur[tIdx];
        decl *= 0.985;                    // 每音节降 1.5%
        const std::string ph = vx::svsSplit(reads[i].syl);
        const std::string ch = (i < chars.size()) ? chars[i] : reads[i].syl;
        char line[256];
        std::snprintf(line, sizeof line, "[%s,%s,%.0f,%.0f,NaN]\n",
                      ch.c_str(), ph.c_str(), dur, posMs);
        blocks += line;
        // 每个音节按声调生成 3 个 F0 控制点
        const auto c = toneContour(reads[i].tone, baseF0 * decl);
        for (int k = 0; k < 3; ++k)
            f0pts.push_back({posMs + dur * k / 3.0, dur / 3.0, c[(size_t)k]});
        posMs += dur + 60.0;              // 音节间留白
    }
    blocks += "[synthesizer_end]\n}\n{\n[F0_start]\n";
    for (const auto& q : f0pts) {
        char fl[192];
        std::snprintf(fl, sizeof fl, "[\"%.0f,%.2f\",\"%.0f,%.2f\",\"%.1f,%.3f\",NaN]\n",
                      q.ms, q.ms / 1000.0, q.dur, q.dur / 1000.0, q.hz, q.hz / 1000.0);
        blocks += fl;
    }
    blocks += "[F0_end]\n}\n";
    const std::string irText = blocks;
    std::printf("IR %zu 字节\n", irText.size());

    // 2) 共享内存
    char shmName[64];
    std::snprintf(shmName, sizeof shmName, "/voxsynth_%d", (int)getpid());
    shm_unlink(shmName);
    const size_t bytes = sizeof(vx::RingHeader) + capFloats * sizeof(float);

    // 3) 拉起后端（IPC：stdin 传 IR）
    int toChild[2];
    if (pipe(toChild) != 0) { std::perror("pipe"); return 1; }
    const pid_t pid = fork();
    if (pid == 0) {
        dup2(toChild[0], STDIN_FILENO);
        close(toChild[0]); close(toChild[1]);
        execl(backend, backend, shmName, std::to_string(capFloats).c_str(), (char*)nullptr);
        std::perror("execl");
        _exit(127);
    }
    close(toChild[0]);
    ssize_t written = write(toChild[1], irText.data(), irText.size());
    (void)written;
    close(toChild[1]);

    // 4) 等映射就绪
    int fd = -1;
    for (int i = 0; i < 200; ++i) {
        fd = shm_open(shmName, O_RDWR, 0600);
        if (fd >= 0) break;
        usleep(5000);
    }
    if (fd < 0) { std::perror("shm_open"); return 1; }
    void* base = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (base == MAP_FAILED) { std::perror("mmap"); return 1; }
    auto* hdr = static_cast<vx::RingHeader*>(base);
    auto* data = reinterpret_cast<const float*>(
        static_cast<const char*>(base) + sizeof(vx::RingHeader));

    // 5) 收 PCM
    vx::RingReader rd(hdr, data);
    std::vector<float> pcm;
    std::vector<float> buf(4096);
    for (;;) {
        const size_t n = rd.read(buf.data(), buf.size());
        if (n) pcm.insert(pcm.end(), buf.begin(), buf.begin() + (long)n);
        else if (rd.closed()) break;
        else usleep(1000);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    // 必须在 munmap 之前读走需要的数据，否则就是悬垂访问
    const int sr = (int)hdr->sampleRate.load();
    munmap(base, bytes);
    close(fd);
    shm_unlink(shmName);

    vx::writeWav16(outPath, pcm, sr ? sr : 44100);
    std::printf("%s  %.2fs  后端退出码 %d\n", outPath, pcm.size() / 44100.0,
                WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    return 0;
}
