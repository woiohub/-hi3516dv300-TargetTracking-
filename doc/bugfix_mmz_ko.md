# Bug修复记录: mmz.ko加载失败

## 问题现象

在开发板上执行 `scripts/load_ko.sh` 时:

```
/mnt/nfs/TargetTracking # sh ./scripts/load_ko.sh
insmod: can't insert 'mmz.ko': No such file or directory
[ERROR] mmz.ko加载失败
所有内核模块加载完成
```

错误被 `|| echo` 捕获后脚本继续执行，但后续所有依赖MMZ内存的模块（NNIE、VI、VPSS等）都会因MMZ未初始化而失败。

---

## 根因分析

### 1. `mmz.ko` 不存在

**核心问题:** 在 Hi3516CV500 SDK V2.0.2.0 中，`mmz.ko` 不再作为独立模块存在。

SDK的 `mpp/ko/` 目录中所有 `.ko` 文件:
```
hi3516cv500_base.ko    hi3516cv500_nnie.ko    hi3516cv500_venc.ko
hi3516cv500_sys.ko     hi3516cv500_tde.ko     hi3516cv500_vi.ko
hi3516cv500_vpss.ko    hi3516cv500_vo.ko      hi3516cv500_ive.ko
hi_osal.ko             hi_mipi_rx.ko          sys_config.ko
...
```

**没有 `mmz.ko`。** MMZ内存管理已集成到 `hi_osal.ko` 中。

### 2. SDK版本演进

| SDK版本 | MMZ管理方式 |
|---------|------------|
| 旧版(如Hi3516CV300) | 独立的 `mmz.ko` 模块 |
| V2.0.2.0(本项目) | 集成在 `hi_osal.ko` 中，通过 `mmz=` 参数配置 |

参考SDK官方脚本 `load3516dv300` 第84行:
```bash
insmod hi_osal.ko anony=1 mmz_allocator=hisi mmz=anonymous,0,$mmz_start,$mmz_size
```

### 3. 原脚本的其他问题

原 `load_ko.sh` 除了 `mmz.ko` 不存在外，还缺少多个关键模块:

| 缺失模块 | 作用 | 影响 |
|----------|------|------|
| `sys_config.ko` | 芯片/传感器类型配置 | 传感器无法识别 |
| `hi_osal.ko` | OS抽象层+MMZ管理 | MMZ内存无法分配 |
| `hi3516cv500_base.ko` | 基础驱动 | 系统功能不完整 |
| `hi3516cv500_isp.ko` | 图像信号处理 | 摄像头画面异常 |
| `hi_mipi_rx.ko` | MIPI接收器 | 摄像头数据无法接收 |
| `extdrv/hi_sensor_i2c.ko` | 传感器I2C通信 | 传感器控制失败 |
| `extdrv/hi_sensor_spi.ko` | 传感器SPI通信 | 传感器控制失败 |
| `hi3516cv500_chnl.ko` | 视频编码通道 | 编码功能不可用 |
| `hi3516cv500_vedu.ko` | 视频编码核心 | 编码功能不可用 |
| `hi3516cv500_rc.ko` | 码率控制 | 编码质量不可控 |
| `hi3516cv500_svprt.ko` | SVP运行时 | NNIE推理可能异常 |

### 4. 脚本执行位置问题

原脚本使用 `insmod mmz.ko`（相对路径），意味着 `.ko` 文件必须在当前目录。但脚本从项目目录 `/mnt/nfs/TargetTracking/` 执行，而 `.ko` 文件在SDK的 `ko/` 目录中。

---

## 修复方案

### 修改文件: `scripts/load_ko.sh`

**主要变更:**

1. **替换 `mmz.ko`** → 使用 `hi_osal.ko` 并传入MMZ参数:
   ```bash
   insmod hi_osal.ko anony=1 mmz_allocator=hisi mmz=anonymous,0,0x84000000,192M
   ```

2. **添加KO_DIR变量** → 脚本自动切换到模块目录:
   ```bash
   KO_DIR="${KO_DIR:-/ko}"
   cd "$KO_DIR"
   ```

3. **添加所有缺失模块** → 按SDK官方 `load3516dv300` 的顺序加载

