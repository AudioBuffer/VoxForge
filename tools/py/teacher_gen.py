#!/usr/bin/env python3
"""用 DiffSinger 老师模型生成梅尔（离线蒸馏用，不进产品链路）

输入: 音符序列（音素 + 时长帧数 + 音高）
输出: mel.npy [frames,128]
"""
import sys
import numpy as np
import onnxruntime as ort

ROOT = "/root/workspace/data/ds/opencpopJPN"
HOP = 512          # 44.1kHz 下每帧约 11.6ms（DiffSinger 默认）
SR = 44100


def load_phonemes():
    with open(f"{ROOT}/phonemes.txt", encoding="utf-8") as f:
        names = [ln.strip() for ln in f if ln.strip()]
    return {n: i for i, n in enumerate(names)}


def build(tokens, durations, f0_frames):
    sess = ort.InferenceSession(f"{ROOT}/acoustic.onnx", providers=["CPUExecutionProvider"])
    tok = np.array([[PH[n] for n in tokens]], dtype=np.int64)
    dur = np.array([durations], dtype=np.int64)
    f0 = np.array([f0_frames], dtype=np.float32)
    speedup = np.array(1, dtype=np.int64)
    mel = sess.run(None, {"tokens": tok, "durations": dur, "f0": f0, "speedup": speedup})[0]
    return mel[0]


PH = load_phonemes()


def syllable_tokens(pinyin):
    """把拼音切成模型音素（近似：声母 + 韵母）"""
    out = []
    for ini in ("zh", "ch", "sh", "b", "p", "m", "f", "d", "t", "n", "l",
                "g", "k", "h", "j", "q", "x", "r", "z", "c", "s"):
        if pinyin.startswith(ini) and len(pinyin) > len(ini):
            if ini in PH:
                out.append(ini)
            pinyin = pinyin[len(ini):]
            break
    if pinyin in PH:
        out.append(pinyin)
    elif "i" in pinyin and "i" in PH:
        out.append("i")
    return out


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "/tmp/teacher.npy"
    # 小星星：yi shan yi shan liang jing jing
    notes = [("yi", 60, 0.6), ("shan", 62, 0.6), ("yi", 64, 0.6),
             ("shan", 64, 0.6), ("liang", 67, 0.6), ("jing", 67, 0.6),
             ("jing", 69, 1.0)]

    tokens, durs = [], []
    for _, _, dur in notes:
        pass
    for pinyin, _, dur in notes:
        tk = syllable_tokens(pinyin)
        if not tk:
            continue
        frames = max(4, int(dur * SR / HOP))
        per = max(2, frames // (len(tk) + 1))
        for t in tk:
            tokens.append(t)
            durs.append(per)
        tokens.append("SP")
        durs.append(max(2, frames - per * len(tk)))

    total = int(sum(durs))
    f0 = np.zeros(total, dtype=np.float32)
    pos = 0
    for (pinyin, midi, dur) in notes:
        frames = max(4, int(dur * SR / HOP))
        hz = 440.0 * 2 ** ((midi - 69) / 12)
        for i in range(frames):
            if pos + i < total:
                f0[pos + i] = hz
        pos += frames

    mel = build(tokens, durs, f0)
    np.save(out, mel)
    print(f"{out} mel {mel.shape}  tokens {len(tokens)}  frames {int(sum(durs))}")


if __name__ == "__main__":
    main()
