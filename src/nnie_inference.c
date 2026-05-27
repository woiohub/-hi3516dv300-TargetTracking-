/**
 * @file nnie_inference.c
 * @brief NNIE推理模块实现
 *
 * 实现YOLOv3模型在海思NNIE引擎上的加载、推理和后处理
 *
 * YOLOv3输出格式:
 *   3个检测尺度，每个尺度一个输出Blob
 *   - Scale 0: [1, 255, 13, 13] - 大目标检测
 *   - Scale 1: [1, 255, 26, 26] - 中目标检测
 *   - Scale 2: [1, 255, 52, 52] - 小目标检测
 *
 *   255 = 3 anchors * (5 + 80 classes)
 *   每个anchor: tx, ty, tw, th, objectness, class1...class80
 *
 * 推理流程:
 * 1. 读取.wk模型文件到内存
 * 2. HI_MPI_SVP_NNIE_LoadModel()加载模型
 * 3. 分配Task Buffer和Tmp Buffer
 * 4. 将BGR图像数据拷贝到NNIE输入Blob
 * 5. HI_MPI_SVP_NNIE_Forward()执行前向推理
 * 6. YOLOv3后处理: anchor解码 + sigmoid + NMS
 */

#include "nnie_inference.h"
#include <math.h>

/*========================================================================
 *                        YOLOv3 辅助函数
 *========================================================================*/

/** Sigmoid函数 */
static HI_FLOAT sigmoid(HI_FLOAT x)
{
    return 1.0f / (1.0f + expf(-x));
}

/** 计算两个bbox的IoU(输入为中心点+宽高格式) */
static HI_FLOAT bbox_iou_ch(HI_FLOAT cx1, HI_FLOAT cy1, HI_FLOAT w1, HI_FLOAT h1,
                             HI_FLOAT cx2, HI_FLOAT cy2, HI_FLOAT w2, HI_FLOAT h2)
{
    HI_FLOAT x1_min = cx1 - w1 * 0.5f, y1_min = cy1 - h1 * 0.5f;
    HI_FLOAT x1_max = cx1 + w1 * 0.5f, y1_max = cy1 + h1 * 0.5f;
    HI_FLOAT x2_min = cx2 - w2 * 0.5f, y2_min = cy2 - h2 * 0.5f;
    HI_FLOAT x2_max = cx2 + w2 * 0.5f, y2_max = cy2 + h2 * 0.5f;

    HI_FLOAT xi_min = (x1_min > x2_min) ? x1_min : x2_min;
    HI_FLOAT yi_min = (y1_min > y2_min) ? y1_min : y2_min;
    HI_FLOAT xi_max = (x1_max < x2_max) ? x1_max : x2_max;
    HI_FLOAT yi_max = (y1_max < y2_max) ? y1_max : y2_max;

    if (xi_max <= xi_min || yi_max <= yi_min) return 0.0f;

    HI_FLOAT inter = (xi_max - xi_min) * (yi_max - yi_min);
    HI_FLOAT area1 = w1 * h1;
    HI_FLOAT area2 = w2 * h2;

    return inter / (area1 + area2 - inter);
}

/** 单个检测框结构(用于后处理) */
typedef struct {
    HI_FLOAT f32Cx;
    HI_FLOAT f32Cy;
    HI_FLOAT f32W;
    HI_FLOAT f32H;
    HI_FLOAT f32Score;
    HI_U32 u32ClassIdx;
    HI_U32 u32Mask;         /* NMS标记: 0=保留, 1=抑制 */
} YOLO_BBOX_S;

/*========================================================================
 *                        NNIE 模块实现
 *========================================================================*/

