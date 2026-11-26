# 04 · 验证计划与已完成实验

## 1. 验证原则

本项目不追求生产可用方案，但仓库仍要体现工程闭环：

1. 能运行：核心 C 代码在主机编译，不依赖特殊硬件；
2. 能测试：协议、队列、记录器、安全和控制环有自动化测试；
3. 能观察：生成真实二进制日志，而不是只画架构图；
4. 能证伪：通过故障注入看到机制失效时的行为和离线报告；
5. 诚实标注边界：主机结果不能替代 MCU WCET、总线、存储、EMC 和功能安全验证。

## 2. 已完成的主机侧验证

运行环境：Ubuntu 22.04 云主机，gcc 11.4.0，C11，Python 3.12。

### 2.1 自动化测试

命令：

```bash
make clean && make test
```

结果摘要（完整输出见 [`../validation/test-output.txt`](../validation/test-output.txt)）：

```text
dexhand tests: ALL PASSED
Ran 3 tests in 0.001s
OK
```

C 测试覆盖：

- CRC-16/CCITT-FALSE 标准向量 `123456789 → 0x29B1`；
- 二进制帧编码/解码往返；
- payload 被篡改后 CRC 校验失败；
- 控制样本 payload 编解码；
- SPSC ring FIFO、空/满行为；
- recorder drain、队列溢出、丢帧计数和 GAP marker；
- 时钟同步首样本偏移转换；
- 过流与指令超时安全故障；
- 控制环初始化、目标下发、P 控制输出限幅。

Python 测试覆盖：

- CRC 标准向量；
- 控制样本 payload 解析；
- 临时 `.dexlog` 文件帧读取与 summary。

### 2.2 正常 1200 周期主机会话

命令：

```bash
make all
./build/host_sim --cycles 1200 --joints 20 \
  --out examples/out/session.dexlog
python3 tools/dexlog_decode.py examples/out/session.dexlog --strict \
  --csv examples/out/session.csv --jsonl examples/out/session.jsonl
```

仿真器输出（[`../validation/default-simulator.txt`](../validation/default-simulator.txt)）：

```text
cycles=1200 joints=20 ring=128 stall=0
recorder accepted=1202 dropped=0 gaps=0 pending=0 bytes=535455 backend_errors=0
control max_cycle_us=6 deadline_misses=0 avg_cycle_ns=309
```

解码器严格模式输出（[`../validation/default-summary.txt`](../validation/default-summary.txt)）：

```text
frames: total=1202 crc_errors=0 seq_gaps=0
frame_counts: control_sample=1200, event=1, session_metadata=1
control: samples=1200 cycles=0..1199 missing_cycles=0 max_cycle_time_us=6
recording_gaps: markers=0 cumulative_dropped=0
completeness: PASS
```

解释：

- `accepted=1202`：1200 个控制帧 + 1 个会话元数据帧 + 1 个停止事件帧；
- `bytes=535455`：实际写出的完整二进制帧总字节数，平均约 445.5 B/帧，与 446 B 设计值一致；
- `max_cycle_us=6` 是**云主机用户态逻辑耗时**，只说明代码路径可运行，不能写到 MCU 指标里；
- 仿真器用 `nanosleep(1 ms)` 逼近 1 kHz 调度节奏，但不是硬实时调度。

### 2.3 记录端停摆 / 队列溢出实验

实验目的：验证慢 writer 或存储卡顿不会让控制线程阻塞，同时离线工具能发现数据缺口。

命令：

```bash
./build/host_sim --cycles 300 --joints 20 --ring 32 --stall 120 \
  --out examples/out/stall.dexlog
python3 tools/dexlog_decode.py examples/out/stall.dexlog --strict
```

仿真器输出（[`../validation/stall-simulator.txt`](../validation/stall-simulator.txt)）：

```text
cycles=300 joints=20 ring=32 stall=120
recorder accepted=213 dropped=90 gaps=1 pending=0 bytes=93961 backend_errors=0
control max_cycle_us=1 deadline_misses=0 avg_cycle_ns=363
```

解码器输出（[`../validation/stall-summary.txt`](../validation/stall-summary.txt)）：

```text
frames: total=213 crc_errors=0
frame_counts: control_sample=210, event=1, gap_marker=1, session_metadata=1
control: samples=210 cycles=0..299 missing_cycles=90
recording_gaps: markers=1 cumulative_dropped=90
completeness: DEGRADED_OR_INCOMPLETE
stall_decoder_exit_code=1
```

实验结论：

1. 前 120 个周期不调用 `dex_recorder_drain()`，32 槽 ring 被填满；
2. ring 满后控制线程的 submit 立即返回满队列错误并增加 `dropped_frames`，没有等待 writer；
3. writer 恢复后，记录器先尝试写入一个 GAP marker，再写后续控制帧；
4. 因为被丢弃的是尚未编码/入队的控制周期，文件内已存储帧的 `seq` 可以连续，但控制周期号出现空洞；解码器用 `cycle` 检测出 90 个缺失周期；
5. `--strict` 对 GAP/缺失返回退出码 1，可用于采集结束后的 CI 或产线/台架数据质量检查。

