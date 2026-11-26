# DexHand Physical AI Embedded Lab

一个围绕「灵巧手 / 数采手套」展开的个人嵌入式技术探索仓库，核心问题是：

> 如何设计灵巧手嵌入式系统，为 Physical AI 提供既能执行真实世界操作、又能把操作过程完整记录下来的能力？

本仓库不是生产可用固件，也不声称已经做过真实灵巧手硬件。它是一个**可运行的架构原型与技术验证**：用可编译的 C11 代码把“强实时控制面”和“可校验数据记录面”之间的关键取舍落地，并通过主机仿真、单元测试、二进制日志解码和故障注入验证设计想法。

## 1. 一页回答

灵巧手嵌入式系统应拆成两条既隔离又共享时间基准的链路：

1. **实时控制面：先保证手能安全、确定地动作。**
   - 手指关节节点负责 FOC、编码器/电流采样、PWM 输出和本地保护；
   - 手掌控制器以固定周期完成状态汇总、目标插值、关节指令、安全检查和总线调度；
   - 控制环内只做定长计算、非阻塞通信和无动态内存路径，不能因为写日志、刷盘、网络上传而阻塞。

2. **数据记录面：把“操作过程”变成带时间、序号、版本和校验的可回放数据。**
   - 每个控制周期生成一份不可变快照：目标、实际位置/速度/电流、安全状态、周期耗时、记录器丢帧计数；
   - 控制线程只把编码后的帧放入 SPSC 无锁环形缓冲，低优先级记录任务再写本地二进制日志 / eMMC / USB / 网络；
   - 所有帧带单调序号、设备时间、主机时间和 CRC16；控制样本另带周期号，解码时可发现 CRC 错误、周期缺失、记录背压和 GAP 标记；
   - 手套触觉阵列、EMF、IMU、灵巧手关节状态、事件、固件版本、标定参数和会话元数据使用统一会话 ID 与时钟模型关联，后续可导出为 MCAP / rosbag2 / CSV / JSONL 供 Physical AI 训练与回放。

3. **完整性不是“永不丢数据”的口号，而是可验证的分层目标。**
   - 链路层：CRC、ACK、总线错误计数；
   - 序列层：帧序号、控制周期号、GAP 标记；
   - 时间层：本地时钟 + 同步偏移/漂移模型，目标硬件用 CAN sync / PTP / PPS 校准；
   - 语义层：Schema 版本、固件版本、标定版本、单位和坐标系；
   - 存储层：先写数据块和 CRC，最后提交 manifest/成功标志；断电重启后未提交会话不被误认为完整；
   - 过载时控制优先，允许降级记录，但必须显式计数、打 GAP 标记并在数据质量报告中暴露，不能静默缺失。

## 2. 快速运行

环境：Linux/macOS，`gcc/clang`、`make`、`python3`，无第三方依赖。

```bash
make test          # C 单元测试 + Python 解码器测试
make simulate      # 生成 1200 周期主机仿真日志：examples/out/session.dexlog
make run           # 仿真并用严格模式解码，导出 CSV/JSONL
```

只解码已有日志：

```bash
python3 tools/dexlog_decode.py examples/out/session.dexlog --strict \
  --csv examples/out/session.csv --jsonl examples/out/session.jsonl
```

人为制造记录端背压：

```bash
# 300 个周期、环形缓冲 32 槽；前 120 周期不 drain，模拟写盘/USB 阻塞
./build/host_sim --cycles 300 --joints 20 --ring 32 --stall 120 \
  --out examples/out/stall.dexlog

# 严格模式会以退出码 1 报告缺失周期/GAP
python3 tools/dexlog_decode.py examples/out/stall.dexlog --strict
```

## 3. 当前原型包含什么

| 模块 | 路径 | 说明 |
|---|---|---|
| 控制环骨架 | `firmware/src/dex_control_loop.c` | 固定周期 step、平台回调注入、P 控制示例、安全状态、周期耗时统计、控制样本记录 |
| 安全监控 | `firmware/src/dex_safety.c` | 指令超时、位置限幅、过流、过温、传感器失效、deadline miss 故障位 |
| SPSC 环形缓冲 | `firmware/src/dex_spsc_ring.c` | 单生产者/单消费者定长槽队列，主机使用 C11 atomics，MCU 可替换为临界区实现 |
| 非阻塞记录器 | `firmware/src/dex_recorder.c` | 控制线程入队、低优先级 drain、满队列丢帧计数、恢复后补 GAP 标记 |
| 二进制协议 | `firmware/src/dex_protocol.c` | 28 字节帧头、小端编码、CRC16/CCITT-FALSE、控制样本/事件/GAP payload |
| 时钟同步模型 | `firmware/src/dex_clock_sync.c` | offset + ppb drift 一阶模型，预留 CAN sync/PTP/PPS 输入 |
| 主机仿真 | `examples/host_sim/main.c` | 20 关节数据流、文件 writer、1kHz 节奏模拟、`--stall` 故障注入 |
| 日志解码器 | `tools/dexlog_decode.py` | 严格校验、汇总、CSV/JSONL 导出、缺帧/GAP/CRC 报告 |
| 单元测试 | `tests/test_main.c` | CRC、协议编解码、环形队列、记录器溢出/GAP、时钟、安全、控制环 |
| 验证证据 | `validation/` | 测试输出、正常会话摘要、背压实验摘要 |

