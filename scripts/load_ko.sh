#!/bin/sh
# Hi3516DV300 内核模块加载脚本
# 适配GC2053传感器，与板级启动配置兼容
#
# 板级默认配置(从启动日志获取):
#   Linux内存: 128MB (0x80000000-0x87FFFFFF)
#   MMZ:       128MB (0x88000000-0x8FFFFFFF)
#   传感器:    imx327 (板级默认)
#
# 使用方法:
#   sh ./scripts/load_ko.sh
#
# 如果板级启动时sensor不是gc2053，需要先修改板级启动脚本:
#   1. 编辑开发板 /etc/init.d/ 中的模块加载脚本
#   2. 将 sensor0=imx327 改为 sensor0=gc2053
#   3. 重启开发板

#################### 可配置变量 ########################
KO_DIR="${KO_DIR:-/ko}"                       # 内核模块目录
SNS_TYPE0="${SNS_TYPE0:-gc2053}"              # 期望的传感器类型
mmz_start="${MMZ_START:-0x88000000}"          # MMZ起始地址(匹配板级默认)
mmz_size="${MMZ_SIZE:-128M}"                  # MMZ大小(匹配板级默认)
#######################################################

# 检查ko目录
if [ ! -d "$KO_DIR" ]; then
    echo "[ERROR] 内核模块目录不存在: $KO_DIR"
    echo "请先将SDK的 mpp/ko/ 目录复制到开发板:"
    echo "  scp -r <SDK_PATH>/smp/a7_linux/mpp/ko/ root@<board_ip>:/ko/"
    exit 1
fi

# 检查MMZ配置是否匹配
check_mmz()
{
    if [ ! -f /proc/media-mem ]; then
        return 1
    fi
    if grep -qi "0x88000000" /proc/media-mem 2>/dev/null; then
        return 0
    fi
    return 1
}

# 检查传感器类型
check_sensor()
{
    if grep -q "sensor0: $SNS_TYPE0" /proc/media-mem 2>/dev/null; then
        return 0
    fi
    # 检查dmesg中的传感器信息
    if dmesg 2>/dev/null | grep -q "${SNS_TYPE0}.*init.*OK\|${SNS_TYPE0}.*succ"; then
        return 0
    fi
    return 1
}

# 检查模块是否已加载
if grep -q "^hi_osal" /proc/modules 2>/dev/null; then
    if check_mmz; then
        echo "=== 内核模块已由板级启动脚本加载 ==="
        echo ""
        echo "  MMZ配置: $(grep 'PHYS(' /proc/media-mem 2>/dev/null | head -1 | grep -o 'PHYS([^)]*)')"
        echo ""

        # 检查传感器
        boot_sensor=$(dmesg 2>/dev/null | grep "sensor0:" | tail -1 | grep -o 'sensor0: [a-z0-9]*' | awk '{print $2}')
        if [ -n "$boot_sensor" ] && [ "$boot_sensor" != "$SNS_TYPE0" ]; then
            echo "  [WARN] 板级传感器为 $boot_sensor，但程序需要 $SNS_TYPE0"
            echo ""
            echo "  可能的问题:"
            echo "    - ISP初始化可能失败(GC2053驱动未加载)"
            echo "    - 视频采集可能异常"
            echo ""
            echo "  解决方案:"
            echo "    1. 编辑开发板启动脚本(如 /etc/init.d/S99mpp):"
            echo "       将 sensor0=$boot_sensor 改为 sensor0=$SNS_TYPE0"
            echo "    2. 重启开发板"
            echo ""
            echo "  如果ISP/视频采集工作正常，可忽略此警告。"
            echo ""
        fi

        echo "  MMZ使用情况:"
        tail -1 /proc/media-mem 2>/dev/null
        echo ""
        echo "  可以直接运行程序:"
        echo "    ./build/sample_target_tracking ./data/nnie_model/yolov3.wk 8080 ./web"
        exit 0
    fi
fi

# 模块未加载，执行加载
echo "=== 加载内核模块 ==="
echo "  传感器: $SNS_TYPE0"
echo "  MMZ: $mmz_start, $mmz_size"
echo "  模块目录: $KO_DIR"
echo ""

cd "$KO_DIR"

# 1. 系统配置模块(传感器类型配置)
echo "[1/9] sys_config.ko ..."
insmod sys_config.ko chip=hi3516dv300 sensors=sns0=$SNS_TYPE0,sns1=NULL, g_cmos_yuv_flag=0 || {
    echo "  [ERROR] sys_config.ko 加载失败"; exit 1; }

# 2. OS抽象层 + MMZ内存管理
echo "[2/9] hi_osal.ko (MMZ: $mmz_start, $mmz_size) ..."
insmod hi_osal.ko anony=1 mmz_allocator=hisi mmz=anonymous,0,$mmz_start,$mmz_size || {
    echo "  [ERROR] hi_osal.ko 加载失败"; exit 1; }

# 3. 基础驱动
echo "[3/9] hi3516cv500_base.ko ..."
insmod hi3516cv500_base.ko || { echo "  [ERROR] 加载失败"; exit 1; }

# 4. 系统模块
echo "[4/9] hi3516cv500_sys.ko ..."
insmod hi3516cv500_sys.ko || { echo "  [ERROR] 加载失败"; exit 1; }

# 5. 视频输入 + ISP + 传感器驱动
echo "[5/9] vi/isp/sensor ..."
insmod hi3516cv500_vi.ko || { echo "  [ERROR] 加载失败"; exit 1; }
insmod hi3516cv500_isp.ko || { echo "  [ERROR] 加载失败"; exit 1; }
insmod extdrv/hi_sensor_i2c.ko || { echo "  [ERROR] 加载失败"; exit 1; }
insmod extdrv/hi_sensor_spi.ko || { echo "  [ERROR] 加载失败"; exit 1; }
insmod hi_mipi_rx.ko || { echo "  [ERROR] 加载失败"; exit 1; }

# 6. 视频处理子系统
echo "[6/9] hi3516cv500_vpss.ko ..."
insmod hi3516cv500_vpss.ko || { echo "  [ERROR] 加载失败"; exit 1; }

# 7. NNIE神经网络推理引擎
echo "[7/9] hi3516cv500_nnie.ko ..."
insmod hi3516cv500_nnie.ko nnie_save_power=1 nnie_max_tskbuf_num=32 || {
    echo "  [ERROR] 加载失败"; exit 1; }

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

if [ -f /proc/media-mem ]; then
    echo ""
    echo "=== MMZ内存信息 ==="
    cat /proc/media-mem | head -5
fi
