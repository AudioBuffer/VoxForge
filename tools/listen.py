#!/usr/bin/env python3
"""本地 Whisper 听合成结果，和输入文本比对（可懂度评估）"""
import sys, numpy as np, wave, torch
from transformers import AutoProcessor, AutoModelForSpeechSeq2Seq
NAME = "openai/whisper-small"
proc = AutoProcessor.from_pretrained(NAME)
model = AutoModelForSpeechSeq2Seq.from_pretrained(NAME).eval()

def transcribe(path):
    w = wave.open(path, 'rb'); sr = w.getframerate(); n = w.getnframes()
    x = np.frombuffer(w.readframes(n), dtype='<i2').astype(np.float32) / 32768
    w.close()
    if sr != 16000:
        idx = np.linspace(0, len(x) - 1, int(len(x) * 16000 / sr))
        x = np.interp(idx, np.arange(len(x)), x).astype(np.float32)
    feats = proc(x, sampling_rate=16000, return_tensors="pt").input_features
    with torch.no_grad():
        ids = model.generate(feats, language="zh", task="transcribe", max_new_tokens=48)
    return proc.batch_decode(ids, skip_special_tokens=True)[0]

if __name__ == "__main__":
    path = sys.argv[1]
    want = sys.argv[2] if len(sys.argv) > 2 else ""
    got = transcribe(path)
    print(f"期望: {want}")
    print(f"听到: {got}")
    if want:
        hit = sum(1 for c in want if c in got) / max(1, len(want))
        print(f"字符命中率: {hit*100:.0f}%")
