# 项目学习文档 — 详细技术解析

## 1. YOLOv3模型

### 1.1 模型概述

本项目使用海思SDK提供的YOLOv3预训练模型 (`inst_yolov3_cycle.wk`)，该模型基于COCO数据集训练，支持80类目标检测。

| 特性 | 说明 |
|------|------|
| 模型大小 | 61MB (.wk) |
| 输入尺寸 | 416x416x3 BGR |
| 检测类别 | 80类 (COCO) |
| 检测尺度 | 3个 (13x13, 26x26, 52x52) |
| 量化方式 | INT8 (NNIE硬件加速) |

### 1.2 YOLOv3网络结构

```
输入: 416x416x3 BGR
     |
Backbone: Darknet-53
  - 53层卷积网络
  - 残差连接(Residual blocks)
  - 多尺度特征提取
     |
Neck: FPN (特征金字塔)
  - 3个尺度的特征融合
  - 上采样 + 拼接
     |
Head: 3个检测头
  - Scale 0: 13x13 (大目标)
  - Scale 1: 26x26 (中目标)
  - Scale 2: 52x52 (小目标)
     |
输出: 3个Blob
  - [1, 255, 13, 13]
  - [1, 255, 26, 26]
  - [1, 255, 52, 52]
  - 255 = 3 anchors * (5 + 80 classes)
```

### 1.3 Anchor机制

YOLOv3使用预定义的anchor box进行目标检测：

```c
/* 3个尺度的anchor尺寸(基于COCO数据集聚类) */
Scale 0 (13x13): {116,90}, {156,198}, {373,326}  // 大目标
Scale 1 (26x26): {30,61},  {62,45},  {59,119}    // 中目标
Scale 2 (52x52): {10,13},  {16,30},  {33,23}     // 小目标
```

每个grid cell预测3个anchor box，每个anchor包含：
- `tx, ty`: 中心点偏移(经sigmoid映射到0~1)
- `tw, th`: 宽高缩放(经exp映射)
- `objectness`: 目标置信度
- `class1~class80`: 80个类别分数

### 1.4 输出解码公式

```
box_cx = (sigmoid(tx) + grid_x) * (input_w / grid_w)
box_cy = (sigmoid(ty) + grid_y) * (input_h / grid_h)
box_w  = exp(tw) * anchor_w
box_h  = exp(th) * anchor_h
score  = sigmoid(objectness) * sigmoid(class_score)
```

---

## 2. 海思NNIE引擎

### 2.1 什么是NNIE

NNIE（Neural Network Inference Engine）是海思媒体芯片内置的神经网络推理硬件加速单元。

**Hi3516DV300的NNIE特性:**
- 支持CNN推理，INT8量化
- 单核NNIE（SVP_NNIE_ID_0）
- 支持多段网络（Multi-Segment）

### 2.2 NNIE推理流程

```
1. 模型准备: .wk文件(海思Model Conversion Tool转换)
2. 模型加载: HI_MPI_SVP_NNIE_LoadModel()
3. 参数初始化: 分配Task Buffer、输入输出Blob
4. 填充输入: BGR图像拷贝到输入Blob
5. 前向推理: HI_MPI_SVP_NNIE_Forward()
6. 查询结果: HI_MPI_SVP_NNIE_Query()
7. 后处理: anchor解码 + sigmoid + NMS
```

### 2.3 关键API

| API | 作用 |
|-----|------|
| `HI_MPI_SVP_NNIE_LoadModel` | 从内存加载.wk模型 |
| `HI_MPI_SVP_NNIE_Forward` | 执行前向推理 |
| `HI_MPI_SVP_NNIE_Query` | 查询推理完成状态 |
| `HI_MPI_SVP_NNIE_UnloadModel` | 卸载模型释放资源 |

### 2.4 内存管理

NNIE使用MMZ（Media Memory Zone）内存：
- 通过`SAMPLE_COMM_SVP_MallocMem()`分配物理连续内存
- NNIE使用物理地址，CPU使用虚拟地址
- 操作前后需要刷新缓存

---

## 3. YOLOv3后处理

### 3.1 NNIE输出格式

YOLOv3有3个输出Blob，对应3个检测尺度：

```
Blob 0: [1, 255, 13, 13]  -> Scale 0 (大目标)
Blob 1: [1, 255, 26, 26]  -> Scale 1 (中目标)
Blob 2: [1, 255, 52, 52]  -> Scale 2 (小目标)

255 = 3 anchors * 85 (5 + 80 classes)
数据布局: CHW (channel-first)
```

### 3.2 后处理流程