4. **添加 `load_module()` 函数** → 优雅处理模块已加载的情况

5. **添加部署检查** → 如果 `/ko/` 目录不存在，提示用户如何部署

### MMZ内存规划

对于256MB DDR的开发板:
```
DDR: 0x80000000 - 0x8FFFFFFF (256MB)
├── Linux OS:    0x80000000 - 0x83FFFFFF (64MB)
└── MMZ:         0x84000000 - 0x8FFFFFFF (192MB)
    ├── NNIE Task Buffer: ~16MB
    ├── VI/VPSS缓存: ~50MB
    └── 其他MPP模块: ~126MB
```

---

## 前置条件

修复后的脚本需要SDK的内核模块文件存在于开发板上:

```bash
# 在PC上，将SDK的ko目录复制到开发板
scp -r /home/woio/hisi/Hi3516CV500_SDK_V2.0.2.0/smp/a7_linux/mpp/ko/ root@<board_ip>:/ko/
```

---

## 验证方法

```bash
# 1. 执行加载脚本
sh ./scripts/load_ko.sh

# 2. 检查MMZ是否分配成功
cat /proc/media-mem

# 3. 检查所有模块是否加载
lsmod | grep hi3516cv500
```

---

## 经验教训

1. **不要假设SDK模块名称** — 不同SDK版本的模块组织方式可能不同，应参考官方脚本
2. **错误处理不应静默** — 原脚本用 `|| echo` 捕获错误后继续执行，掩盖了问题
3. **模块加载有依赖顺序** — 必须按 `sys_config → hi_osal → base → sys → ...` 的顺序加载
4. **insmod需要正确路径** — 使用相对路径时必须确保当前目录正确

---

## 问题2: NNIE参数初始化失败(MMZ内存分配失败)

### 问题现象

修复 `mmz.ko` 问题后，在开发板上运行程序:

```
[INFO] NNIE模型加载成功, 网络段数: 1
mmz_userdev:ioctl_mmb_alloc: hil_mmb_alloc(SAMPLE_NNIE_TAS, 15999856, 0x0, 0, ) failed!
[Func]:hi_mpi_sys_mmz_alloc_cached [Line]:913 [Info]:system alloc mmz memory failed!
[ERROR] NNIE参数初始化失败! s32Ret=0xffffffff
```

### 根因分析

**关键线索:** 所有内核模块显示 `[WARN] ... 已加载，跳过`

开发板在启动时已通过系统初始化脚本加载了所有内核模块，使用的是**板级默认MMZ配置**（0x88000000, 128MB），而不是我们脚本中配置的（0x84000000, 192MB）。

**MMZ信息对比:**

| 项目 | 板级默认(实际) | 脚本配置(期望) |
|------|---------------|---------------|
| 起始地址 | 0x88000000 | 0x84000000 |
| 大小 | 128MB | 192MB |
| 剩余空间 | 111MB | - |

**为什么111MB空闲却分配15MB失败?**

MMZ是物理连续内存分配器。虽然总空闲111MB，但可能存在以下情况:
1. 内存碎片化导致没有15MB的连续空闲块
2. 板级默认配置的某些约束限制了最大分配大小
3. MMZ分配器的内部管理开销

**根本原因:** `load_module()` 函数检测到模块已加载就跳过了，但没有检查MMZ配置是否正确。`hi_osal.ko` 已经用错误的MMZ参数加载了，我们的配置完全无效。

### 修复方案

修改 `load_ko.sh`:

1. **添加 `--force` 参数** — 强制卸载所有模块后重新加载
2. **添加 `unload_all()` 函数** — 按依赖反序卸载所有模块
3. **自动检测MMZ配置** — 检查当前MMZ是否匹配期望配置
4. **不匹配时自动重载** — 无需手动加 `--force`

```bash
# 使用方法
sh ./scripts/load_ko.sh           # 自动检测，MMZ不匹配时自动重载
sh ./scripts/load_ko.sh --force   # 强制卸载后重新加载
```

### 验证

加载成功后应看到:

```
=== MMZ内存信息 ===
+---ZONE: PHYS(0x84000000, 0x8FFFFFFF), GFP=0, nBYTES=196608KB, NAME="anonymous"
```
