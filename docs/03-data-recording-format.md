# 03 · 数据记录格式与会话模型

## 1. 设计原则

Physical AI 操作数据不是普通调试日志，至少要回答五个问题：

1. 这条数据属于哪一次操作、哪台设备、哪一版固件？
2. 数据是在哪个采样时刻产生的？和其他传感器如何对齐？
3. 帧是否完整、是否被破坏、中间有没有缺失？
4. 每个字段的单位、坐标系、量程和 Schema 是什么？
5. 会话是否正常结束？掉电或断链后哪些数据可信？

本仓库实现的是 **MCU 友好的 v1 二进制帧流**：文件由若干帧直接拼接而成，第一帧建议为 `session_metadata`。边缘侧可将其转换为 MCAP/rosbag2/Parquet/JSONL。

## 2. 二进制帧格式

所有多字节整数使用小端编码。

### 2.1 通用帧头（28 字节）

| 偏移 | 长度 | 字段 | 类型 | 说明 |
|---:|---:|---|---|---|
| 0 | 1 | magic0 | u8 | 固定 `0xA5` |
| 1 | 1 | magic1 | u8 | 固定 `0x5A` |
| 2 | 1 | version | u8 | 帧协议版本，当前 `1` |
| 3 | 1 | type | u8 | 帧类型，见下表 |
| 4 | 1 | flags | u8 | 预留标志位 |
| 5 | 1 | reserved | u8 | 对齐/预留，写 0 |
| 6 | 4 | seq | u32 LE | 已入队帧序号；成功入队后递增 |
| 10 | 8 | device_ns | u64 LE | 设备本地采样时间，纳秒 |
| 18 | 8 | host_ns | u64 LE | 经时钟模型转换后的主时钟时间，纳秒 |
| 26 | 2 | payload_len | u16 LE | payload 字节数，最大 512 |
| 28 | N | payload | bytes | 类型相关数据 |
| 28+N | 2 | crc16 | u16 LE | CRC-16/CCITT-FALSE，覆盖帧头和 payload |

CRC 参数：

```text
poly=0x1021, init=0xFFFF, refin=false, refout=false, xorout=0x0000
```

校验范围是最后 2 字节 CRC 之前的全部字节。

### 2.2 帧类型

| Type | 名称 | Payload | 用途 |
|---:|---|---|---|
| `0x01` | `session_metadata` | UTF-8 JSON | 会话、设备、固件、Schema、标定和任务元数据 |
| `0x10` | `control_sample` | 控制样本 v1 | 一个控制周期的目标、实际状态、安全和耗时 |
| `0x11` | `sensor_batch` | 预留 | 触觉/EMF/IMU 等批量传感器数据 |
| `0x20` | `event` | 事件 payload | 模式切换、故障、上下电、标定、OTA 等 |
| `0x21` | `clock_sync` | 预留 | 原始同步样本、offset/drift 和同步质量 |
| `0x30` | `gap_marker` | 16 字节 GAP | 记录队列过载后的显式缺口标记 |

## 3. 控制样本 Payload（`0x10`）

### 3.1 固定头（16 字节）

| 偏移 | 长度 | 字段 | 类型 | 说明 |
|---:|---:|---|---|---|
| 0 | 1 | payload_version | u8 | 当前 `1` |
| 1 | 1 | joint_count | u8 | 关节数，1–20 |
| 2 | 1 | safety_level | u8 | 0=OK，1=WARN，2=FAULT |
| 3 | 1 | control_mode | u8 | 0=DISABLED，1=POSITION_P，2=SAFE_HOLD |
| 4 | 4 | cycle | u32 LE | 控制周期号，从 0 开始连续递增 |
| 8 | 2 | cycle_budget_us | u16 LE | 周期 deadline，微秒 |
| 10 | 2 | cycle_time_us | u16 LE | 控制路径实测耗时，微秒 |
| 12 | 4 | recorder_dropped | u32 LE | 控制记录器累计丢帧数 |

### 3.2 每个关节通道（20 字节 × joint_count）

