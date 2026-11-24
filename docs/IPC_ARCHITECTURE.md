# 双进程架构与双协议

## 许可划分

| 组件 | 许可 | 内容 |
|---|---|---|
| 前端 `src/frontend/` | **MIT** | UI、Lua 绑定、IR 打包、播放调度 |
| 插件（Lua/脚本） | **MIT** | 用户可自由分发 |
| 后端 `src/backend/` + `third_party/` | **AGPLv3** | IR 解析、DSP 渲染核心 |
| IR 定义 `src/ir.h` | MIT | 两端共用的契约，单独可复用 |

聚合体是「单程序双协议」：前端与后端是**两个独立进程**，
通过 IPC 通信，**不存在 DSO 调用**，因此 AGPL 不会传染到前端。

## 数据流

```
Lua 脚本 ──► 前端 C++ ──(stdin: IR 文本)──► 后端 C++
                ▲                              │
                │                              ▼
                └──(共享内存环形缓冲: PCM)──────┘
                      无锁 SPSC
```

- **IR**：纯文本行式（`src/ir.h`），前端生成、后端解析
- **PCM**：POSIX 共享内存 + 无锁 SPSC 环形缓冲（`src/ring.h`）
- **播放**：前端收到 PCM 后交给播放进程（后续接 mpv/ALSA）

## 为什么这样切

1. **许可隔离**：AGPL 只在后端，前端 MIT 可独立分发
2. **崩溃隔离**：后端渲染崩了不影响 UI
3. **无悬垂指针**：跨进程只有字节流和共享内存，没有对象生命周期纠缠
4. **可替换**：后端换成神经网络声码器时，IR 不用改

## 当前实现

- `src/ir.h`：IR 结构、序列化、解析
- `src/ring.h`：`RingWriter`/`RingReader`，基于 `std::atomic` 的 SPSC
- `src/backend/main.cpp`：读 stdin IR → Klatt 渲染 → 写 ring
- `src/frontend/main.cpp`：打包 IR → fork/exec 后端 → 收 PCM → 写 WAV

实测：4 音符 1.91 秒，后端退出码 0，前端正常收流。
