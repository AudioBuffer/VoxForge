// mkdict - 把 tonedata.json 转成紧凑字典（C++ 手写扫描，不依赖 JSON 库）
// 输出格式：每行 "<汉字>\t<拼音>\t<声调>"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

int main(int argc, char** argv) {
    const char* in = (argc > 1) ? argv[1] : "data/tonedata.json";
    const char* out = (argc > 2) ? argv[2] : "data/g2p_dict.txt";
    std::ifstream f(in);
    if (!f) { std::fprintf(stderr, "打不开 %s\n", in); return 1; }
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    FILE* o = std::fopen(out, "wb");
    if (!o) { std::perror("fopen"); return 1; }

    size_t pos = 0, n = 0;
    while ((pos = s.find("\"char\": \"", pos)) != std::string::npos) {
        pos += 9;
        const size_t e1 = s.find('"', pos);
        if (e1 == std::string::npos) break;
        const std::string ch = s.substr(pos, e1 - pos);

        const size_t sp = s.find("\"syl\": \"", e1);
        if (sp == std::string::npos) break;
        const size_t s2 = sp + 8;
        const size_t e2 = s.find('"', s2);
        if (e2 == std::string::npos) break;
        const std::string syl = s.substr(s2, e2 - s2);

        const size_t tp = s.find("\"tone\": ", e2);
        if (tp == std::string::npos) break;
        const int tone = std::atoi(s.c_str() + tp + 8);

        std::fprintf(o, "%s\t%s\t%d\n", ch.c_str(), syl.c_str(), tone);
        ++n;
        pos = e2;
    }
    std::fclose(o);
    std::printf("%zu 条 -> %s\n", n, out);
    return 0;
}
