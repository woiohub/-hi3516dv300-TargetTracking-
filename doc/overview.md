# 项目总览 — Hi3516DV300端侧AI目标追踪系统

## 1. 项目概述

本项目在海思Hi3516DV300嵌入式平台上实现了一个完整的端侧实时多目标检测与追踪系统。系统利用芯片内置的NNIE（神经网络推理引擎）硬件加速单元运行YOLOv3模型，对摄像头画面中的目标进行检测和追踪，并通过Web页面实时展示结果。

**核心特点:**
- 纯C实现，无外部运行时依赖
- NNIE硬件加速推理（INT8量化）
- IoU多目标追踪算法
- 轻量级Web可视化（纯HTML/JS前端）

---

## 2. 系统架构

```
┌─────────────────────────────────────────────────────────┐
│                    硬件层                                │
│  GC2053(1080p) → MIPI → Hi3516DV300 → HDMI/网络输出     │
└─────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────┐
│                   内核模块层                              │
│  sys_config → hi_osal(MMZ) → vi/isp/vpss → nnie → venc │
└─────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────┐
│                   用户空间程序                            │
│  ┌──────────┐  ┌──────────┐  ┌─────────┐  ┌──────────┐ │
│  │视频采集   │→│NNIE推理  │→│目标追踪  │→│Web服务器  │ │
│  │VI/VPSS   │  │YOLOv3    │  │IoU匹配   │  │HTTP+JSON │ │
│  └──────────┘  └──────────┘  └─────────┘  └──────────┘ │
└─────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────┐
│                   前端展示层                              │
│  浏览器 ← HTTP轮询 ← Canvas绘制检测框 + 目标列表         │
└─────────────────────────────────────────────────────────┘
```

---

## 3. 模块职责

| 模块 | 源文件 | 职责 |
|------|--------|------|
| 主程序 | `src/main.c` | 初始化各模块、主循环调度 |
| 视频采集 | `src/video_capture.c` | GC2053传感器初始化、VPSS双通道配置、YUV→BGR转换 |
| NNIE推理 | `src/nnie_inference.c` | 模型加载、NNIE前向推理、YOLOv3后处理（anchor解码+NMS） |
| 目标追踪 | `src/tracker.c` | IoU贪心匹配、轨迹生命周期管理 |
| Web服务器 | `src/web_server.c` | HTTP服务、JSON API、前端页面分发 |
| 公共定义 | `include/common.h` | 数据结构、常量、COCO类别名 |

---

## 4. 关键技术决策

| 决策点 | 选择 | 原因 |
|--------|------|------|
| 模型 | SDK预训练YOLOv3 | 开箱即用，官方NNIE优化，80类通用检测 |
| 推理方式 | 直接调用MPI API | SDK封装函数为static，无法外部调用 |
| 追踪算法 | IoU贪心匹配 | 1fps低帧率下运动预测无意义，目标数少无需匈牙利算法 |
| Web实现 | 纯C socket | 无外部HTTP库依赖，减少部署复杂度 |
| 链接方式 | 静态链接 | 减少运行时依赖，单文件部署 |
| 帧率控制 | VPSS硬件降采样 | 从源头降低到1fps，减少CPU负载 |

---

## 5. 数据流

```
摄像头采集(30fps)
    ↓ MIPI
VI → VPSS Group
    ↓
VPSS Channel 0: 1920x1080 (预留)
VPSS Channel 1: 416x416 @ 1fps (NNIE输入)
    ↓
NV21 → BGR Planar (CPU转换)
    ↓
NNIE Forward (硬件加速)
    ↓
YOLOv3输出: 3个Blob [1,255,13,13] [1,255,26,26] [1,255,52,52]
    ↓
后处理: anchor解码 → sigmoid → confidence过滤 → NMS
    ↓
检测结果: 最多50个目标 (class, confidence, bbox)
    ↓
IoU追踪: 匹配/新建/删除轨迹
    ↓
共享数据(互斥锁保护)
    ↓
Web服务器: JSON响应 → 前端Canvas绘制
```

---

## 6. 项目文件结构

```
TargetTracking/
├── Makefile                    # 构建脚本
├── src/                        # 源代码
│   ├── main.c                  # 主程序
│   ├── video_capture.c/.h      # 视频采集
│   ├── nnie_inference.c/.h     # NNIE推理
│   ├── tracker.c/.h            # 目标追踪
│   └── web_server.c/.h         # Web服务器
├── include/
│   └── common.h                # 公共定义
├── web/
│   └── index.html              # 前端页面
├── scripts/
│   └── load_ko.sh              # 内核模块加载脚本
├── model/                      # 模型训练(离线)
│   ├── data/nnie_model/yolov3.wk
│   ├── train.py, evaluate.py, export_onnx.py
│   └── dataset/
└── doc/                        # 文档
    ├── overview.md             # 本文件
    ├── design.md               # 设计文档
    ├── learning.md             # 学习文档
    └── bugfix_mmz_ko.md        # Bug修复记录
```

---

## 7. 运行环境

### 硬件
- 开发板: EB-Hi3516DV300-DC-182
- 芯片: Hi3516DV300 (Cortex-A7 + NNIE)
- 传感器: GC2053 1080p CMOS (MIPI接口)

### 软件
- SDK: Hi3516CV500_SDK_V2.0.2.0
- 编译器: arm-himix200-linux-gcc 6.3.0
- 系统: Linux 4.9 (glibc)

### 部署流程
```
PC: make 编译
    ↓
PC: scp SDK mpp/ko/ → 开发板 /ko/
PC: scp 项目文件 → 开发板 /mnt/nfs/TargetTracking/
    ↓
开发板: sh ./scripts/load_ko.sh  (加载内核模块)
开发板: ./build/sample_target_tracking ./model/data/nnie_model/yolov3.wk 8080 ./web
    ↓
浏览器: http://<开发板IP>:8080
```

---

## 8. 性能指标

| 指标 | 值 | 说明 |
|------|-----|------|
| 推理帧率 | 1 FPS | VPSS硬件降采样控制 |
| 输入分辨率 | 416x416 | YOLOv3标准输入 |
| 检测类别 | 80类 | COCO全类别 |
| 最大检测数 | 50/帧 | 可配置 |
| 最大追踪目标 | 20个 | 可配置 |
| 可执行文件大小 | 3.1MB | 静态链接 |
| NNIE模型大小 | 61MB | INT8量化 |

---

## 9. 文档索引

| 文档 | 内容 |
|------|------|
| `doc/overview.md` | 项目总览（本文件） |
| `doc/design.md` | 设计决策与实现路径 |
| `doc/learning.md` | 详细技术解析（YOLOv3、NNIE、后处理、追踪算法等） |
| `doc/bugfix_mmz_ko.md` | Bug修复记录：mmz.ko加载失败根因分析 |