```c
// 1. 遍历3个尺度
for (s = 0; s < 3; s++) {
    // 2. 遍历grid cell
    for (h = 0; h < grid_h; h++) {
        for (w = 0; w < grid_w; w++) {
            // 3. 遍历3个anchor
            for (a = 0; a < 3; a++) {
                // 4. 读取anchor输出值
                tx = data[anchor_offset + 0 * stride];
                ty = data[anchor_offset + 1 * stride];
                tw = data[anchor_offset + 2 * stride];
                th = data[anchor_offset + 3 * stride];
                obj = data[anchor_offset + 4 * stride];

                // 5. objectness过滤
                if (sigmoid(obj) < 0.5) continue;

                // 6. 找最大类别
                for (c = 0; c < 80; c++) {
                    class_score = sigmoid(data[anchor_offset + (5+c) * stride]);
                    // 记录最大值
                }

                // 7. 最终置信度
                score = sigmoid(obj) * max_class_score;
                if (score < 0.5) continue;

                // 8. Anchor解码
                cx = (sigmoid(tx) + w) * (416 / grid_w);
                cy = (sigmoid(ty) + h) * (416 / grid_h);
                bw = exp(tw) * anchor_w;
                bh = exp(th) * anchor_h;
            }
        }
    }
}

// 9. NMS(非极大值抑制)
for (i = 0; i < num; i++) {
    for (j = i+1; j < num; j++) {
        if (same_class && iou(box[i], box[j]) > 0.45) {
            box[j].mask = 1;  // 抑制
        }
    }
}
```

### 3.3 数据偏移计算

NNIE输出Blob的内存布局为CHW格式，带stride对齐：

```
偏移 = anchor_idx * det_dim * grid_h * stride
     + h * stride
     + w

其中:
- anchor_idx: 0~2 (3个anchor)
- det_dim: 85 (5 + 80 classes)
- grid_h: 13/26/52 (网格高度)
- stride: grid_w 按32字节对齐后的值
```

---

## 4. 视频处理流程

### 4.1 海思MPI架构

```
GC2053传感器 -> MIPI RX -> VI -> VPSS -> VENC/VO
                                |
                           通道0: 1920x1080 (显示)
                           通道1: 416x416 (NNIE推理, 1fps)
```

### 4.2 VPSS配置

```c
// VPSS通道1: NNIE推理用
astVpssChnAttr[1].u32Width = 416;
astVpssChnAttr[1].u32Height = 416;
astVpssChnAttr[1].stFrameRate.s32DstFrameRate = 1;  // 1FPS
```

### 4.3 YUV到BGR转换

GC2053输出NV21格式，NNIE需要BGR Planar：

```
NV21: YYYYYYYY VUVU (Y全量, UV交错)
BGR:  BBBB... GGGG... RRRR... (三通道分离)

转换公式(ITU-R BT.601):
  R = Y + 1.402 * (V - 128)
  G = Y - 0.344136 * (U - 128) - 0.714136 * (V - 128)
  B = Y + 1.772 * (U - 128)
```

使用整数近似实现，避免浮点运算：

```c
R = Y + ((359 * (V - 128)) >> 8)
G = Y - ((88 * (U - 128) + 183 * (V - 128)) >> 8)
B = Y + ((454 * (U - 128)) >> 8)
```

---

## 5. 目标追踪算法

### 5.1 IoU匹配

```
IoU = 交集面积 / 并集面积
    = |A ∩ B| / (|A| + |B| - |A ∩ B|)
```

### 5.2 追踪流程

```
1. 计算IoU矩阵(每个轨迹 vs 每个检测)
2. 贪心匹配(优先高IoU, 阈值0.3)
3. 匹配成功: 更新轨迹位置和置信度
4. 匹配失败: miss_count++
5. miss_count > 5: 删除轨迹
6. 未匹配检测: 创建新轨迹
```

### 5.3 设计考量

- IoU匹配而非卡尔曼滤波：1fps低帧率，运动预测意义不大
- 贪心匹配而非匈牙利算法：目标数少，O(n^2)够用
- 同类匹配约束：只在相同类别内匹配

---

## 6. Web服务器

### 6.1 HTTP API

| 端点 | 方法 | 返回 |
|------|------|------|
| `/` | GET | HTML前端页面 |
| `/api/detections` | GET | JSON检测结果 |

JSON格式:
```json
{
    "timestamp": 1234567890,
    "frame_count": 100,
    "fps": 1.0,
    "target_count": 3,
    "targets": [
        {
            "id": 1,
            "class": "person",
            "class_id": 0,
            "confidence": 0.95,
            "cx": 200,
            "cy": 150,
            "w": 80,
            "h": 160,
            "age": 10
        }
    ]
}
```

### 6.2 前端实现

- Canvas绘制检测框和标签(不同类别不同颜色)
- 每秒轮询API获取最新结果
- 纯HTML/JS，无框架依赖
- 支持COCO 80类目标的颜色区分

---

## 7. 交叉编译环境

### 7.1 工具链

- 编译器: arm-himix200-linux-gcc 6.3.0
- 目标: ARM Cortex-A7 (armv7)
- 选项: `-mcpu=cortex-a7 -mfloat-abi=softfp -mfpu=neon-vfpv4`

### 7.2 链接方式

使用静态链接，将SDK库编译进可执行文件：
- libmpi.a, libnnie.a, libsvpruntime.a
- libisp.a, lib_hiae.a, lib_hiawb.a 等ISP库
- libsns_gc2053.a 等传感器驱动库
- libsecurec.a 安全C库

### 7.3 SDK公共源文件

需要将SDK的sample公共源文件一起编译：
- sample_comm_sys.c — 系统初始化
- sample_comm_vi.c — VI配置
- sample_comm_vpss.c — VPSS配置
- sample_comm_svp.c — SVP公共函数
- sample_comm_nnie.c — NNIE公共函数
- 等
