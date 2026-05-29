#!/bin/sh
# Hi3516DV300 内核模块加载脚本
# 基于SDK官方 load3516dv300 脚本改写，适配GC2053传感器
#
# 使用方法:
#   sh ./scripts/load_ko.sh          # 加载模块(已加载则检查MMZ配置)
#   sh ./scripts/load_ko.sh --force  # 强制卸载后重新加载(需重启后使用!)
#
# 重要: --force 会在运行时卸载所有内核模块，可能导致系统不稳定。
#       建议仅在系统刚启动、尚未运行应用程序时使用。
#       如果系统已运行其他MPP程序，请先重启开发板再使用 --force。

#################### 可配置变量 ########################
KO_DIR="${KO_DIR:-/ko}"                       # 内核模块目录
SNS_TYPE0="${SNS_TYPE0:-gc2053}"              # 传感器类型
CHIP_TYPE="${CHIP_TYPE:-hi3516dv300}"         # 芯片类型
YUV_TYPE0="${YUV_TYPE0:-0}"                   # 0=raw, 1=bt1120/DC, 2=bt656
mmz_start="${MMZ_START:-0x84000000}"          # MMZ起始地址
mmz_size="${MMZ_SIZE:-192M}"                  # MMZ大小(256MB DDR: 64MB OS + 192MB MMZ)
FORCE=0                                        # 强制重载标志
#######################################################

# 解析参数
for arg in "$@"; do
    case "$arg" in
        --force|-f) FORCE=1 ;;
    esac
done

# 检查ko目录
if [ ! -d "$KO_DIR" ]; then
    echo "[ERROR] 内核模块目录不存在: $KO_DIR"
    echo "请先将SDK的 mpp/ko/ 目录复制到开发板:"
    echo "  scp -r <SDK_PATH>/smp/a7_linux/mpp/ko/ root@<board_ip>:/ko/"
    exit 1
fi

cd "$KO_DIR"

# 卸载所有模块(严格按照SDK官方 remove_ko() 顺序)
unload_all()
{
    echo "=== 卸载已加载的内核模块 ==="
    echo "  [WARN] 卸载内核模块可能导致系统不稳定"
    echo ""

    # 音频模块
    rmmod hi3516cv500_acodec 2>/dev/null
    rmmod hi3516cv500_adec 2>/dev/null
    rmmod hi3516cv500_aenc 2>/dev/null
    rmmod hi3516cv500_ao 2>/dev/null
    rmmod hi3516cv500_ai 2>/dev/null
    rmmod hi3516cv500_aio 2>/dev/null
    echo "  音频模块已卸载"

    # MIPI/传感器/外设
    rmmod hi_mipi_rx 2>/dev/null
    rmmod hi_piris 2>/dev/null
    rmmod hi_pwm 2>/dev/null
    echo "  MIPI/外设模块已卸载"

    # NNIE/IVE/SVP
    rmmod hi3516cv500_nnie 2>/dev/null
    rmmod hi3516cv500_ive 2>/dev/null
    rmmod hi3516cv500_svprt 2>/dev/null
    echo "  NNIE/SVP模块已卸载"

    # 视频解码
    rmmod hi3516cv500_jpegd 2>/dev/null
    rmmod hi3516cv500_vfmw 2>/dev/null
    rmmod hi3516cv500_vdec 2>/dev/null
    echo "  视频解码模块已卸载"

    # 视频编码
    rmmod hi3516cv500_rc 2>/dev/null
    rmmod hi3516cv500_jpege 2>/dev/null
    rmmod hi3516cv500_h264e 2>/dev/null
    rmmod hi3516cv500_h265e 2>/dev/null
    rmmod hi3516cv500_venc 2>/dev/null
    rmmod hi3516cv500_vedu 2>/dev/null
    rmmod hi3516cv500_chnl 2>/dev/null
    echo "  视频编码模块已卸载"

    # 显示/图形
    rmmod hifb 2>/dev/null
    rmmod hi3516cv500_vo 2>/dev/null
    rmmod hi3516cv500_hdmi 2>/dev/null
    echo "  显示模块已卸载"

    # 视频处理
    rmmod hi3516cv500_vpss 2>/dev/null
    rmmod hi3516cv500_isp 2>/dev/null
    rmmod hi3516cv500_vi 2>/dev/null
    rmmod hi3516cv500_gdc 2>/dev/null
    rmmod hi3516cv500_dis 2>/dev/null
    rmmod hi3516cv500_vgs 2>/dev/null
    rmmod hi3516cv500_rgn 2>/dev/null
    rmmod hi3516cv500_tde 2>/dev/null
    echo "  视频处理模块已卸载"

    # 传感器驱动
    rmmod hi_sensor_i2c 2>/dev/null
    rmmod hi_sensor_spi 2>/dev/null
    echo "  传感器驱动已卸载"

    # 系统基础
    rmmod hi3516cv500_sys 2>/dev/null
    rmmod hi3516cv500_base 2>/dev/null
    rmmod hi_osal 2>/dev/null
    rmmod sys_config 2>/dev/null
    echo "  系统模块已卸载"

    echo "  卸载完成"
    echo ""
}

# 检查当前MMZ配置
check_mmz()
{
    if [ ! -f /proc/media-mem ]; then
        return 1  # MMZ未初始化
    fi
    # 检查MMZ起始地址是否匹配
    if grep -qi "$(echo $mmz_start | tr 'a-f' 'A-F')" /proc/media-mem 2>/dev/null; then
        return 0  # 匹配
    fi
    return 1  # 不匹配
}

