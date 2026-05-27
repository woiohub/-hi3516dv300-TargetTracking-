/**
 * @file video_capture.h
 * @brief 视频采集模块头文件
 *
 * 负责初始化VI/VPSS通路，采集YUV帧并转换为BGR格式供NNIE推理使用
 */

#ifndef __VIDEO_CAPTURE_H__
#define __VIDEO_CAPTURE_H__

#include "common.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

/** 视频采集模块上下文 */
typedef struct hiVIDEO_CAPTURE_CTX_S {
    VI_DEV ViDev;               /* VI设备号 */
    VI_PIPE ViPipe;             /* VI管道号 */
    VI_CHN ViChn;               /* VI通道号 */
    VPSS_GRP VpssGrp;           /* VPSS组号 */
    VPSS_CHN VpssChn;           /* VPSS通道号(用于NNIE推理) */
    SIZE_S stOrigSize;          /* 原始图像尺寸(1920x1080) */
    SIZE_S stNnieSize;          /* NNIE输入尺寸(416x416) */
    HI_BOOL bStarted;           /* 是否已启动 */
} VIDEO_CAPTURE_CTX_S;

/**
 * @brief 初始化视频采集模块
 *
 * 包括: 系统初始化 -> VB配置 -> VI启动 -> VPSS启动
 *
 * @param pstCtx 视频采集上下文
 * @return HI_SUCCESS成功, 其他失败
 */
HI_S32 VIDEO_CAPTURE_Init(VIDEO_CAPTURE_CTX_S* pstCtx);

/**
 * @brief 获取一帧BGR数据(供NNIE推理)
 *
 * 从VPSS通道获取YUV帧，转换为BGR planar格式，填充到NNIE输入缓冲区
 *
 * @param pstCtx 视频采集上下文
 * @param pu8BgrBuf BGR数据输出缓冲区(需要预分配 416*416*3 字节)
 * @param u32BufSize 缓冲区大小
 * @return HI_SUCCESS成功, 其他失败
 */
HI_S32 VIDEO_CAPTURE_GetFrame(VIDEO_CAPTURE_CTX_S* pstCtx, HI_U8* pu8BgrBuf, HI_U32 u32BufSize);

/**
 * @brief 反初始化视频采集模块
 *
 * @param pstCtx 视频采集上下文
 * @return HI_SUCCESS成功, 其他失败
 */
HI_S32 VIDEO_CAPTURE_Deinit(VIDEO_CAPTURE_CTX_S* pstCtx);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif

#endif /* __VIDEO_CAPTURE_H__ */
