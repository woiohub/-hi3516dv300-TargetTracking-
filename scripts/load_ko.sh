#!/bin/sh
# Hi3516DV300 内核模块加载脚本
# 基于SDK官方 load3516dv300 脚本改写，适配GC2053传感器
#
# 使用方法:
#   sh ./scripts/load_ko.sh          # 正常加载(如已加载则跳过)
#   sh ./scripts/load_ko.sh --force  # 强制卸载后重新加载(修改MMZ配置时使用)
#
# 注意: 需要先将SDK的 mpp/ko/ 目录复制到开发板 /ko/ 目录

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

unload_all()
{
    echo "=== 卸载已加载的内核模块 ==="
    # 按依赖反序卸载
    rmmod hi3516cv500_svprt 2>/dev/null
    rmmod hi3516cv500_ive 2>/dev/null
    rmmod hi3516cv500_hdmi 2>/dev/null
    rmmod hi3516cv500_vo 2>/dev/null
    rmmod hi3516cv500_jpege 2>/dev/null
    rmmod hi3516cv500_h265e 2>/dev/null
    rmmod hi3516cv500_h264e 2>/dev/null
    rmmod hi3516cv500_venc 2>/dev/null
    rmmod hi3516cv500_rc 2>/dev/null
    rmmod hi3516cv500_vedu 2>/dev/null
    rmmod hi3516cv500_chnl 2>/dev/null
    rmmod hi3516cv500_tde 2>/dev/null
    rmmod hi3516cv500_nnie 2>/dev/null
    rmmod hi3516cv500_vpss 2>/dev/null
    rmmod hi_mipi_rx 2>/dev/null
    rmmod extdrv/hi_sensor_spi 2>/dev/null
    rmmod hi_sensor_spi 2>/dev/null
    rmmod extdrv/hi_sensor_i2c 2>/dev/null
    rmmod hi_sensor_i2c 2>/dev/null
    rmmod hi3516cv500_isp 2>/dev/null
    rmmod hi3516cv500_vi 2>/dev/null
    rmmod hi3516cv500_sys 2>/dev/null
    rmmod hi3516cv500_base 2>/dev/null
    rmmod hi_osal 2>/dev/null
    rmmod sys_config 2>/dev/null
    echo "  卸载完成"
    echo ""
}

# 检查是否需要强制重载
if [ "$FORCE" -eq 1 ]; then
    unload_all
else
    # 检查hi_osal.ko是否已加载(代表所有模块已加载)
    if grep -q "^hi_osal" /proc/modules 2>/dev/null; then
        # 检查MMZ配置是否匹配
        current_mmz=$(cat /proc/media-mem 2>/dev/null | grep "PHYS(0x" | head -1)
        expected_start=$(echo "$mmz_start" | sed 's/0x/0x/' | tr 'a-f' 'A-F')
        if echo "$current_mmz" | grep -qi "$mmz_start"; then
            echo "=== 内核模块已加载，MMZ配置匹配 ==="
            echo "  如需重新加载请使用: sh $0 --force"
            echo ""
            cat /proc/media-mem | head -5
            exit 0
        else
            echo "[WARN] MMZ配置不匹配，需要重新加载"
            echo "  当前: $(echo "$current_mmz" | grep -o 'PHYS([^)]*)')"
            echo "  期望: PHYS($mmz_start, ...)"
            echo "  执行强制重载..."
            echo ""
            unload_all
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
    echo ""
    echo "  如MMZ配置不正确，请使用 --force 重新加载:"
    echo "  sh $0 --force"
fi
