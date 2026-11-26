# 09 · 参考来源与口径说明

整理日期：2026-11-24。

## 1. 舞肌科技公开产品资料

- Wuji Hand 官方页：<https://www.wuji.tech/zh/hand>
  - 引用要点：20 个主动自由度、每指 4 个关节、FOC 矢量控制、双编码器反馈、USB 2.0、重复定位精度 ±1 mm、自重与指尖力等公开规格。
- Wuji Glove 官方页：<https://www.wuji.tech/zh/glove>
  - 引用要点：526 个有效触觉点、120 FPS、5 个指尖 6DOF EMF 模块、手背 6 轴 IMU 800 Hz、RJ45 100BASE-TX、Wuji CLI/Python SDK 生态。
- 舞肌科技首页：<https://www.wuji.tech/zh/>

## 2. 行业背景参考

- 36氪：《这30家企业，撑起了灵巧手的半边天》
  - <https://36kr.com/p/4075019280406408>
  - 用于交叉核对 Wuji Hand 2 的 20 主动自由度、1000Hz 控制能力和数采手套/遥操作背景；引用 1000Hz 时应表述为“公开报道/产品资料显示”，最终以官方最新协议和实测为准。
- 36氪：《雷军投的20家机器人企业》
  - <https://36kr.com/p/4043850978367360>
  - 用于了解舞肌科技融资、直驱关节路线和 Physical AI 数据采集行业背景。

## 3. 技术概念参考方向

仓库中的机制为通用嵌入式设计实践，可按目标技术栈补充官方文档：

- CAN / CAN-FD、ISO 14229 UDS、Bootloader/OTA；
- FreeRTOS 任务优先级、队列与中断延迟；
- IEEE 1588 PTP、PPS 与多传感器时间同步；
- MCAP、rosbag2、CDR/Protobuf、sensor_msgs/JointState/Imu；
- CRC-16/CCITT-FALSE、SPSC ring、WAL/manifest 与崩溃恢复；
- ISO 26262 / IEC 61508 中安全需求、诊断和故障响应的基本概念。

## 4. 口径边界

- 本仓库是个人技术探索项目，不代表舞肌科技官方方案，也不代表任何前雇主代码或内部资料。
- 主机仿真输出只证明软件机制和编解码逻辑，不构成真实硬件性能、功能安全或量产可靠性结论。
- 公开网页规格可能随产品版本更新，引用前应再次核对官网和官方最新资料。
