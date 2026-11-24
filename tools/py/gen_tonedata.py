#!/usr/bin/env python3
"""生成 G2P 训练数据：汉字（带上下文） -> 拼音（无调） + 声调

数据源：pypinyin（MIT），对常用字与常见词组取带调拼音。
多音字靠词组上下文区分（如 银行/行走）。
"""
import json, sys
from pypinyin import pinyin, Style

CHARS = []
for cp in range(0x4E00, 0x9FA6):
    CHARS.append(chr(cp))

# 常见词组（用于多音字上下文）
PHRASES = ["银行", "行走", "重庆", "重量", "音乐", "快乐", "长大", "长度",
           "还是", "归还", "空气", "空闲", "广场", "场院", "便宜", "方便"]

def tone_of(py_marked: str) -> int:
    tones = {"ā":1,"á":2,"ǎ":3,"à":4,"ē":1,"é":2,"ě":3,"è":4,
             "ī":1,"í":2,"ǐ":3,"ì":4,"ō":1,"ó":2,"ǒ":3,"ò":4,
             "ū":1,"ú":2,"ǔ":3,"ù":4,"ǖ":1,"ǘ":2,"ǚ":3,"ǜ":4}
    for ch in py_marked:
        if ch in tones: return tones[ch]
    return 5   # 轻声

def strip_tone(s: str) -> str:
    table = str.maketrans("āáǎàēéěèīíǐìōóǒòūúǔùǖǘǚǜü", "aaaaeeeeiiiioooouuuuvvvvv")
    return s.translate(table)

def main(out):
    rows = []
    # 1) 单字（上下文用自身填充）
    for ch in CHARS:
        try:
            py = pinyin(ch, style=Style.TONE, heteronym=False)
        except Exception:
            continue
        if not py or not py[0]:
            continue
        marked = py[0][0]
        rows.append({"ctx": [ch], "char": ch,
                     "syl": strip_tone(marked), "tone": tone_of(marked)})
    # 2) 词组（多音字上下文）
    for w in PHRASES:
        try:
            pys = pinyin(w, style=Style.TONE, heteronym=False)
        except Exception:
            continue
        for i, ch in enumerate(w):
            if i >= len(pys) or not pys[i]:
                continue
            marked = pys[i][0]
            rows.append({"ctx": list(w), "char": ch, "pos": i,
                         "syl": strip_tone(marked), "tone": tone_of(marked)})
    json.dump(rows, open(out, "w"), ensure_ascii=False)
    print(f"{len(rows)} 条 -> {out}")

if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "data/tonedata.json")