HI_S32 NNIE_Init(NNIE_CTX_S* pstCtx, const HI_CHAR* pszModelPath)
{
    HI_S32 s32Ret;
    FILE* pFile = NULL;
    HI_U32 u32FileSize;

    LOG_INFO("NNIE推理模块初始化...");
    LOG_INFO("模型文件: %s", pszModelPath);

    memset(pstCtx, 0, sizeof(NNIE_CTX_S));
    pstCtx->bInitialized = HI_FALSE;

    /* 步骤1: 读取模型文件 */
    pFile = fopen(pszModelPath, "rb");
    if (NULL == pFile) {
        LOG_ERROR("打开模型文件失败: %s", pszModelPath);
        return HI_FAILURE;
    }
    fseek(pFile, 0, SEEK_END);
    u32FileSize = ftell(pFile);
    fseek(pFile, 0, SEEK_SET);

    s32Ret = SAMPLE_COMM_SVP_MallocMem("NNIE_MODEL", NULL,
        (HI_U64*)&pstCtx->stModel.stModelBuf.u64PhyAddr,
        (void**)&pstCtx->stModel.stModelBuf.u64VirAddr,
        u32FileSize);
    if (HI_SUCCESS != s32Ret) {
        LOG_ERROR("模型内存分配失败! s32Ret=0x%x", s32Ret);
        fclose(pFile);
        return s32Ret;
    }

    fread((HI_VOID*)(HI_UL)pstCtx->stModel.stModelBuf.u64VirAddr, 1, u32FileSize, pFile);
    pstCtx->stModel.stModelBuf.u32Size = u32FileSize;
    fclose(pFile);
    LOG_INFO("模型文件加载成功, 大小: %u bytes", u32FileSize);

    /* 步骤2: 加载NNIE模型 */
    s32Ret = HI_MPI_SVP_NNIE_LoadModel(&pstCtx->stModel.stModelBuf, &pstCtx->stModel.stModel);
    if (HI_SUCCESS != s32Ret) {
        LOG_ERROR("NNIE加载模型失败! s32Ret=0x%x", s32Ret);
        return s32Ret;
    }
    LOG_INFO("NNIE模型加载成功, 网络段数: %u", pstCtx->stModel.stModel.u32NetSegNum);

    /* 步骤3: 初始化NNIE推理参数 */
    SAMPLE_SVP_NNIE_CFG_S stNnieCfg = {0};
    stNnieCfg.u32MaxInputNum = 1;
    stNnieCfg.u32MaxRoiNum = 0;
    stNnieCfg.aenNnieCoreId[0] = SVP_NNIE_ID_0;

    pstCtx->stNnieParam.pstModel = &pstCtx->stModel.stModel;
    s32Ret = SAMPLE_COMM_SVP_NNIE_ParamInit(&stNnieCfg, &pstCtx->stNnieParam);
    if (HI_SUCCESS != s32Ret) {
        LOG_ERROR("NNIE参数初始化失败! s32Ret=0x%x", s32Ret);
        return s32Ret;
    }

    /* 打印输出Blob信息 */
    LOG_INFO("输出Blob数量: %u", pstCtx->stNnieParam.astForwardCtrl[0].u32DstNum);
    for (HI_U32 i = 0; i < pstCtx->stNnieParam.astForwardCtrl[0].u32DstNum; i++) {
        SVP_DST_BLOB_S* pstDst = &pstCtx->stNnieParam.astSegData[0].astDst[i];
        LOG_INFO("  输出Blob[%u]: 类型=%u, W=%u, H=%u, Ch=%u, Stride=%u",
            i, pstDst->enType,
            pstDst->unShape.stWhc.u32Width,
            pstDst->unShape.stWhc.u32Height,
            pstDst->unShape.stWhc.u32Chn,
            pstDst->u32Stride);
    }

    pstCtx->bInitialized = HI_TRUE;
    LOG_INFO("NNIE推理模块初始化完成");

    return HI_SUCCESS;
}

/**
 * @brief YOLOv3后处理
 *
 * 处理3个尺度的输出Blob，解码anchor box，应用sigmoid，执行NMS
 *
 * NNIE输出Blob格式(每个尺度):
 *   [1, C, H, W] 其中 C = 3 * (5 + 80) = 255, H = W = grid_size
 *   数据布局: CHW (channel-first)
 *   Stride: W维度按32字节对齐
 */
