#!/usr/bin/env python3
"""Qwen3-ASR 评测（官方 qwen_asr 接口）

用法: python3 tools/asr_qwen.py <wav> [期望文本]
"""
import sys
import torch
from qwen_asr import Qwen3ASRModel

NAME = "Qwen/Qwen3-ASR-0.6B"


def main() -> None:
    path = sys.argv[1]
    want = sys.argv[2] if len(sys.argv) > 2 else ""
    model = Qwen3ASRModel.from_pretrained(
        NAME,
        dtype=torch.bfloat16,
        device_map="cpu",
        max_new_tokens=128,
    )
    results = model.transcribe(audio=path, language="Chinese")
    got = results[0].text.strip()
    print(f"期望: {want}")
    print(f"听到: {got}")
    if want:
        hit = sum(1 for c in want if c in got) / max(1, len(want))
        print(f"字符命中率: {hit * 100:.0f}%")


if __name__ == "__main__":
    main()