| 偏移 | 长度 | 字段 | 类型 | 单位 |
|---:|---:|---|---|---|
| 0 | 4 | target_mrad | i32 LE | milli-radian，目标关节角 |
| 4 | 4 | position_mrad | i32 LE | milli-radian，实际关节角 |
| 8 | 4 | velocity_mrad_s | i32 LE | milli-radian/s，关节速度 |
| 12 | 4 | current_ma | i32 LE | mA，电流/力矩等效量 |
| 16 | 2 | temperature_cdeg | i16 LE | 摄氏度 × 100 |
| 18 | 2 | flags | u16 LE | 厂商状态/传感器有效位 |

20 关节时 payload 长度为：

```text
16 + 20 × 20 = 416 字节
整帧长度 = 28 + 416 + 2 = 446 字节
```

## 4. 事件与 GAP Payload

### 4.1 Event（`0x20`）

| 偏移 | 长度 | 字段 | 类型 |
|---:|---:|---|---|
| 0 | 2 | event_code | u16 LE |
| 2 | 1 | severity | u8 |
| 3 | 1 | reserved | u8 |
| 4 | 4 | cycle | u32 LE |
| 8 | N | message | UTF-8 字符串 |

建议事件码：

| Code | 含义 |
|---:|---|
| 1000 | 仿真/会话停止 |
| 1001 | 会话开始 |
| 2001 | 控制使能 |
| 2002 | 进入安全保持 |
| 3001 | 记录队列高水位 |
| 3002 | 记录 writer 错误 |
| 4001 | 时钟失步 |
| 5001 | 标定开始/完成/失败 |
| 6001 | OTA 开始/完成/回滚 |

### 4.2 GAP Marker（`0x30`，16 字节）

| 偏移 | 长度 | 字段 | 类型 | 说明 |
|---:|---:|---|---|---|
| 0 | 4 | dropped_frames | u32 LE | 截至当前的累计丢帧数 |
| 4 | 4 | pending_dropped | u32 LE | 本次 GAP 前尚未标记的丢帧数 |
| 8 | 4 | cycle | u32 LE | 最近控制周期号，未知时为 0 |
| 12 | 4 | reserved | u32 LE | 预留 |

## 5. Session Metadata 建议字段

第一帧 `session_metadata` 使用 UTF-8 JSON，示例见 [`../schemas/session-metadata.example.json`](../schemas/session-metadata.example.json)。

最小字段集：

```json
{
  "schema": "dexhand-reference-session/v1",
  "session_id": "20261124-hand-left-0001",
  "device": {
    "family": "dexhand-reference",
    "side": "left",
    "serial": "REDACTED_FOR_PUBLIC_REPO",
    "hardware_version": "prototype-rev-a",
    "firmware_version": "0.1.0-reference",
    "protocol_version": 1,
    "calibration_version": "cal-unknown"
  },
  "control": {
    "joint_count": 20,
    "nominal_period_us": 1000,
    "mode": "position_reference"
  },
  "recording": {
    "binary_frame_version": 1,
    "clock_source": "local-monotonic-reference",
    "compression": "none"
  },
  "task": {
    "label": "host_simulation",
    "operator": "optional",
    "mode": "simulation"
  }
}
```

公开 GitHub 仓库和示例日志中不要放真实设备序列号、手机号、邮箱、客户名称、密钥或未公开协议细节。

## 6. 文件与会话目录建议

产品化目录可设计为：

```text
session_20261124_183000_left/
├── manifest.json              # 会话最终清单，正常结束后原子写入
├── metadata.json              # 会话开始元数据
├── control_000.dexlog         # 二进制帧流，可分片
├── glove_tactile_000.dexlog   # 或转换为 MCAP channel
├── events_000.dexlog
├── clock_sync.jsonl
├── calibration.json           # 本次会话使用的标定版本/参数哈希
├── quality_report.json        # 帧数、缺失、CRC、队列水位、deadline
└── chunks/
    ├── 000001.dexlog
    ├── 000001.crc32
    └── ...
```

写入规则：

1. 数据先 append 到 `.tmp` 或未提交 chunk；
2. 每个 chunk 结束写长度和 CRC；
3. 所有数据落盘并校验后，最后通过临时文件 + `rename` 原子生成 `manifest.json`；
4. 掉电后没有有效 manifest 的会话标记为 `UNCLOSED`，恢复工具扫描 chunk CRC，列出可信区间；
5. 上传完成后服务端返回对象哈希，本地再更新 `upload_state`。

