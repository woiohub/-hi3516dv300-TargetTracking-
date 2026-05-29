# 项目设计文档 — 设计决策与实现路径

## 1. 整体架构设计

### 1.1 设计目标

在资源受限的嵌入式平台上实现多目标检测与追踪系统：
- 实时性：每秒至少处理1帧
- 通用性：支持COCO 80类目标检测
- 可视化：Web页面远程查看
- 可维护性：模块化设计

### 1.2 架构选型

**多线程方案：主线程推理 + 子线程Web服务**

```
主线程: 视频采集 -> NNIE推理 -> 追踪 -> 更新共享数据
Web线程: accept() -> 读取共享数据 -> JSON响应
```

共享数据通过互斥锁保护。

### 1.3 数据流

```
GC2053 (MIPI, 1920x1080, 30fps)
    | (硬件缩放)
VPSS (416x416, 1fps)
    | (CPU转换)
BGR Planar (416x416x3)
    | (NNIE硬件推理)
YOLOv3输出 [1, 255, 13/26/52] x 3尺度
    | (anchor解码 + sigmoid + NMS)
检测结果 DETECTION_RESULTS_S
    | (IoU匹配)
追踪结果 TRACK_RESULTS_S
    | (共享内存)
Web服务器 -> 浏览器
```

---

## 2. 模型选择

### 2.1 为什么选择SDK预训练YOLOv3

| 考虑因素 | SDK预训练YOLOv3 | 自训练YOLOv8n |
|----------|-----------------|---------------|
| 类别数 | 80类(COCO) | 需要自训练 |
| 模型来源 | 官方提供，开箱即用 | 需要训练+转换 |
| 输出格式 | 3个blob, 需anchor解码 | 1个blob, 较简单 |
| NNIE兼容性 | 官方验证 | 需要RuyiStudio转换 |
| 可控性 | 不可修改 | 完全可控 |

选择SDK预训练YOLOv3的原因：
1. 开箱即用，无需训练和模型转换流程
2. 官方优化过的NNIE模型，兼容性有保障
3. 80类检测更通用，可扩展性好
4. 61MB大小适中，平衡精度与推理速度

### 2.2 可用模型对比

SDK中可用的检测模型：

| 模型 | 大小 | 架构 | 速度 | 选择 |
|------|------|------|------|------|
| YOLOv3 | 61M | 单阶段 | 快 | **选用** |
| YOLOv2 | 57M | 单阶段 | 快 | 备选 |
| SSD | 26M | 单阶段 | 最快 | 精度低 |
| Faster R-CNN | 21M/132M | 两阶段 | 慢 | 不适合实时 |
| R-FCN | 34M | 两阶段 | 慢 | 不适合实时 |

---

## 3. 模块设计

### 3.1 视频采集模块

**设计决策：**
1. VPSS双通道输出：通道0原始尺寸(预留)，通道1=416x416(NNIE用)
2. 帧率控制在VPSS层：通道1输出1fps，减少CPU处理
3. YUV到BGR在CPU完成：整数运算快速转换

### 3.2 NNIE推理模块

**设计决策：**
1. 直接调用MPI API（`HI_MPI_SVP_NNIE_Forward`），不依赖SDK的static封装函数
2. 自行实现YOLOv3后处理
3. 遍历3个输出Blob，每个Blob独立处理

**YOLOv3后处理关键逻辑：**

```
遍历3尺度 -> 遍历grid cell -> 遍历3个anchor
  -> 读取(tx,ty,tw,th,obj,class_scores)
  -> sigmoid(obj)过滤
  -> 找最大类别 * sigmoid(obj) = 最终置信度
  -> anchor解码得到绝对坐标
  -> 收集候选框
-> NMS去重
```

**数据偏移计算：**
```c
offset = anchor_idx * 85 * grid_h * stride + h * stride + w
```
其中stride是W维度按32字节对齐后的值。

### 3.3 追踪模块

**设计决策：**
1. IoU匹配而非卡尔曼滤波（1fps低帧率，运动预测意义不大）
2. 贪心匹配而非匈牙利算法（目标数少，O(n^2)够用）
3. 同类匹配约束（person只和person匹配）

### 3.4 Web服务器模块

**设计决策：**
1. 纯C socket实现，不依赖外部HTTP库
2. 单线程阻塞模型（请求处理快，够用）
3. JSON手动拼接（不依赖cJSON库）
4. 缓冲区大小16KB，支持最多50个目标的JSON响应

---

## 4. 关键问题解决

### 4.1 编译链接问题

**问题1：SDK的SAMPLE_COMM_*函数未定义**

原因：这些函数是源代码，不是预编译库

解决：将SDK公共源文件加入Makefile一起编译

```makefile
SDK_COMMON_SRCS := $(SDK_COMMON_DIR)/sample_comm_sys.c \
                   $(SDK_COMMON_DIR)/sample_comm_vi.c \
                   ...
```

**问题2：memcpy_s未定义**

原因：libmpi.a依赖安全C函数

解决：添加libsecurec.a

**问题3：传感器符号未定义**

原因：sample_comm_isp.c引用了所有传感器驱动

解决：添加所有传感器驱动库

### 4.2 NNIE推理问题

**问题：SAMPLE_SVP_NNIE_Forward不可用**

原因：是SDK示例中的static函数

解决：直接调用MPI底层API
```c
HI_MPI_SVP_NNIE_Forward(&handle, src, model, dst, ctrl, HI_TRUE);
HI_MPI_SVP_NNIE_Query(nnieId, handle, &finish, HI_TRUE);
```