这个实验的局限：它只模拟“消费者不 drain”，没有模拟真实 eMMC 延迟、USB 断连、网络重传、掉电和多线程竞争；这些在目标环境验证计划中列出。

## 3. 目标板验证矩阵（按需求评审展开）

| 类别 | 测试项 | 方法 | 通过判据（候选，需按需求评审） |
|---|---|---|---|
| 实时性 | 控制环 WCET | 定时器计数 + GPIO 翻转 + 示波器/逻辑分析仪，覆盖最坏分支、总线错误、CRC 失败、队列满 | 小于分配 deadline 并保留 ≥2× 裕量；具体阈值由系统周期决定 |
| 调度 | 中断延迟/抢占 | 在高优先级 ISR、控制任务、writer 任务同时运行时测量抖动 | 无控制周期丢失；deadline miss 计数为 0 或进入明确降级 |
| 内存 | 栈/堆/队列峰值 | 静态 map、栈染色、高水位统计、长时间压力测试 | 无栈溢出、无泄漏、队列水位可解释 |
| 总线 | CAN-FD/USB/Ethernet 峰值 | 真实多关节 + 手套数据流抓包，注入错误帧/重传 | 总线负载、延迟和错误率满足协议设计裕量 |
| 记录 | 正常写入完整性 | 1 kHz 控制 + 传感器流长时间运行，解码器严格校验 | CRC 错误 0、周期缺失 0、manifest 完整 |
| 记录 | 慢存储/队列满 | 注入文件系统 flush 延迟、放慢 writer、填满 ring | 控制环不阻塞；丢帧/GAP/质量报告正确 |
| 记录 | 掉电恢复 | 写入过程中随机断电，重启扫描 chunk/manifest | 已提交 chunk CRC 正确；未提交会话明确标记，不伪造完整 |
| 通信 | CRC/丢帧/重传 | SPI/CAN/UART 错误注入，拔插线缆 | 坏帧丢弃且有错误码；无界重试不进入实时路径 |
| 安全 | 指令超时 | 停止上位机/遥操作指令 | 按设计进入零力/保持/安全位置并记录事件 |
| 安全 | 过流/过温/越限 | 台架信号注入或限流/温度模拟 | 故障位、安全输出和恢复锁存符合设计 |
| 同步 | 多设备时间对齐 | 共同 GPIO/PPS 事件 + 总线 sync 时间戳 | 报告 P50/P95/P99 偏差和失步事件 |
| 数据 | 训练格式转换 | DexLog → CSV/JSONL/MCAP/rosbag2 样例 | 时间、单位、坐标系、序号和 GAP 不丢失 |
| 稳定性 | 长稳与温度 | 连续抓取/遥操作/记录，监控温度、存储、丢帧 | 无复位、无静默数据缺口、无存储写满未告警 |
| OTA | 升级/回滚 | A/B 分区升级、升级中断电、版本校验 | 可启动、可回滚、版本和事件可追溯 |

## 4. 建议补充的自动化 CI

当前仓库可在 GitHub Actions 中加入：

```yaml
# 候选 .github/workflows/ci.yml
jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - run: make test
      - run: make simulate
      - run: python3 tools/dexlog_decode.py examples/out/session.dexlog --strict
```

注意：`make simulate` 当前会 `nanosleep` 约 1.2 秒，CI 成本很低；生成的 `*.dexlog` 已在 `.gitignore` 中，不把二进制日志提交进 Git，验证摘要文本放入 `validation/`。

## 5. 如何判断结果是否达到预期

### 5.1 原型阶段的“达到预期”

- clone 后两条命令即可运行测试和仿真；
- 正常日志可被独立 Python 工具解码，周期连续、CRC 通过；
- 故障注入能复现显式缺口，而不是只在口头说“不会丢”；
- 文档能区分已实现机制、设计建议和未验证假设；
- 能从代码讲到系统取舍，再讲到真实硬件验证计划。

### 5.2 产品阶段的“达到预期”

必须由真实需求定义指标，例如：

- 20 轴控制周期、抖动和 WCET 裕量；
- 关节状态/触觉/IMU 的端到端延迟；
- 数据会话完整率、丢帧率和可恢复率；
- 多设备同步误差 P95/P99；
- 存储寿命、容量和极端卡顿下的降级行为；
- 安全故障响应时间和误触发率；
- 台架、车辆/机械臂联调、现场试用和量产一致性问题闭环。

## 6. 已知未完成项

- 未实现真实 CAN-FD/USB/Ethernet 驱动和关节协议；
- 未实现 MCAP/rosbag2 writer，只提供 CSV/JSONL 调试导出；
- 未实现 manifest、chunk CRC、断点续传和掉电恢复代码；
- 未实现多线程 SPSC 压力测试和内存屏障的目标板验证；
- 未测量真实 MCU 的 Flash/RAM/栈占用与 WCET；
- 未接入真实手套数据格式；当前手套带宽仅为公开规格上的估算。
