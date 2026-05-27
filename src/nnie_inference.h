/**
 * @file nnie_inference.h
 * @brief NNIE推理模块头文件
 *
 * 负责YOLOv3模型的加载、推理和后处理
 */

#ifndef __NNIE_INFERENCE_H__
#define __NNIE_INFERENCE_H__

#include "common.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

/** NNIE推理模块上下文 */
typedef struct hiNNIE_CTX_S {
    SAMPLE_SVP_NNIE_MODEL_S stModel;        /* NNIE模型 */
    SAMPLE_SVP_NNIE_PARAM_S stNnieParam;    /* NNIE推理参数 */
    HI_BOOL bInitialized;                   /* 是否已初始化 */
} NNIE_CTX_S;

/**
 * @brief 初始化NNIE推理模块
 *
 * 加载YOLOv3 .wk模型，分配推理所需的内存缓冲区
 */
HI_S32 NNIE_Init(NNIE_CTX_S* pstCtx, const HI_CHAR* pszModelPath);

/**
 * @brief 执行YOLOv3推理
 *
 * 将BGR图像数据送入NNIE引擎进行前向推理，然后执行YOLOv3后处理
 */
HI_S32 NNIE_Forward(NNIE_CTX_S* pstCtx, const HI_U8* pu8BgrData,
    DETECTION_RESULTS_S* pstResults);

/**
 * @brief 反初始化NNIE推理模块
 */
HI_S32 NNIE_Deinit(NNIE_CTX_S* pstCtx);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif

#endif /* __NNIE_INFERENCE_H__ */
