// kt_lint - DSL 校验（TUI）：逐行显示告警，并打印带注释的工程文件
//
// 用法: kt_lint <in.dsl> [-o annotated.dsl]
#include "dsl_lint.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

std::string readFile(const char* p) {
    std::string s;
    if (FILE* f = std::fopen(p, "rb")) {
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
        std::fclose(f);
    }
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "用法: kt_lint <in.dsl> [-o out.dsl]\n");
        return 2;
    }
    const std::string src = readFile(argv[1]);
    if (src.empty()) {
        std::fprintf(stderr, "读取失败: %s\n", argv[1]);
        return 1;
    }
    auto res = vx::lintDsl(src);

    const char* outPath = nullptr;
    for (int i = 2; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], "-o") == 0) outPath = argv[i + 1];
    if (outPath) {
        if (FILE* f = std::fopen(outPath, "wb")) {
            std::fwrite(res.annotated.data(), 1, res.annotated.size(), f);
            std::fclose(f);
        }
    }

    if (res.warnings.empty()) {
        std::printf("✓ 没有发现问题（%zu 行）\n", (size_t)std::count(src.begin(), src.end(), '\n') + 1);
        return 0;
    }

    std::printf("发现 %zu 条告警：\n\n", res.warnings.size());
    std::printf("%-6s %-8s %-6s %s\n", "行号", "类型", "编号", "说明");
    std::printf("----------------------------------------------\n");
    for (const auto& w : res.warnings) {
        std::printf("%-6d %-8s %-6d %s\n", w.line, w.kind.c_str(), w.itemIndex,
                    w.message.c_str());
    }
    std::printf("\n带注释的文件：\n");
    std::printf("----------------------------------------------\n");
    std::printf("%s", res.annotated.c_str());
    return res.warnings.empty() ? 0 : 3;
}
