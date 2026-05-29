# Bug修复记录

## 问题1: mmz.ko加载失败

### 现象

```
insmod: can't insert 'mmz.ko': No such file or directory
[ERROR] mmz.ko加载失败
```

### 根因

SDK V2.0.2.0 中不存在独立的 `mmz.ko`，MMZ管理已集成到 `hi_osal.ko` 中。原脚本还缺少 `sys_config.ko`、`hi_osal.ko`、`hi3516cv500_base.ko` 等关键模块。

### 修复

重写 `load_ko.sh`，使用 `hi_osal.ko` 的 `mmz=` 参数配置MMZ，添加所有缺失模块。

---

## 问题2: NNIE Task Buffer分配失败

### 现象

```
mmz_userdev:ioctl_mmb_alloc: hil_mmb_alloc(SAMPLE_NNIE_TAS, 15999856, 0x0, 0, ) failed!
[ERROR] NNIE参数初始化失败! s32Ret=0xffffffff
```

### 根因

板级启动脚本 `/etc/profile` 使用 `-total 256` 配置，但开发板实际有1GB DDR。MMZ仅分配128MB，不足以容纳YOLOv3模型(60MB) + NNIE Task Buffer(15MB) + 其他模块。

### 修复

修改 `/etc/profile` 中的启动参数: `-total 256` → `-total 512`，MMZ增大到384MB。

---

## 问题3: 卸载模块导致Kernel Panic

### 现象

```
Unable to handle kernel NULL pointer dereference at virtual address 00000000
Kernel panic - not syncing: Fatal exception in interrupt
```

### 根因

运行时卸载内核模块时，遗漏了 `hifb`、`rgn`、`gdc`、`vgs` 等依赖模块，导致已卸载模块的资源被仍加载的模块访问。

### 修复

不在运行时卸载模块。改为修改板级启动脚本后重启。`load_ko.sh` 仅检测和提示，不执行卸载。

---

## 问题4: VPSS帧获取失败(0xa007800e)

### 现象

```
[ERROR] 获取VPSS帧失败! s32Ret=0xa007800e
```

错误码 `0xa007800e` = `HI_ERR_VPSS_BUF_EMPTY`（VPSS通道缓冲区为空）

### 根因分析

从 `/proc/umap/vi` 和 `/proc/umap/vpss` 诊断信息发现:
- VI Pipe 状态全为0（未输出帧）
- VPSS Group/Channel 全为空（未配置）

可能原因:
1. `VI_ONLINE_VPSS_ONLINE` 模式与板级ISP配置冲突
2. 程序重新初始化VI/VPSS时与板级已有的管道冲突
3. VPSS通道配置参数与SDK示例不一致

### 修复

1. 改用与SDK VIO示例完全一致的配置
2. 移除手动VI-VPSS绑定（online模式自动绑定）
3. 添加VIO测试程序 `test_vio.c` 独立验证硬件通路

### 排查步骤

```bash
# 1. 运行VIO测试程序(仅测试VI->VPSS帧获取)
./build/test_vio

# 2. 查看VI状态
cat /proc/umap/vi

# 3. 查看VPSS状态
cat /proc/umap/vpss

# 4. 查看MMZ使用情况
cat /proc/media-mem
```

---

## 经验教训

1. **SDK版本差异** — 不同版本的模块名称和架构可能不同，应参考官方脚本
2. **板级配置优先** — 开发板启动脚本的配置会覆盖我们的脚本，需修改启动脚本
3. **不要运行时卸载模块** — 海思MPP模块依赖复杂，运行时卸载极易导致Kernel Panic
4. **先验证硬件通路** — 使用最小化测试程序验证VI/VPSS是否正常，再集成NNIE
5. **参考SDK示例** — SDK自带的sample程序是最可靠的配置参考
