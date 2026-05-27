/**
 * @file tracker.c
 * @brief 目标追踪模块实现
 *
 * 基于IoU匹配的简单多目标追踪算法:
 * 1. 对每个已有轨迹，在当前帧检测中找IoU最大的匹配
 * 2. 使用贪心策略分配匹配(优先高IoU)
 * 3. 未匹配的检测创建新轨迹
 * 4. 连续N帧未匹配的轨迹删除
 */

#include "tracker.h"

HI_S32 TRACKER_Init(TRACKER_CTX_S* pstCtx)
{
    memset(pstCtx, 0, sizeof(TRACKER_CTX_S));
    pstCtx->u32NextId = 1;
    pstCtx->bInitialized = HI_TRUE;
    LOG_INFO("目标追踪器初始化完成");
    return HI_SUCCESS;
}

HI_S32 TRACKER_Update(TRACKER_CTX_S* pstCtx, const DETECTION_RESULTS_S* pstDetections,
    TRACK_RESULTS_S* pstTracks)
{
    HI_U32 i, j;
    HI_BOOL abDetMatched[YOLO_MAX_ROI_NUM] = {0};  /* 检测是否已匹配 */
    HI_BOOL abTrackMatched[TRACKER_MAX_TARGETS] = {0}; /* 轨迹是否已匹配 */

    if (!pstCtx->bInitialized) {
        LOG_ERROR("追踪器未初始化!");
        return HI_FAILURE;
    }

    /* 步骤1: 计算IoU矩阵并进行贪心匹配
     * 对每个轨迹，找到IoU最大的未匹配检测 */
    for (i = 0; i < pstCtx->u32TrackNum; i++) {
        TRACK_TARGET_S* pstTrack = &pstCtx->astTracks[i];
        HI_FLOAT f32BestIoU = TRACKER_MIN_IOU_THRESH;
        HI_S32 s32BestIdx = -1;

        /* 将轨迹转为检测格式以计算IoU */
        DETECTION_RESULT_S stTrackDet;
        stTrackDet.f32X = pstTrack->f32X - pstTrack->f32W / 2;
        stTrackDet.f32Y = pstTrack->f32Y - pstTrack->f32H / 2;
        stTrackDet.f32W = pstTrack->f32W;
        stTrackDet.f32H = pstTrack->f32H;

        for (j = 0; j < pstDetections->u32ResultNum; j++) {
            if (abDetMatched[j]) continue; /* 已被其他轨迹匹配 */

            /* 只匹配同类别的目标 */
            if (pstTrack->u32ClassId != pstDetections->astResults[j].u32ClassId) continue;

            HI_FLOAT f32IoU = calc_iou(&stTrackDet, &pstDetections->astResults[j]);
            if (f32IoU > f32BestIoU) {
                f32BestIoU = f32IoU;
                s32BestIdx = j;
            }
        }

        if (s32BestIdx >= 0) {
            /* 匹配成功: 更新轨迹 */
            DETECTION_RESULT_S* pstDet = &pstDetections->astResults[s32BestIdx];
            pstTrack->f32X = pstDet->f32X + pstDet->f32W / 2;
            pstTrack->f32Y = pstDet->f32Y + pstDet->f32H / 2;
            pstTrack->f32W = pstDet->f32W;
            pstTrack->f32H = pstDet->f32H;
            pstTrack->f32Confidence = pstDet->f32Confidence;
            pstTrack->u32Age++;
            pstTrack->u32MissCount = 0;

            abDetMatched[s32BestIdx] = HI_TRUE;
            abTrackMatched[i] = HI_TRUE;
        }
    }

    /* 步骤2: 处理未匹配的轨迹(增加miss计数) */
    for (i = 0; i < pstCtx->u32TrackNum; i++) {
        if (!abTrackMatched[i]) {
            pstCtx->astTracks[i].u32MissCount++;
        }
    }

    /* 步骤3: 删除长期未匹配的轨迹 */
    HI_U32 u32NewTrackNum = 0;
    for (i = 0; i < pstCtx->u32TrackNum; i++) {
        if (pstCtx->astTracks[i].u32MissCount <= TRACKER_MAX_MISS_COUNT) {
            if (u32NewTrackNum != i) {
                pstCtx->astTracks[u32NewTrackNum] = pstCtx->astTracks[i];
            }
            u32NewTrackNum++;
        }
    }
    pstCtx->u32TrackNum = u32NewTrackNum;

    /* 步骤4: 为未匹配的检测创建新轨迹 */
    for (j = 0; j < pstDetections->u32ResultNum; j++) {
        if (abDetMatched[j]) continue;
        if (pstCtx->u32TrackNum >= TRACKER_MAX_TARGETS) break;

        DETECTION_RESULT_S* pstDet = &pstDetections->astResults[j];
        TRACK_TARGET_S* pstNewTrack = &pstCtx->astTracks[pstCtx->u32TrackNum];

        pstNewTrack->u32TrackId = pstCtx->u32NextId++;
        pstNewTrack->f32X = pstDet->f32X + pstDet->f32W / 2;
        pstNewTrack->f32Y = pstDet->f32Y + pstDet->f32H / 2;
        pstNewTrack->f32W = pstDet->f32W;
        pstNewTrack->f32H = pstDet->f32H;
        pstNewTrack->f32Confidence = pstDet->f32Confidence;
        pstNewTrack->u32ClassId = pstDet->u32ClassId;
        pstNewTrack->u32Age = 1;
        pstNewTrack->u32MissCount = 0;

        pstCtx->u32TrackNum++;
    }

    /* 步骤5: 输出追踪结果 */
    pstTracks->u32TargetNum = pstCtx->u32TrackNum;
    for (i = 0; i < pstCtx->u32TrackNum; i++) {
        pstTracks->astTargets[i] = pstCtx->astTracks[i];
    }

    return HI_SUCCESS;
}

HI_S32 TRACKER_Deinit(TRACKER_CTX_S* pstCtx)
{
    pstCtx->bInitialized = HI_FALSE;
    pstCtx->u32TrackNum = 0;
    LOG_INFO("目标追踪器反初始化完成");
    return HI_SUCCESS;
}