static HI_S32 NNIE_Yolov3_PostProcess(NNIE_CTX_S* pstCtx, DETECTION_RESULTS_S* pstResults)
{
    HI_U32 u32DstNum = pstCtx->stNnieParam.astForwardCtrl[0].u32DstNum;
    YOLO_BBOX_S astBbox[YOLO_MAX_DET_NUM];
    HI_U32 u32BboxNum = 0;

    /* 遍历3个检测尺度 */
    for (HI_U32 s = 0; s < YOLO3_SCALE_NUM && s < u32DstNum; s++) {
        SVP_DST_BLOB_S* pstDst = &pstCtx->stNnieParam.astSegData[0].astDst[s];
        HI_S32* ps32Data = (HI_S32*)(HI_UL)pstDst->u64VirAddr;

        HI_U32 u32GridW = pstDst->unShape.stWhc.u32Width;
        HI_U32 u32GridH = pstDst->unShape.stWhc.u32Height;
        HI_U32 u32Chn = pstDst->unShape.stWhc.u32Chn;
        HI_U32 u32Stride = pstDst->u32Stride / sizeof(HI_S32);

        LOG_DEBUG("Scale %u: Grid=%ux%u, Ch=%u, Stride=%u",
            s, u32GridW, u32GridH, u32Chn, u32Stride);

        /* 验证通道数: 应为 3 * 85 = 255 */
        if (u32Chn != YOLO3_ANCHOR_NUM * YOLO3_DET_DIM) {
            LOG_ERROR("Scale %u 通道数异常: %u (期望%u)",
                s, u32Chn, YOLO3_ANCHOR_NUM * YOLO3_DET_DIM);
            continue;
        }

        /* 解析每个grid cell的每个anchor */
        for (HI_U32 h = 0; h < u32GridH; h++) {
            for (HI_U32 w = 0; w < u32GridW; w++) {
                for (HI_U32 a = 0; a < YOLO3_ANCHOR_NUM; a++) {
                    /* 计算数据偏移: [anchor * det_dim + feature][h][w] */
                    HI_U32 u32BaseOffset = a * YOLO3_DET_DIM * u32GridH * u32Stride
                                         + h * u32Stride + w;

                    HI_FLOAT f32Tx = (HI_FLOAT)ps32Data[u32BaseOffset + 0 * u32GridH * u32Stride] / SAMPLE_SVP_QUANT_BASE;
                    HI_FLOAT f32Ty = (HI_FLOAT)ps32Data[u32BaseOffset + 1 * u32GridH * u32Stride] / SAMPLE_SVP_QUANT_BASE;
                    HI_FLOAT f32Tw = (HI_FLOAT)ps32Data[u32BaseOffset + 2 * u32GridH * u32Stride] / SAMPLE_SVP_QUANT_BASE;
                    HI_FLOAT f32Th = (HI_FLOAT)ps32Data[u32BaseOffset + 3 * u32GridH * u32Stride] / SAMPLE_SVP_QUANT_BASE;
                    HI_FLOAT f32Obj = (HI_FLOAT)ps32Data[u32BaseOffset + 4 * u32GridH * u32Stride] / SAMPLE_SVP_QUANT_BASE;

                    /* objectness置信度过滤 */
                    HI_FLOAT f32ObjScore = sigmoid(f32Obj);
                    if (f32ObjScore < YOLO_CONF_THRESH) continue;

                    /* 找最大类别分数 */
                    HI_FLOAT f32MaxClassScore = 0;
                    HI_U32 u32MaxClassIdx = 0;
                    for (HI_U32 c = 0; c < YOLO_CLASS_NUM; c++) {
                        HI_FLOAT f32ClassLogit = (HI_FLOAT)ps32Data[
                            u32BaseOffset + (5 + c) * u32GridH * u32Stride] / SAMPLE_SVP_QUANT_BASE;
                        HI_FLOAT f32ClassScore = sigmoid(f32ClassLogit);
                        if (f32ClassScore > f32MaxClassScore) {
                            f32MaxClassScore = f32ClassScore;
                            u32MaxClassIdx = c;
                        }
                    }

                    /* 最终置信度 = objectness * class_score */
                    HI_FLOAT f32FinalScore = f32ObjScore * f32MaxClassScore;
                    if (f32FinalScore < YOLO_CONF_THRESH) continue;

                    /* Anchor解码: 转换为绝对像素坐标(相对于416x416输入) */
                    HI_FLOAT f32AnchorW = YOLO3_ANCHORS[s][a][0];
                    HI_FLOAT f32AnchorH = YOLO3_ANCHORS[s][a][1];

                    HI_FLOAT f32Cx = (sigmoid(f32Tx) + (HI_FLOAT)w) * ((HI_FLOAT)NNIE_INPUT_WIDTH / u32GridW);
                    HI_FLOAT f32Cy = (sigmoid(f32Ty) + (HI_FLOAT)h) * ((HI_FLOAT)NNIE_INPUT_HEIGHT / u32GridH);
                    HI_FLOAT f32W = expf(f32Tw) * f32AnchorW;
                    HI_FLOAT f32H = expf(f32Th) * f32AnchorH;

                    /* 过滤过小的框 */
                    if (f32W < 1.0f || f32H < 1.0f) continue;

                    /* 钳位到图像范围 */
                    if (f32Cx < 0 || f32Cx > NNIE_INPUT_WIDTH) continue;
                    if (f32Cy < 0 || f32Cy > NNIE_INPUT_HEIGHT) continue;

                    if (u32BboxNum >= YOLO_MAX_DET_NUM) break;

                    astBbox[u32BboxNum].f32Cx = f32Cx;
                    astBbox[u32BboxNum].f32Cy = f32Cy;
                    astBbox[u32BboxNum].f32W = f32W;
                    astBbox[u32BboxNum].f32H = f32H;
                    astBbox[u32BboxNum].f32Score = f32FinalScore;
                    astBbox[u32BboxNum].u32ClassIdx = u32MaxClassIdx;
                    astBbox[u32BboxNum].u32Mask = 0;
                    u32BboxNum++;
                }
                if (u32BboxNum >= YOLO_MAX_DET_NUM) break;
            }
            if (u32BboxNum >= YOLO_MAX_DET_NUM) break;
        }
    }

    LOG_DEBUG("NMS前候选框数: %u", u32BboxNum);

    /* NMS: 非极大值抑制 */
    for (HI_U32 i = 0; i < u32BboxNum; i++) {
        if (astBbox[i].u32Mask) continue;
        for (HI_U32 j = i + 1; j < u32BboxNum; j++) {
            if (astBbox[j].u32Mask) continue;
            if (astBbox[i].u32ClassIdx != astBbox[j].u32ClassIdx) continue;

            HI_FLOAT f32IoU = bbox_iou_ch(
                astBbox[i].f32Cx, astBbox[i].f32Cy, astBbox[i].f32W, astBbox[i].f32H,
                astBbox[j].f32Cx, astBbox[j].f32Cy, astBbox[j].f32W, astBbox[j].f32H);

            if (f32IoU > YOLO_NMS_THRESH) {
                astBbox[j].u32Mask = 1;
            }
        }
    }

    /* 收集NMS后的结果 */
    pstResults->u32ResultNum = 0;
    pstResults->u64Timestamp = get_timestamp_us();

    for (HI_U32 i = 0; i < u32BboxNum && pstResults->u32ResultNum < YOLO_MAX_ROI_NUM; i++) {
        if (astBbox[i].u32Mask) continue;

        DETECTION_RESULT_S* pstDet = &pstResults->astResults[pstResults->u32ResultNum];
        pstDet->u32ClassId = astBbox[i].u32ClassIdx;
        pstDet->f32Confidence = astBbox[i].f32Score;

        /* 转换为角点坐标(像素) */
        pstDet->f32X = astBbox[i].f32Cx - astBbox[i].f32W * 0.5f;
        pstDet->f32Y = astBbox[i].f32Cy - astBbox[i].f32H * 0.5f;
        pstDet->f32W = astBbox[i].f32W;
        pstDet->f32H = astBbox[i].f32H;

        if (pstDet->f32X < 0) pstDet->f32X = 0;
        if (pstDet->f32Y < 0) pstDet->f32Y = 0;

        pstResults->u32ResultNum++;
    }

    return HI_SUCCESS;
}

