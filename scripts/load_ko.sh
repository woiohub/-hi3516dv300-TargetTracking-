#!/bin/sh
# Hi3516DV300 内核模块加载脚本
# 基于SDK官方 load3516dv300 脚本改写，适配GC2053传感器
#
# 使用方法:
#   1. 将SDK的 mpp/ko/ 目录复制到开发板 /ko/ 目录
#   2. 在开发板上执行: sh ./scripts/load_ko.sh
#
# 注意: 本脚本必须在 /ko/ 目录存在的情况下运行

# 不使用set -e，允许模块已加载的情况

#################### 可配置变量 ########################
KO_DIR="${KO_DIR:-/ko}"                       # 内核模块目录
SNS_TYPE0="${SNS_TYPE0:-gc2053}"              # 传感器类型
CHIP_TYPE="${CHIP_TYPE:-hi3516dv300}"         # 芯片类型
YUV_TYPE0="${YUV_TYPE0:-0}"                   # 0=raw, 1=bt1120/DC, 2=bt656
mem_start=0x80000000                          # DDR物理起始地址
mmz_start="${MMZ_START:-0x84000000}"          # MMZ起始地址
mmz_size="${MMZ_SIZE:-192M}"                  # MMZ大小(256MB DDR: 64MB OS + 192MB MMZ)
#######################################################

load_module() {
    local module=$1
    shift
    if insmod "$module" "$@" 2>/dev/null; then
        return 0
    else
        # 检查是否已加载
        local name=$(basename "$module" .ko)
        if grep -q "^${name}" /proc/modules 2>/dev/null; then
            echo "  [WARN] $module 已加载，跳过"
            return 0
        fi
        echo "  [ERROR] $module 加载失败"
        return 1
    fi
}

# 检查ko目录
if [ ! -d "$KO_DIR" ]; then
    echo "[ERROR] 内核模块目录不存在: $KO_DIR"
    echo "请先将SDK的 mpp/ko/ 目录复制到开发板:"
    echo "  scp -r /home/woio/hisi/Hi3516CV500_SDK_V2.0.2.0/smp/a7_linux/mpp/ko/ root@<board_ip>:/ko/"
    exit 1
fi

cd "$KO_DIR"

echo "=== 加载内核模块 ==="
echo "  传感器: $SNS_TYPE0"
echo "  MMZ: $mmz_start, $mmz_size"
echo "  模块目录: $KO_DIR"
echo ""

# 1. 系统配置模块(传感器类型配置)
echo "[1/9] sys_config.ko ..."
load_module sys_config.ko chip=$CHIP_TYPE sensors=sns0=$SNS_TYPE0,sns1=NULL, g_cmos_yuv_flag=$YUV_TYPE0

# 2. OS抽象层 + MMZ内存管理
# 注意: 在SDK V2.0.2.0中，MMZ由hi_osal.ko管理，不存在独立的mmz.ko
echo "[2/9] hi_osal.ko (MMZ: $mmz_start, $mmz_size) ..."
load_module hi_osal.ko anony=1 mmz_allocator=hisi mmz=anonymous,0,$mmz_start,$mmz_size

# 3. 基础驱动
echo "[3/9] hi3516cv500_base.ko ..."
load_module hi3516cv500_base.ko

# 4. 系统模块
echo "[4/9] hi3516cv500_sys.ko ..."
load_module hi3516cv500_sys.ko

# 5. 视频输入 + ISP + 传感器驱动
echo "[5/9] vi/isp/sensor ..."
load_module hi3516cv500_vi.ko
load_module hi3516cv500_isp.ko
load_module extdrv/hi_sensor_i2c.ko
load_module extdrv/hi_sensor_spi.ko
load_module hi_mipi_rx.ko

# 6. 视频处理子系统
echo "[6/9] hi3516cv500_vpss.ko ..."
load_module hi3516cv500_vpss.ko

# 7. NNIE神经网络推理引擎
echo "[7/9] hi3516cv500_nnie.ko ..."
load_module hi3516cv500_nnie.ko nnie_save_power=1 nnie_max_tskbuf_num=32

# 8. 其他模块
echo "[8/9] tde/venc/vo ..."
load_module hi3516cv500_tde.ko
load_module hi3516cv500_chnl.ko
load_module hi3516cv500_vedu.ko
load_module hi3516cv500_rc.ko
load_module hi3516cv500_venc.ko
load_module hi3516cv500_h264e.ko
load_module hi3516cv500_h265e.ko
load_module hi3516cv500_jpege.ko
load_module hi3516cv500_vo.ko
load_module hi3516cv500_hdmi.ko

# 9. SVP/IVE(NNIE依赖)
echo "[9/9] ive/svprt ..."
load_module hi3516cv500_ive.ko save_power=0
load_module hi3516cv500_svprt.ko

echo ""
echo "=== 所有内核模块加载完成 ==="

# 验证MMZ
if [ -f /proc/media-mem ]; then
    echo ""
    echo "=== MMZ内存信息 ==="
    cat /proc/media-mem | head -20
fi
