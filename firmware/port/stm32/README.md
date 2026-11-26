# STM32 / ARM Cortex-M 移植说明

本目录不参与主机 Makefile 编译，只给出把参考核心移植到真实 MCU 时的边界。

## 1. 不需要重写的部分

`firmware/src` 中以下模块尽量保持平台无关：

- `dex_crc16.c`：CRC-16/CCITT-FALSE；
- `dex_protocol.c`：帧与控制样本显式编解码；
- `dex_spsc_ring.c`：C11 atomics 版 SPSC 队列；
- `dex_recorder.c`：非阻塞提交、drain、丢帧和 GAP；
- `dex_clock_sync.c`：offset/drift 时钟模型；
- `dex_safety.c`：参考安全检查；
- `dex_control_loop.c`：周期控制骨架。

如果目标工具链不支持 `<stdatomic.h>`，可新增 `dex_spsc_ring_baremetal.c`，保持头文件 API 不变，用以下方式保护索引更新：

- 生产者/消费者位于不同中断优先级时，对 head/tail 的读改写进入最短关中断窗口；
- 若只有生产者写 `tail`、消费者写 `head`，可将索引声明为 `volatile` 并在更新前后加 `__DMB()`；
- 无论采用哪种方式，都要在目标板上做长时间并发压测和反汇编检查。

## 2. 必须由平台实现的部分

### 2.1 时间

```c
uint64_t now_ns(void *user_ctx);
```

建议使用：

- SysTick/HAL tick 提供秒级基准；
- 32 位/64 位硬件定时器提供微秒或纳秒计数；
- 控制环入口和出口分别打点，用于 WCET 与周期统计；
- 同步捕获寄存器记录 CAN sync/PPS 到达时刻。

### 2.2 关节状态读取

```c
int read_joints(DexJointSample *joints, uint8_t joint_count, void *user_ctx);
```

真实实现应从 CAN-FD/UART/SPI 电机总线、编码器或 ADC+DMA 缓冲读取，并填充：

- 实际关节角和速度；
- 电流/温度；
- 传感器有效位、总线错误和时间戳；
- 无效帧不得伪装成 0，应通过返回码或 `flags` 标记。

### 2.3 控制指令下发

```c
int apply_command(const DexControlCommand *command, void *user_ctx);
```

参考原型只计算示例电流。产品实现应转换为电机模组协议或本地 FOC 输入，并处理：

- 总线超时与 ACK；
- 关节使能/失能；
- 力矩/电流/速度/位置模式；
- 安全状态下的零力、保持或安全位置策略。

### 2.4 记录 Writer

`dex_recorder_drain()` 的回调原型：

```c
int (*DexRecordWriter)(const uint8_t *frame, size_t len, void *user_ctx);
```

可接到：

- SPI/QSPI Flash 或 eMMC 文件；
- USB CDC/Bulk；
- UART/CAN 诊断口（带宽有限，不适合高频全量数据）；
- Ethernet/TCP/UDP；
- MCAP/rosbag2 writer（通常放在 MPU/上位机更合适）。

Writer 运行在低优先级任务，允许批量写；控制任务只调用 `dex_recorder_submit_*()`。

## 3. 示例任务划分

```text
timer control task (high priority, 1 kHz)
  ├─ read_joints()
  ├─ dex_control_loop_step()
  └─ recorder submit (non-blocking)

glove/sensor acquisition task (medium-high priority)
  ├─ parse tactile/EMF/IMU frames
  └─ submit sensor batches

recorder writer task (medium-low priority)
  └─ dex_recorder_drain(..., flash_or_usb_writer, ...)

diagnostic/OTA task (low priority)
  ├─ UDS/CAN/USB diagnostics
  └─ firmware update and manifest management
```

## 4. 目标板 Bring-up 顺序

1. 先跑通时钟、GPIO、定时器和串口日志；
2. 移植 CRC/protocol 单测到板端，验证字节序和对齐；
3. 用 RAM ring + UART/USB writer 跑通控制样本帧到 PC 解码器；
4. 接入一路真实关节/传感器，再扩展到 20 路；
5. 测量 submit、控制 step、writer drain 的周期和栈高水位；
6. 注入总线错误、队列满、writer 断开和掉电；
7. 接入 Flash/文件系统与会话 manifest；
8. 最后做长时间台架、温度、总线负载和安全故障验证。

## 5. 模板文件

[`stm32_platform_template.c`](stm32_platform_template.c) 给出非编译型集成骨架。真实工程应替换 HAL 头文件、总线协议和存储 writer，并加入芯片厂商启动文件、链接脚本和 HAL 源文件。
