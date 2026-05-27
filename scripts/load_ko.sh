#!/bin/bash
# Hi3516DV300 内核模块加载脚本
# 用于在开发板上加载MPP所需的内核模块

# 内存管理模块(mmz) - 配置MMZ内存区域
# 参数说明: anonymous,0,起始地址,大小
# 0x84000000起始,分配576MB给MMZ
insmod mmz.ko mmz=anonymous,0,0x84000000,576M anony=1,0,0x8FA00000,64M 2>/dev/null

# 系统基础模块
insmod hi3516cv500_sys.ko 2>/dev/null

# 视频缓存池
insmod hi3516cv500_vb.ko 2>/dev/null

# 视频输入(VI)模块 - 摄像头数据采集
insmod hi3516cv500_vi.ko 2>/dev/null

# 视频处理子系统(VPSS) - 图像缩放、裁剪等
insmod hi3516cv500_vpss.ko 2>/dev/null

# NNIE神经网络推理引擎
insmod hi3516cv500_nnie.ko 2>/dev/null

# 2D图形加速引擎(TDE) - 用于图像格式转换
insmod hi3516cv500_tde.ko 2>/dev/null

# 视频编码器(H.264/H.265/JPEG)
insmod hi3516cv500_venc.ko 2>/dev/null

# 视频输出(VO)
insmod hi3516cv500_vo.ko 2>/dev/null

echo "所有内核模块加载完成"
