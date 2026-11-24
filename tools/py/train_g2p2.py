#!/usr/bin/env python3
"""G2P 网络（嵌入表版）：上下文汉字 -> 拼音类别 + 声调

与旧版的区别：用可学习的 char embedding 表，而不是哈希二进制特征。
小词表（训练集出现的字）足够，模型几 MB。
导出 VXGP2：词表 + 嵌入 + MLP 权重，供 C++ 推理。
"""
import json, struct, sys
import numpy as np

EMB, WIN, H1, H2 = 32, 5, 192, 192

def main(data_json, out_bin):
    rows = json.load(open(data_json))
    chars = sorted({c for r in rows for c in r["ctx"]} | {r["char"] for r in rows})
    cid = {c: i + 1 for i, c in enumerate(chars)}      # 0 保留给 padding
    syls = sorted({r["syl"] for r in rows})
    sid = {s: i for i, s in enumerate(syls)}
    print(f"{len(rows)} 条 | 字 {len(chars)} | 音节 {len(syls)}")

    # 留出 10% 做验证，别报训练集准确率糊弄自己
    rng_split = np.random.default_rng(7)
    order = rng_split.permutation(len(rows))
    val_idx = set(order[:len(rows)//10].tolist())
    train_rows = [r for i, r in enumerate(rows) if i not in val_idx]
    val_rows = [rows[i] for i in sorted(val_idx)]
    rows = train_rows
    print(f"训练 {len(train_rows)} | 验证 {len(val_rows)}")
    N = len(rows)
    X = np.zeros((N, WIN), np.int32)
    Ys = np.zeros(N, np.int32)
    Yt = np.zeros(N, np.int32)
    for i, r in enumerate(rows):
        ctx, pos = r["ctx"], r.get("pos", len(r["ctx"]) // 2)
        for k in range(WIN):
            j = pos + k - WIN // 2
            c = ctx[j] if 0 <= j < len(ctx) else ctx[pos]
            X[i, k] = cid.get(c, 0)
        Ys[i] = sid[r["syl"]]
        Yt[i] = max(0, min(4, r["tone"] - 1))

    rng = np.random.default_rng(0)
    V = len(chars) + 1
    E = (rng.normal(0, 0.1, (V, EMB))).astype(np.float32)
    din = EMB * WIN
    W1 = rng.normal(0, np.sqrt(2 / din), (din, H1)).astype(np.float32)
    b1 = np.zeros(H1, np.float32)
    W2 = rng.normal(0, np.sqrt(2 / H1), (H1, H2)).astype(np.float32)
    b2 = np.zeros(H2, np.float32)
    Ws = (rng.normal(0, np.sqrt(1 / H2), (H2, len(syls))) * 0.01).astype(np.float32)
    bs = np.zeros(len(syls), np.float32)
    Wt = (rng.normal(0, np.sqrt(1 / H2), (H2, 5)) * 0.01).astype(np.float32)
    bt = np.zeros(5, np.float32)

    params = [E, W1, b1, W2, b2, Ws, bs, Wt, bt]
    m = [np.zeros_like(p) for p in params]
    v = [np.zeros_like(p) for p in params]
    lr, b1m, b2m, eps = 3e-3, 0.9, 0.999, 1e-8

    B = 256
    for ep in range(60):
        perm = rng.permutation(N)
        tot = 0.0; hitS = 0; hitT = 0; seen = 0
        for i in range(0, N - B, B):
            idx = perm[i:i + B]
            xb = X[idx]                       # (B, WIN)
            x = E[xb].reshape(B, -1)          # (B, EMB*WIN)
            z1 = x @ W1 + b1; a1 = np.maximum(z1, 0)
            z2 = a1 @ W2 + b2; a2 = np.maximum(z2, 0)
            ls = a2 @ Ws + bs; lt = a2 @ Wt + bt
            ys, yt = Ys[idx], Yt[idx]
            ps = np.exp(ls - ls.max(1, keepdims=True)); ps /= ps.sum(1, keepdims=True)
            pt = np.exp(lt - lt.max(1, keepdims=True)); pt /= pt.sum(1, keepdims=True)
            loss = -np.log(ps[np.arange(B), ys] + 1e-9).mean() - np.log(pt[np.arange(B), yt] + 1e-9).mean()
            tot += loss
            hitS += (ps.argmax(1) == ys).sum(); hitT += (pt.argmax(1) == yt).sum(); seen += B

            ds = ps.copy(); ds[np.arange(B), ys] -= 1; ds /= B
            dt = pt.copy(); dt[np.arange(B), yt] -= 1; dt /= B
            gWs = a2.T @ ds; gbs = ds.sum(0)
            gWt = a2.T @ dt; gbt = dt.sum(0)
            da2 = ds @ Ws.T + dt @ Wt.T
            dz2 = da2 * (z2 > 0)
            gW2 = a1.T @ dz2; gb2 = dz2.sum(0)
            da1 = dz2 @ W2.T
            dz1 = da1 * (z1 > 0)
            gW1 = x.T @ dz1; gb1 = dz1.sum(0)
            gx = dz1 @ W1.T                   # (B, EMB*WIN)
            gx = gx.reshape(B, WIN, EMB)
            gE = np.zeros_like(E)
            np.add.at(gE, xb, gx)             # 稀疏累加到被用到的行

            grads = [gE, gW1, gb1, gW2, gb2, gWs, gbs, gWt, gbt]
            for j, (p, g) in enumerate(zip(params, grads)):
                m[j] = b1m * m[j] + (1 - b1m) * g
                v[j] = b2m * v[j] + (1 - b2m) * (g * g)
                mh = m[j] / (1 - b1m ** (ep + 1))
                vh = v[j] / (1 - b2m ** (ep + 1))
                p -= lr * mh / (np.sqrt(vh) + eps)
        if ep % 5 == 0 or ep == 59:
            print(f"ep {ep+1}: loss {tot/max(1,(N-B)//B):.3f} 音节 {100*hitS/max(1,seen):.1f}% 声调 {100*hitT/max(1,seen):.1f}%")

    # 验证集准确率
    def eval_rows(rs):
        Xv = np.zeros((len(rs), WIN), np.int32)
        for i, r in enumerate(rs):
            ctx, pos = r["ctx"], r.get("pos", len(r["ctx"]) // 2)
            for k in range(WIN):
                j = pos + k - WIN // 2
                c = ctx[j] if 0 <= j < len(ctx) else ctx[pos]
                Xv[i, k] = cid.get(c, 0)
        x = E[Xv].reshape(len(rs), -1)
        a1 = np.maximum(x @ W1 + b1, 0)
        a2 = np.maximum(a1 @ W2 + b2, 0)
        okS = okT = 0
        for i, r in enumerate(rs):
            s_pred = syls[int(np.argmax(a2[i] @ Ws + bs))]
            t_pred = int(np.argmax(a2[i] @ Wt + bt)) + 1
            if s_pred == r["syl"]: okS += 1
            if t_pred == r["tone"]: okT += 1
        return 100.0*okS/len(rs), 100.0*okT/len(rs)
    vs, vt = eval_rows(val_rows)
    print(f"验证集：音节 {vs:.2f}%  声调 {vt:.2f}%")

    with open(out_bin, "wb") as f:
        f.write(b"VXG2")
        f.write(struct.pack("<4i", EMB, WIN, H1, H2))
        f.write(struct.pack("<3i", len(chars), len(syls), 5))
        for c in chars:
            b = c.encode(); f.write(struct.pack("<B", len(b))); f.write(b)
        for s in syls:
            b = s.encode(); f.write(struct.pack("<B", len(b))); f.write(b)
        for p in params:
            f.write(np.ascontiguousarray(p, dtype="<f4").tobytes())
    print("saved", out_bin)

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
