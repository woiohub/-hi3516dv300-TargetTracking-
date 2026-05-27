/**
 * @file tracker.h
 * @brief 目标追踪模块头文件
 *
 * 基于IoU匹配的简单多目标追踪器
 */

#ifndef __TRACKER_H__
#define __TRACKER_H__

#include "common.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

/** 追踪器上下文 */
typedef struct hiTRACKER_CTX_S {
    TRACK_TARGET_S astTracks[TRACKER_MAX_TARGETS];  /* 追踪目标数组 */
    HI_U32 u32TrackNum;                             /* 当前追踪目标数 */
    HI_U32 u32NextId;                               /* 下一个可用ID */
    HI_BOOL bInitialized;                           /* 是否已初始化 */
} TRACKER_CTX_S;

/**
 * @brief 初始化追踪器
 */
HI_S32 TRACKER_Init(TRACKER_CTX_S* pstCtx);

/**
 * @brief 更新追踪状态
 *
 * 使用IoU匹配将当前帧检测结果与已有轨迹关联
 *
 * @param pstCtx 追踪器上下文
 * @param pstDetections 当前帧检测结果
 * @param pstTracks 输出追踪结果
 */
HI_S32 TRACKER_Update(TRACKER_CTX_S* pstCtx, const DETECTION_RESULTS_S* pstDetections,
    TRACK_RESULTS_S* pstTracks);

/**
 * @brief 反初始化追踪器
 */
HI_S32 TRACKER_Deinit(TRACKER_CTX_S* pstCtx);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif

#endif /* __TRACKER_H__ */