# 主逻辑
if [ "$FORCE" -eq 1 ]; then
    # 强制模式: 卸载后重新加载
    unload_all
else
    # 检查模块是否已加载
    if grep -q "^hi_osal" /proc/modules 2>/dev/null; then
        if check_mmz; then
            echo "=== 内核模块已加载，MMZ配置正确 ==="
            echo "  MMZ: $(grep 'PHYS(' /proc/media-mem | head -1 | grep -o 'PHYS([^)]*)')"
            echo "  如需重新加载请使用: sh $0 --force"
            exit 0
        else
            current_mmz=$(grep 'PHYS(' /proc/media-mem 2>/dev/null | head -1 | grep -o 'PHYS([^)]*)')
            echo "[ERROR] MMZ配置不匹配!"
            echo "  当前: $current_mmz"
            echo "  期望: PHYS($mmz_start, ...)"
            echo ""
            echo "  开发板启动时已使用默认MMZ配置加载了内核模块。"
            echo "  要应用新配置，请执行以下操作之一:"
            echo ""
            echo "  方案1: 重启开发板后运行(推荐)"
            echo "    reboot"
            echo "    # 重启后执行:"
            echo "    sh $0 --force"
            echo ""
            echo "  方案2: 修改开发板启动脚本"
            echo "    编辑 /etc/init.d/rcS 或相关启动脚本"
            echo "    将MMZ参数修改为: mmz=anonymous,0,$mmz_start,$mmz_size"
            echo "    然后重启开发板"
            echo ""
            exit 1
        fi
    fi
fi

echo "=== 加载内核模块 ==="
echo "  传感器: $SNS_TYPE0"
echo "  MMZ: $mmz_start, $mmz_size"
echo "  模块目录: $KO_DIR"
echo ""

# 1. 系统配置模块(传感器类型配置)
echo "[1/9] sys_config.ko ..."
insmod sys_config.ko chip=$CHIP_TYPE sensors=sns0=$SNS_TYPE0,sns1=NULL, g_cmos_yuv_flag=$YUV_TYPE0 || {
    echo "  [ERROR] sys_config.ko 加载失败"; exit 1; }

# 2. OS抽象层 + MMZ内存管理
# 注意: 在SDK V2.0.2.0中，MMZ由hi_osal.ko管理，不存在独立的mmz.ko
echo "[2/9] hi_osal.ko (MMZ: $mmz_start, $mmz_size) ..."
insmod hi_osal.ko anony=1 mmz_allocator=hisi mmz=anonymous,0,$mmz_start,$mmz_size || {
    echo "  [ERROR] hi_osal.ko 加载失败"; exit 1; }

# 3. 基础驱动
echo "[3/9] hi3516cv500_base.ko ..."
insmod hi3516cv500_base.ko || { echo "  [ERROR] hi3516cv500_base.ko 加载失败"; exit 1; }

# 4. 系统模块
echo "[4/9] hi3516cv500_sys.ko ..."
insmod hi3516cv500_sys.ko || { echo "  [ERROR] hi3516cv500_sys.ko 加载失败"; exit 1; }

# 5. 视频输入 + ISP + 传感器驱动
echo "[5/9] vi/isp/sensor ..."
insmod hi3516cv500_vi.ko || { echo "  [ERROR] hi3516cv500_vi.ko 加载失败"; exit 1; }
insmod hi3516cv500_isp.ko || { echo "  [ERROR] hi3516cv500_isp.ko 加载失败"; exit 1; }
insmod extdrv/hi_sensor_i2c.ko || { echo "  [ERROR] hi_sensor_i2c.ko 加载失败"; exit 1; }
insmod extdrv/hi_sensor_spi.ko || { echo "  [ERROR] hi_sensor_spi.ko 加载失败"; exit 1; }
insmod hi_mipi_rx.ko || { echo "  [ERROR] hi_mipi_rx.ko 加载失败"; exit 1; }

# 6. 视频处理子系统
echo "[6/9] hi3516cv500_vpss.ko ..."
insmod hi3516cv500_vpss.ko || { echo "  [ERROR] hi3516cv500_vpss.ko 加载失败"; exit 1; }

# 7. NNIE神经网络推理引擎
echo "[7/9] hi3516cv500_nnie.ko ..."
insmod hi3516cv500_nnie.ko nnie_save_power=1 nnie_max_tskbuf_num=32 || {
    echo "  [ERROR] hi3516cv500_nnie.ko 加载失败"; exit 1; }

# 8. 其他模块
echo "[8/9] tde/venc/vo ..."
insmod hi3516cv500_tde.ko
insmod hi3516cv500_chnl.ko
insmod hi3516cv500_vedu.ko
insmod hi3516cv500_rc.ko
insmod hi3516cv500_venc.ko
insmod hi3516cv500_h264e.ko
insmod hi3516cv500_h265e.ko
insmod hi3516cv500_jpege.ko
insmod hi3516cv500_vo.ko
insmod hi3516cv500_hdmi.ko

# 9. SVP/IVE(NNIE依赖)
echo "[9/9] ive/svprt ..."
insmod hi3516cv500_ive.ko save_power=0
insmod hi3516cv500_svprt.ko

echo ""
echo "=== 所有内核模块加载完成 ==="

# 验证MMZ
if [ -f /proc/media-mem ]; then
    echo ""
    echo "=== MMZ内存信息 ==="
    cat /proc/media-mem | head -5
fi
