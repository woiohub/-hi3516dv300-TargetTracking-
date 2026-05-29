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

---

## 8. 部署问题排查

### 8.1 MMZ内存分配失败

**错误现象：**
```
mmz_userdev:ioctl_mmb_alloc: hil_mmb_alloc(SAMPLE_NNIE_TAS, 15999856, 0x0, 0, ) failed!
[Func]:hi_mpi_sys_mmz_alloc_cached [Line]:913 [Info]:system alloc mmz memory failed!
[ERROR] NNIE参数初始化失败! s32Ret=0xffffffff
```

**原因分析：**

NNIE引擎需要从MMZ(Media Memory Zone)分配约16MB的Task Buffer。分配失败说明MMZ内存不足。

常见原因：
1. **MMZ区域地址重叠** — load_ko.sh中配置了两个MMZ区域，如果起始地址有重叠会导致内存管理混乱
2. **MMZ总量过大** — 超出实际DDR物理内存容量
3. **MMZ未正确加载** — mmz.ko加载失败但错误被静默

**解决方案：**

修改 `scripts/load_ko.sh`，使用单个MMZ区域，分配合理大小：

```bash
# 错误配置(两个区域地址重叠)
insmod mmz.ko mmz=anonymous,0,0x84000000,576M anony=1,0,0x8FA00000,64M

# 正确配置(单区域，256MB足够)
insmod mmz.ko mmz=anonymous,0,0x84000000,256M
```

**内存规划参考：**

| 用途 | 大小 | 说明 |
|------|------|------|
| Linux内核+用户空间 | ~64MB | 0x80000000起始 |
| MMZ区域 | 256MB | 0x84000000起始 |
| NNIE Task Buffer | ~16MB | 从MMZ分配 |
| VI/VPSS缓存 | ~50MB | 从MMZ分配 |
| 剩余可用 | ~190MB | 其他MPP模块 |

**排查命令：**
```bash
# 检查mmz.ko是否加载成功
cat /proc/media-mem

# 检查MMZ区域信息
cat /proc/himedia/mmz
```

### 8.2 传感器初始化失败

**错误现象：**
```
GC2053 init failed!
```

**排查步骤：**
1. 检查MIPI排线是否松动
2. 确认传感器供电正常
3. 检查 `load_ko.sh` 中的sensor驱动是否匹配

### 8.3 NNIE推理结果异常

**可能原因：**
1. 输入BGR数据格式不正确(检查NV21转BGR)
2. 模型输入尺寸不匹配(必须416x416)
3. 输出Blob维度与预期不符(检查启动日志中的Blob信息)

### 8.4 mmz.ko加载失败

**错误现象：**
```
insmod: can't insert 'mmz.ko': No such file or directory
[ERROR] mmz.ko加载失败
```

**原因分析：**

在Hi3516CV500 SDK V2.0.2.0中，**不存在独立的 `mmz.ko` 模块**。MMZ（Media Memory Zone）内存管理已集成到 `hi_osal.ko` 中。

SDK版本演进：
- 旧版SDK（如Hi3516CV300）：MMZ由独立的 `mmz.ko` 管理
- SDK V2.0.2.0（本项目）：MMZ由 `hi_osal.ko` 的 `mmz=` 参数配置

**解决方案：**

使用 `hi_osal.ko` 替代 `mmz.ko`：
```bash
# 错误（mmz.ko不存在）
insmod mmz.ko mmz=anonymous,0,0x84000000,256M

# 正确（使用hi_osal.ko）
insmod hi_osal.ko anony=1 mmz_allocator=hisi mmz=anonymous,0,0x84000000,192M
```

**加载顺序（参考SDK官方 load3516dv300 脚本）：**
```
sys_config.ko → hi_osal.ko → hi3516cv500_base.ko → hi3516cv500_sys.ko
→ hi3516cv500_vi.ko → hi3516cv500_isp.ko → hi_mipi_rx.ko
→ hi3516cv500_vpss.ko → hi3516cv500_nnie.ko → 其他模块
```

---

## 9. 内核模块加载

### 9.1 模块架构

Hi3516DV300的MPP（媒体处理平台）基于内核模块实现，各模块有严格的依赖关系：

```
sys_config.ko        ← 芯片类型、传感器类型配置
    ↓
hi_osal.ko           ← OS抽象层 + MMZ内存管理
    ↓
hi3516cv500_base.ko  ← 基础驱动
    ↓
hi3516cv500_sys.ko   ← 系统模块（VB视频缓存池等）
    ↓
┌───────────────────────────────────────────┐
│  hi3516cv500_vi.ko    视频输入            │
│  hi3516cv500_isp.ko   图像信号处理        │
│  hi_mipi_rx.ko        MIPI接收器          │
│  extdrv/hi_sensor_*.ko 传感器驱动         │
└───────────────────────────────────────────┘
    ↓
hi3516cv500_vpss.ko  ← 视频处理子系统
    ↓
hi3516cv500_nnie.ko  ← NNIE推理引擎（依赖MMZ）
```

### 9.2 MMZ内存管理

MMZ（Media Memory Zone）是海思芯片特有的物理连续内存管理机制：

| 特性 | 说明 |
|------|------|
| 用途 | NNIE模型加载、VI/VPSS帧缓存、VENC编码缓存 |
| 分配方式 | 物理连续内存，通过 `HI_MPI_SYS_MmzAlloc` 分配 |
| 地址空间 | 独立于Linux内核管理的内存 |
| 典型大小 | 192MB~384MB（取决于DDR总容量） |

**内存规划（256MB DDR）：**
```
0x80000000 ┌─────────────────────┐
           │ Linux内核+用户空间   │ 64MB
0x84000000 ├─────────────────────┤
           │ MMZ区域              │ 192MB
           │  ├── NNIE Task: 16MB │
           │  ├── VI/VPSS: 50MB   │
           │  └── 其他: 126MB     │
0x90000000 └─────────────────────┘
```

### 9.3 部署流程

SDK的内核模块文件位于开发主机的SDK目录中：
```
/home/woio/hisi/Hi3516CV500_SDK_V2.0.2.0/smp/a7_linux/mpp/ko/
```

需要将整个 `ko/` 目录复制到开发板：
```bash
scp -r <SDK_PATH>/smp/a7_linux/mpp/ko/ root@<board_ip>:/ko/
```

### 9.4 常用排查命令

```bash
# 查看已加载模块
lsmod

# 查看MMZ内存使用情况
cat /proc/media-mem

# 查看MMZ区域信息
cat /proc/himedia/mmz

# 查看内核日志（模块加载错误）
dmesg | tail -50
```