当前仓库只实现单文件帧流和解码器，目录/manifest 是产品化设计建议。

## 7. 带宽与记录粒度

### 7.1 本原型控制帧

20 关节、1 kHz、每帧 446 B：

```text
446 × 1000 = 446,000 B/s ≈ 435.5 KiB/s ≈ 3.57 Mbps
```

这还不包括协议封装、文件系统元数据和重传。若手内总线使用 CAN-FD，应区分：

- 实时控制 PDO：只保留闭环必需字段，追求短帧和低延迟；
- 记录数据：可由手掌控制器汇总后经独立高速链路送边缘记录器；
- 调试 trace：默认关闭或降低频率。

### 7.2 手套数据粗算

按官网公开规格做无压缩原始量估算：

| 数据流 | 计算 | 原始速率 |
|---|---|---:|
| 触觉矩阵，u16/点 | 526 × 120 × 2 B | 123.3 KiB/s |
| EMF，5 模块 × 6DOF × float32 | 5 × 6 × 4 × 120 B | 14.1 KiB/s |
| 手腕 IMU，6 轴 × i16 × 800 Hz | 6 × 2 × 800 B | 9.4 KiB/s |
| 合计 | 上述三项 | 146.7 KiB/s ≈ 1.20 Mbps |

实际 SDK 可能使用归一化 uint8、增量编码、压缩或厂商私有帧，应以真实协议为准。

## 8. 到 MCAP / rosbag2 / 训练数据的映射

| DexLog 内容 | MCAP/ROS 2 建议 Channel | 消息类型建议 |
|---|---|---|
| `session_metadata` | `/session/metadata` | `std_msgs/String` 或自定义 `SessionMetadata` |
| `control_sample` | `/dexhand/joint_states` | `sensor_msgs/msg/JointState` |
| 目标与电流 | `/dexhand/control/command` | 自定义 `JointCommand` |
| 安全状态 | `/dexhand/safety/status` | 自定义 `SafetyStatus` |
| `event` | `/dexhand/events` | `diagnostic_msgs/msg/DiagnosticEvent` 或自定义 |
| `gap_marker` | `/dexhand/recording/gap` | 自定义 `RecordingGap` |
| 手套触觉 | `/glove/tactile_matrix` | 自定义数组消息 |
| EMF | `/glove/emf_poses` | `geometry_msgs/msg/PoseArray` |
| IMU | `/glove/imu` | `sensor_msgs/msg/Imu` |
| 时钟同步 | `/clock_sync` | `sensor_msgs/msg/TimeReference` 或自定义 |

转换时必须保留：

- 原始设备时间戳和转换后主时钟时间戳；
- 原始 frame seq、control cycle 和 gap 信息；
- 单位转换公式与坐标系变换；
- 未修正/已修正数据的区别；
- Schema 版本和固件版本。

## 9. Schema 演进规则

1. 新增字段优先追加到 payload 尾部，不改变旧字段偏移；
2. 字段语义或单位变化必须提升 payload version；
3. 通用帧头不随意改长度，新增能力优先使用 `flags` 或新 frame type；
4. 解码器遇到未知 type 可跳过 payload，但必须用 `payload_len + CRC` 定位下一帧；
5. 不允许复用已废弃字段表达新含义；
6. 每个版本保留往返编解码测试和至少一个旧版本样本。

## 10. 当前解码器能力

`tools/dexlog_decode.py` 支持：

- 顺序读取拼接帧并校验 CRC；
- 解析 session metadata、control sample、event、gap marker；
- 统计帧数、类型、周期缺失、CRC 错误、GAP、最大控制耗时；
- 导出控制样本 CSV 和全帧 JSONL；
- `--strict` 模式在 CRC、序号、周期或 GAP 不完整时返回非零退出码，便于 CI 或采集后检查。

它还不支持：自动分片恢复、manifest 哈希、压缩、MCAP 写出、多文件会话和时间同步误差拟合图；这些列为后续迭代。