## 4. 已在云主机实际跑过的结果

> 以下是**主机功能仿真结果**，不是真实 MCU 的 WCET、Flash/RAM 占用或 EMC 测试结论。目标板指标必须用定时器打点、GPIO/示波器和最坏分支测试重新测量。

正常路径（20 关节、1200 周期、ring=128）：

```text
recorder accepted=1202 dropped=0 gaps=0 bytes=535455 backend_errors=0
control max_cycle_us=6 deadline_misses=0
frames: total=1202 crc_errors=0 seq_gaps=0
control: samples=1200 cycles=0..1199 missing_cycles=0 completeness: PASS
```

背压故障注入（300 周期、ring=32、前 120 周期不 drain）：

```text
recorder accepted=213 dropped=90 gaps=1
frames: total=213 crc_errors=0
control: samples=210 cycles=0..299 missing_cycles=90
recording_gaps: markers=1 cumulative_dropped=90
completeness: DEGRADED_OR_INCOMPLETE
strict decoder exit code = 1
```

这组实验验证的是机制：**控制线程不等待慢 writer；过载时允许丢记录帧，但恢复后留下 GAP，离线解码能把缺失周期数和累计丢帧数报告出来。**

## 5. 代码走读路线

1. 先看问题拆分：控制面、记录面、时间/元数据/质量层。
2. 再看数据流：关节节点 → 手掌控制器 → SPSC ring → 记录任务 → 本地日志/上位机/训练数据。
3. 关键取舍：为什么控制环不能直接写文件；为什么用序号、周期号、CRC、GAP 和 manifest；为什么过载时控制优先但不能静默丢数据。
4. 代码路径：从 `dex_control_loop_step()` 到 `dex_recorder_submit_control()`，再到 `dex_recorder_drain()` 和 `dexlog_decode.py`。
5. 验证证据：`make test`、正常日志 PASS、`--stall` 后严格解码失败。
6. 未解决问题：真实总线带宽、多设备硬同步、掉电原子提交、eMMC 抖动、高帧率触觉/视频数据 QoS、目标板 WCET 与功能安全边界。

## 6. 文档导航

- [`docs/01-answer-to-required-question.md`](docs/01-answer-to-required-question.md)：核心问题的完整回答与系统设计
- [`docs/02-architecture.md`](docs/02-architecture.md)：硬件/软件分层、任务优先级、控制与记录数据流
- [`docs/03-data-recording-format.md`](docs/03-data-recording-format.md)：会话模型、二进制帧、Schema、带宽估算与 MCAP 导出
- [`docs/04-validation-and-experiments.md`](docs/04-validation-and-experiments.md)：测试、主机实验、目标板验证计划与实测记录
- [`docs/05-optional-deep-dive-recorder-ring.md`](docs/05-optional-deep-dive-recorder-ring.md)：专题深挖：非阻塞记录环的设计、实验与未解决问题
- [`firmware/port/stm32/README.md`](firmware/port/stm32/README.md)：移植到真实 MCU 时需要补齐的平台层

## 7. 公开产品上下文

本仓库只引用舞肌科技官网公开规格作为需求背景：

- Wuji Hand：20 个主动自由度、每指 4 个关节、FOC 矢量控制、双编码器反馈、USB 2.0、重复定位精度 ±1 mm。
- Wuji Glove：24×31 矩阵中 526 个有效触觉点、120 FPS；5 个指尖 6DOF EMF 模块；手背 6 轴 IMU 800 Hz；RJ45 100BASE-TX；官方提供 CLI/Python SDK 方向。

来源：

- <https://www.wuji.tech/zh/hand>
- <https://www.wuji.tech/zh/glove>
- <https://www.wuji.tech/zh/>

公开规格只用于理解数据规模和产品形态；真实硬件架构、总线选型、频率分配和安全等级必须以舞肌硬件原理图、通信协议和系统需求为准。
