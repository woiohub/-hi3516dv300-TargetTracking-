# Hi3516DV300 + GC2053 端侧AI目标追踪系统

## 项目简介

基于海思Hi3516DV300开发板和格科微GC2053 1080p CMOS图像传感器，实现端侧实时多目标检测与追踪系统。系统使用海思SDK提供的YOLOv3预训练模型，通过NNIE（神经网络推理引擎）硬件加速推理，结合IoU多目标追踪算法，并通过Web页面实时展示检测结果。

## 硬件环境

| 组件 | 型号 | 说明 |
|------|------|------|
| 开发板 | EB-Hi3516DV300-DC-182 | 海思媒体处理芯片开发板 |
| 图像传感器 | 格科微 GC2053 | 1080p CMOS, MIPI接口 |
| 芯片 | Hi3516DV300 | Cortex-A7 + NNIE引擎 |

## 软件环境

| 组件 | 版本/说明 |
|------|----------|
| SDK | Hi3516CV500_SDK_V2.0.2.0 |
| 交叉编译器 | arm-himix200-linux-gcc 6.3.0 |
| 目标系统 | Linux 4.9 (glibc) |
| 模型 | YOLOv3 (COCO 80类预训练, 416x416输入) |
| 推理引擎 | 海思NNIE (INT8硬件加速) |

## 项目结构

```
TargetTracking/
├── Makefile                        # 顶层构建脚本
├── include/
│   └── common.h                    # 公共定义(数据结构、常量、类别名)
├── src/
│   ├── main.c                      # 主程序入口
│   ├── video_capture.c/.h          # 视频采集模块(VI/VPSS)
│   ├── nnie_inference.c/.h         # NNIE推理模块(YOLOv3后处理)
│   ├── tracker.c/.h                # 目标追踪模块(IoU匹配)
│   └── web_server.c/.h             # Web服务器模块
├── web/
│   └── index.html                  # 前端页面
├── scripts/
│   └── load_ko.sh                  # 内核模块加载脚本
├── model/
│   └── data/nnie_model/
│       └── yolov3.wk               # YOLOv3 NNIE模型(61MB)
└── doc/
```

## 编译方法

### 前置条件

- Ubuntu 20.04 (WSL2或原生)
- arm-himix200-linux- 交叉编译工具链已安装
- Hi3516CV500 SDK 已解压到 `/home/woio/hisi/Hi3516CV500_SDK_V2.0.2.0/`

### 编译步骤

```bash
cd /home/woio/project/TargetTracking
make clean
make
```

编译成功后生成 `build/sample_target_tracking` (ARM ELF可执行文件)。

### 部署到开发板

**第一步: 复制SDK内核模块到开发板**

内核模块(`.ko`文件)需要从SDK目录单独复制到开发板的 `/ko/` 目录：

```bash
# 在PC上执行，将SDK内核模块复制到开发板
scp -r /home/woio/hisi/Hi3516CV500_SDK_V2.0.2.0/smp/a7_linux/mpp/ko/ root@<开发板IP>:/ko/
```

**第二步: 复制项目文件到开发板**

```bash
# 方式1: NFS挂载(推荐开发阶段使用)
# 方式2: SCP传输
scp -r build scripts/ model/data/ web/ root@<开发板IP>:/mnt/nfs/TargetTracking/
```

**第三步: 修改开发板启动配置**

板级启动脚本默认使用 `sensor0=imx327`，需要改为 `sensor0=gc2053`：

```bash
# 在开发板上编辑启动脚本
vi /etc/init.d/S99mpp   # 或其他加载ko的启动脚本
# 将 sensor0=imx327 改为 sensor0=gc2053
# 保存后重启
reboot
```

**第四步: 在开发板上执行**

```bash
cd /mnt/nfs/TargetTracking
chmod +x scripts/load_ko.sh build/sample_target_tracking

# 检查内核模块状态(板级启动时已加载)
sh ./scripts/load_ko.sh

# 运行程序
./build/sample_target_tracking ./data/nnie_model/yolov3.wk 8080 ./web
```

> **注意:**
> - 板级启动时已自动加载所有内核模块（MMZ: 0x88000000, 128MB），`load_ko.sh` 会检测并跳过
> - 如果模块未加载，脚本会自动从 `/ko/` 目录加载
> - 板级默认传感器为 imx327，需修改为 gc2053 才能正常使用
> - 如果模块目录不在 `/ko/`，可通过环境变量指定: `KO_DIR=/path/to/ko sh ./scripts/load_ko.sh`

### 访问Web页面

浏览器打开: `http://<开发板IP>:8080`

功能:
- 实时显示检测结果可视化(Canvas绘制边界框)
- 追踪目标列表(ID、类别、置信度、位置)
- 系统状态(FPS、帧计数、运行时间)

## 系统架构

```
GC2053 -> MIPI -> VI -> VPSS -> YUV帧
                                  |
                            VPSS缩放(416x416)
                                  |
                            NV21转BGR Planar
                                  |
                          NNIE推理(YOLOv3 Forward)
                                  |
                          YOLOv3后处理(anchor解码+NMS)
                                  |
                          IoU多目标追踪
                                  |
                      HTTP Server -> 前端页面
```

## 运行参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| 推理帧率 | 1 FPS | 每秒推理一帧 |
| 输入尺寸 | 416x416 | YOLOv3标准输入 |
| 检测类别 | 80类 | COCO全类别 |
| 置信度阈值 | 0.5 | 过滤低置信度检测 |
| NMS阈值 | 0.45 | 非极大值抑制 |
| 最大检测数 | 50 | 单帧最大检测框输出数 |
| 追踪最大丢失帧数 | 5 | 连续5帧未匹配则删除轨迹 |
| Web端口 | 8080 | HTTP服务端口 |

## 命令行参数

```
./sample_target_tracking [模型路径] [Web端口] [前端目录]

示例:
./build/sample_target_tracking                                    # 使用默认参数
./build/sample_target_tracking ./model/data/nnie_model/yolov3.wk # 指定模型
./build/sample_target_tracking model.wk 9090 ./web               # 全部自定义
```

## 许可证

本项目仅供学习和研究使用。