HI_S32 NNIE_Forward(NNIE_CTX_S* pstCtx, const HI_U8* pu8BgrData, DETECTION_RESULTS_S* pstResults)
{
    HI_S32 s32Ret;
    HI_BOOL bFinish = HI_FALSE;
    SVP_NNIE_HANDLE hSvpNnieHandle = 0;

    if (!pstCtx->bInitialized) {
        LOG_ERROR("NNIE模块未初始化!");
        return HI_FAILURE;
    }

    /* 步骤1: 将BGR数据拷贝到NNIE输入Blob */
    HI_U8* pu8InputVirAddr = (HI_U8*)(HI_UL)pstCtx->stNnieParam.astSegData[0].astSrc[0].u64VirAddr;
    HI_U32 u32InputStride = pstCtx->stNnieParam.astSegData[0].astSrc[0].u32Stride;
    HI_U32 u32PlaneSize = NNIE_INPUT_WIDTH * NNIE_INPUT_HEIGHT;

    for (HI_U32 c = 0; c < 3; c++) {
        for (HI_U32 h = 0; h < NNIE_INPUT_HEIGHT; h++) {
            memcpy(pu8InputVirAddr + h * u32InputStride + c * u32PlaneSize,
                   pu8BgrData + c * u32PlaneSize + h * NNIE_INPUT_WIDTH,
                   NNIE_INPUT_WIDTH);
        }
    }

    /* 刷新缓存 */
    SAMPLE_COMM_SVP_FlushCache(
        pstCtx->stNnieParam.astSegData[0].astSrc[0].u64PhyAddr,
        (HI_VOID*)(HI_UL)pstCtx->stNnieParam.astSegData[0].astSrc[0].u64VirAddr,
        u32InputStride * NNIE_INPUT_HEIGHT * 3);

    SAMPLE_COMM_SVP_FlushCache(
        pstCtx->stNnieParam.stTaskBuf.u64PhyAddr,
        (HI_VOID*)(HI_UL)pstCtx->stNnieParam.stTaskBuf.u64VirAddr,
        pstCtx->stNnieParam.stTaskBuf.u32Size);

    /* 步骤2: 执行NNIE前向推理 */
    s32Ret = HI_MPI_SVP_NNIE_Forward(
        &hSvpNnieHandle,
        pstCtx->stNnieParam.astSegData[0].astSrc,
        &pstCtx->stModel.stModel,
        pstCtx->stNnieParam.astSegData[0].astDst,
        &pstCtx->stNnieParam.astForwardCtrl[0],
        HI_TRUE);
    if (HI_SUCCESS != s32Ret) {
        LOG_ERROR("NNIE Forward失败! s32Ret=0x%x", s32Ret);
        return s32Ret;
    }

    s32Ret = HI_MPI_SVP_NNIE_Query(
        pstCtx->stNnieParam.astForwardCtrl[0].enNnieId,
        hSvpNnieHandle, &bFinish, HI_TRUE);
    if (HI_SUCCESS != s32Ret || !bFinish) {
        LOG_ERROR("NNIE Query失败! s32Ret=0x%x bFinish=%d", s32Ret, bFinish);
        return s32Ret;
    }

    /* 步骤3: YOLOv3后处理 */
    s32Ret = NNIE_Yolov3_PostProcess(pstCtx, pstResults);
    if (HI_SUCCESS != s32Ret) {
        LOG_ERROR("YOLOv3后处理失败! s32Ret=0x%x", s32Ret);
        return s32Ret;
    }

    return HI_SUCCESS;
}

HI_S32 NNIE_Deinit(NNIE_CTX_S* pstCtx)
{
    if (!pstCtx->bInitialized) {
        return HI_SUCCESS;
    }

    LOG_INFO("NNIE推理模块反初始化...");

    SAMPLE_COMM_SVP_NNIE_ParamDeinit(&pstCtx->stNnieParam);
    SAMPLE_COMM_SVP_NNIE_UnloadModel(&pstCtx->stModel);

    pstCtx->bInitialized = HI_FALSE;
    LOG_INFO("NNIE推理模块反初始化完成");

    return HI_SUCCESS;
}
