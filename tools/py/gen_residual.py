#!/usr/bin/env python3
"""残差数据生成：真实音频 + 我们按同一 F0 渲染的结果 -> 残差目标

流程：
  1. librosa.pyin 提取真实 F0
  2. 用 kt_f0 按同一 F0 渲染「我们的版本」
  3. 两边算 log-mel，DTW 对齐
  4. 保存 (帧参数, 残差) 对，供训练使用

用法: gen_residual.py <音频.wav> <输出.npz>
"""
import subprocess
import sys
import numpy as np
import librosa
import soundfile as sf

SR, NFFT, HOP, MELS = 44100, 2048, 512, 128
FMIN, FMAX = 40.0, 16000.0
KT_F0 = "/tmp/kt_f0"


def logmel(y):
    m = librosa.feature.melspectrogram(y=y, sr=SR, n_fft=NFFT, hop_length=HOP,
                                       win_length=NFFT, n_mels=MELS,
                                       fmin=FMIN, fmax=FMAX, htk=False, center=True, power=2.0)
    return np.log(np.maximum(m, 1e-5)).T.astype(np.float32)     # [T,128]


def dtw_path(a, b):
    """a,b: [T,D] 的帧特征；返回对齐索引对"""
    from scipy.spatial.distance import cdist  # 可能没有 scipy，改用简化版
    raise NotImplementedError


def main():
    src, dst = sys.argv[1], sys.argv[2]
    y, sr = sf.read(src, dtype="float32")
    if y.ndim > 1:
        y = y.mean(1)
    if sr != SR:
        y = librosa.resample(y, orig_sr=sr, target_sr=SR)

    # 1) 提取 F0
    f0, voiced, _ = librosa.pyin(y, fmin=80, fmax=800, sr=SR,
                                 frame_length=2048, hop_length=HOP)
    f0 = np.nan_to_num(f0.astype(np.float32))
    print(f"F0 帧 {len(f0)}，有声比例 {(f0 > 0).mean() * 100:.0f}%")

    # 2) 我们的版本
    f0.tofile("/tmp/_f0.bin")
    hop_ms = HOP / SR * 1000.0
    subprocess.run([KT_F0, "/tmp/_ours.wav", "/tmp/_f0.bin", "a", str(hop_ms)], check=True)
    ours, _ = sf.read("/tmp/_ours.wav", dtype="float32")

    # 3) 两边的梅尔
    m_real = logmel(y)
    m_ours = logmel(ours)
    n = min(len(m_real), len(m_ours))
    m_real, m_ours = m_real[:n], m_ours[:n]

    # 4) 残差 = 真实 - 我们
    resid = (m_real - m_ours).astype(np.float32)
    np.savez_compressed(dst, mel_in=m_ours, residual=resid, f0=f0[:n])
    print(f"{dst}  帧 {n}  残差均值 {resid.mean():.3f} 标准差 {resid.std():.3f}")


if __name__ == "__main__":
    main()