### 4.3 YOLOv3多尺度输出

**问题：YOLOv3有3个输出Blob，如何处理**

解决：遍历`astForwardCtrl[0].u32DstNum`获取输出Blob数量，对每个Blob独立做anchor解码，最后统一NMS。

```c
for (s = 0; s < YOLO3_SCALE_NUM && s < u32DstNum; s++) {
    SVP_DST_BLOB_S* pstDst = &pstCtx->stNnieParam.astSegData[0].astDst[s];
    // 解码该尺度的anchor...
}
// 统一NMS
```

---

## 5. 性能优化

### 5.1 已实现的优化

1. VPSS硬件缩放：避免CPU做图像缩放
2. VPSS帧率控制：硬件层降采样到1fps
3. 静态链接：减少运行时开销
4. NNIE硬件加速：INT8量化推理

### 5.2 可能的优化

1. 零拷贝：直接用VPSS物理地址作为NNIE输入
2. NEON加速：YUV转BGR使用SIMD指令
3. 双缓冲：采集和推理流水线并行
4. VPSS裁剪：只处理感兴趣区域

---

## 6. 扩展方向

1. 自训练模型：替换为专门训练的单类别/多类别模型
2. 视频流推送：添加RTSP支持
3. 告警功能：检测到特定目标时触发告警
4. 深度追踪：使用DeepSORT等算法
5. 多模型切换：支持运行时切换不同.wk模型

---

## 7. 部署工作流

### 7.1 完整部署步骤

```
开发主机                          开发板
┌─────────────┐                 ┌─────────────────┐
│ 交叉编译     │                 │                  │
│ make         │                 │                  │
│      ↓       │                 │                  │
│ build/       │ ──scp/nfs──→   │ /mnt/nfs/        │
│ sample_...   │                 │   TargetTracking/│
│              │                 │   ├── build/     │
│ SDK mpp/ko/  │ ──scp──────→   │   ├── scripts/   │
│              │                 │   ├── model/     │
│              │                 │   ├── web/       │
│              │                 │   └── /ko/       │
└─────────────┘                 └─────────────────┘
```

### 7.2 开发板文件布局

```
/mnt/nfs/TargetTracking/          # 项目目录(NFS挂载)
├── build/
│   ├── sample_target_tracking    # 主程序
│   └── test_vio                  # VIO测试程序
├── scripts/load_ko.sh            # 内核模块加载脚本
├── model/data/nnie_model/yolov3.wk  # NNIE模型
├── web/index.html                # 前端页面
└── ko/                           # SDK内核模块(从SDK复制)
    ├── load3516dv300             # SDK官方加载脚本
    ├── sys_config.ko
    ├── hi_osal.ko                # MMZ内存管理
    ├── hi3516cv500_base.ko
    ├── hi3516cv500_sys.ko
    ├── hi3516cv500_vi.ko
    ├── hi3516cv500_isp.ko
    ├── hi3516cv500_vpss.ko
    ├── hi3516cv500_nnie.ko
    └── ...
```

板级启动脚本 `/etc/profile` 通过 `load3516dv300` 加载模块:
```bash
cd /mnt/nfs/TargetTracking/ko && ./load3516dv300 -i -sensor gc2053 -osmem 128 -total 512
```

### 7.3 内核模块加载顺序

模块加载必须遵循严格的依赖顺序（参考SDK官方 `load3516dv300` 脚本）：

```
1. sys_config.ko          芯片/传感器配置（必须最先加载）
2. hi_osal.ko             OS抽象层 + MMZ内存（替代旧版mmz.ko）
3. hi3516cv500_base.ko    基础驱动
4. hi3516cv500_sys.ko     系统模块（VB缓存池）
5. hi3516cv500_vi.ko      视频输入
   hi3516cv500_isp.ko     图像信号处理
   hi_mipi_rx.ko          MIPI接收器
   extdrv/hi_sensor_*.ko  传感器I2C/SPI驱动
6. hi3516cv500_vpss.ko    视频处理子系统
7. hi3516cv500_nnie.ko    NNIE推理引擎
8. hi3516cv500_tde.ko     2D图形引擎
   hi3516cv500_venc.ko    视频编码器
   hi3516cv500_vo.ko      视频输出
9. hi3516cv500_ive.ko     IVE智能视频分析
   hi3516cv500_svprt.ko   SVP运行时
```

### 7.4 MMZ内存规划

| DDR容量 | -total参数 | OS内存 | MMZ大小 | 适用场景 |
|---------|-----------|--------|---------|---------|
| 1GB | 512 | 128MB | 384MB | 本项目配置 |
| 1GB | 1024 | 128MB | 896MB | 充裕配置 |
| 256MB | 256 | 128MB | 128MB | 最小配置 |

MMZ内存用途分配：
- NNIE模型: ~60MB（YOLOv3 .wk文件加载）
- NNIE Task Buffer: ~16MB（模型推理工作区）
- VI/VPSS帧缓存: ~50MB（视频帧缓冲）
- 其他MPP模块: ~258MB

### 7.5 板级启动配置

开发板通过 `/etc/profile` 中的 `load3516dv300` 命令加载内核模块。配置修改需编辑此文件后重启:

```bash
# 编辑启动脚本
vi /etc/profile
# 修改 load3516dv300 参数
# 保存后重启
reboot
```

**不要在运行时卸载内核模块**，海思MPP模块依赖复杂，运行时卸载极易导致Kernel Panic。
