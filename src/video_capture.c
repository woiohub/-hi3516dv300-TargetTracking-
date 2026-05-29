/**
 * @file video_capture.c
 * @brief 视频采集模块实现
 *
 * 实现GC2053摄像头的视频采集流程:
 * GC2053 -> MIPI -> VI -> VPSS -> YUV帧 -> BGR转换 -> NNIE输入
 *
 * 参考: SDK sample/vio/smp/sample_vio.c
 */

#include "video_capture.h"

/*========================================================================
 *                    NV21(YUV420SP) 转 BGR Planar
 *========================================================================*/

/**
 * @brief NV21格式转BGR Planar格式
 *
 * NV21内存布局: YYYYYYYY VUVU (Y全量, UV交错排列)
 * BGR Planar布局: BBBB...GGG...RRR... (三通道分离)
 *
 * @param pu8Yuv 输入NV21数据
 * @param pu8Bgr 输出BGR Planar数据
 * @param u32Width 图像宽度
 * @param u32Height 图像高度
 * @param u32Stride Y数据行跨度(可能大于宽度以对齐)
 */
static HI_VOID NV21_To_BGR_Planar(const HI_U8* pu8Yuv, HI_U8* pu8Bgr,
    HI_U32 u32Width, HI_U32 u32Height, HI_U32 u32Stride)
{
    HI_U32 i, j;
    const HI_U8* pu8Y = pu8Yuv;
    const HI_U8* pu8VU = pu8Yuv + u32Stride * u32Height;
    HI_U8* pu8B = pu8Bgr;
    HI_U8* pu8G = pu8Bgr + u32Width * u32Height;
    HI_U8* pu8R = pu8Bgr + u32Width * u32Height * 2;

    for (i = 0; i < u32Height; i++) {
        for (j = 0; j < u32Width; j++) {
            /* 取YUV值 */
            HI_S32 s32Y = pu8Y[i * u32Stride + j];
            HI_S32 s32V = pu8VU[(i / 2) * u32Stride + (j / 2) * 2];
            HI_S32 s32U = pu8VU[(i / 2) * u32Stride + (j / 2) * 2 + 1];

            /* YUV -> BGR转换公式(ITU-R BT.601) */
            /* R = Y + 1.402 * (V - 128) */
            /* G = Y - 0.344136 * (U - 128) - 0.714136 * (V - 128) */
            /* B = Y + 1.772 * (U - 128) */
            HI_S32 s32R = s32Y + ((359 * (s32V - 128)) >> 8);
            HI_S32 s32G = s32Y - ((88 * (s32U - 128) + 183 * (s32V - 128)) >> 8);
            HI_S32 s32B = s32Y + ((454 * (s32U - 128)) >> 8);

            /* 钳位到[0, 255] */
            pu8R[i * u32Width + j] = (HI_U8)(s32R < 0 ? 0 : (s32R > 255 ? 255 : s32R));
            pu8G[i * u32Width + j] = (HI_U8)(s32G < 0 ? 0 : (s32G > 255 ? 255 : s32G));
            pu8B[i * u32Width + j] = (HI_U8)(s32B < 0 ? 0 : (s32B > 255 ? 255 : s32B));
        }
    }
}

/*========================================================================
 *                         视频采集模块实现
 *========================================================================*/

HI_S32 VIDEO_CAPTURE_Init(VIDEO_CAPTURE_CTX_S* pstCtx)
{
    HI_S32 s32Ret;
    VB_CONFIG_S stVbConf;
    SIZE_S stSize;
    PIC_SIZE_E enPicSize;
    HI_U32 u32BlkSize;

    SAMPLE_VI_CONFIG_S stViConfig;
    WDR_MODE_E enWDRMode = WDR_MODE_NONE;
    DYNAMIC_RANGE_E enDynamicRange = DYNAMIC_RANGE_SDR8;
    PIXEL_FORMAT_E enPixFormat = PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    VIDEO_FORMAT_E enVideoFormat = VIDEO_FORMAT_LINEAR;
    COMPRESS_MODE_E enCompressMode = COMPRESS_MODE_NONE;
    VI_VPSS_MODE_E enMastPipeMode = VI_ONLINE_VPSS_ONLINE;

    VPSS_GRP_ATTR_S stVpssGrpAttr;
    VPSS_CHN_ATTR_S astVpssChnAttr[VPSS_MAX_PHY_CHN_NUM];
    HI_BOOL abChnEnable[VPSS_MAX_PHY_CHN_NUM] = {0};

    LOG_INFO("视频采集模块初始化...");

    /* 初始化上下文 */
    pstCtx->ViDev = 0;
    pstCtx->ViPipe = 0;
    pstCtx->ViChn = 0;
    pstCtx->VpssGrp = 0;
    pstCtx->VpssChn = VPSS_CHN0;  /* 通道0: 原始尺寸(软件缩放到NNIE尺寸) */
    pstCtx->stNnieSize.u32Width = NNIE_INPUT_WIDTH;
    pstCtx->stNnieSize.u32Height = NNIE_INPUT_HEIGHT;
    pstCtx->bStarted = HI_FALSE;

    /* 步骤1: 获取传感器信息 */
    SAMPLE_COMM_VI_GetSensorInfo(&stViConfig);
    stViConfig.s32WorkingViNum = 1;
    stViConfig.as32WorkingViId[0] = 0;
    stViConfig.astViInfo[0].stSnsInfo.MipiDev = pstCtx->ViDev;
    stViConfig.astViInfo[0].stSnsInfo.s32BusId = 0;
    stViConfig.astViInfo[0].stDevInfo.ViDev = pstCtx->ViDev;
    stViConfig.astViInfo[0].stDevInfo.enWDRMode = enWDRMode;
    stViConfig.astViInfo[0].stPipeInfo.enMastPipeMode = enMastPipeMode;
    stViConfig.astViInfo[0].stPipeInfo.aPipe[0] = pstCtx->ViPipe;
    stViConfig.astViInfo[0].stPipeInfo.aPipe[1] = -1;
    stViConfig.astViInfo[0].stPipeInfo.aPipe[2] = -1;
    stViConfig.astViInfo[0].stPipeInfo.aPipe[3] = -1;
    stViConfig.astViInfo[0].stChnInfo.ViChn = pstCtx->ViChn;
    stViConfig.astViInfo[0].stChnInfo.enPixFormat = enPixFormat;
    stViConfig.astViInfo[0].stChnInfo.enDynamicRange = enDynamicRange;
    stViConfig.astViInfo[0].stChnInfo.enVideoFormat = enVideoFormat;
    stViConfig.astViInfo[0].stChnInfo.enCompressMode = enCompressMode;

    /* 步骤2: 获取图像尺寸 */
    s32Ret = SAMPLE_COMM_VI_GetSizeBySensor(
        stViConfig.astViInfo[0].stSnsInfo.enSnsType, &enPicSize);
    if (HI_SUCCESS != s32Ret) {
        LOG_ERROR("获取传感器图像尺寸失败! s32Ret=0x%x", s32Ret);
        return s32Ret;
    }

    s32Ret = SAMPLE_COMM_SYS_GetPicSize(enPicSize, &stSize);
    if (HI_SUCCESS != s32Ret) {
        LOG_ERROR("获取图像尺寸失败! s32Ret=0x%x", s32Ret);
        return s32Ret;
    }
    pstCtx->stOrigSize = stSize;
    LOG_INFO("原始图像尺寸: %dx%d", stSize.u32Width, stSize.u32Height);

    /* 步骤3: 配置VB(视频缓存池) */
    memset(&stVbConf, 0, sizeof(VB_CONFIG_S));
    stVbConf.u32MaxPoolCnt = 2;

    /* YUV缓存 */
    u32BlkSize = COMMON_GetPicBufferSize(stSize.u32Width, stSize.u32Height,
        SAMPLE_PIXEL_FORMAT, DATA_BITWIDTH_8, COMPRESS_MODE_SEG, DEFAULT_ALIGN);
    stVbConf.astCommPool[0].u64BlkSize = u32BlkSize;
    stVbConf.astCommPool[0].u32BlkCnt = 10;

    /* Raw缓存 */
    u32BlkSize = VI_GetRawBufferSize(stSize.u32Width, stSize.u32Height,
        PIXEL_FORMAT_RGB_BAYER_16BPP, COMPRESS_MODE_NONE, DEFAULT_ALIGN);
    stVbConf.astCommPool[1].u64BlkSize = u32BlkSize;
    stVbConf.astCommPool[1].u32BlkCnt = 4;

    s32Ret = SAMPLE_COMM_SYS_Init(&stVbConf);
    if (HI_SUCCESS != s32Ret) {
        LOG_ERROR("系统初始化失败! s32Ret=0x%x", s32Ret);
        return s32Ret;
    }

    /* 步骤4: 启动VI */
    s32Ret = SAMPLE_COMM_VI_StartVi(&stViConfig);
    if (HI_SUCCESS != s32Ret) {
        LOG_ERROR("启动VI失败! s32Ret=0x%x", s32Ret);
        goto SYS_EXIT;
    }

    /* 步骤5: 配置并启动VPSS
     * VPSS通道0: 原始尺寸(用于显示/编码)
     * VPSS通道1: 416x416(用于NNIE推理)
     */
    memset(&stVpssGrpAttr, 0, sizeof(VPSS_GRP_ATTR_S));
    stVpssGrpAttr.stFrameRate.s32SrcFrameRate = -1;
    stVpssGrpAttr.stFrameRate.s32DstFrameRate = -1;
    stVpssGrpAttr.enDynamicRange = DYNAMIC_RANGE_SDR8;
    stVpssGrpAttr.enPixelFormat = enPixFormat;
    stVpssGrpAttr.u32MaxW = stSize.u32Width;
    stVpssGrpAttr.u32MaxH = stSize.u32Height;
    stVpssGrpAttr.bNrEn = HI_TRUE;
    stVpssGrpAttr.stNrAttr.enCompressMode = COMPRESS_MODE_FRAME;
    stVpssGrpAttr.stNrAttr.enNrMotionMode = NR_MOTION_MODE_NORMAL;

    /* VPSS通道0: 原始尺寸(NNIE推理用，软件缩放到416x416) */
    memset(astVpssChnAttr, 0, sizeof(astVpssChnAttr));
    astVpssChnAttr[VPSS_CHN0].u32Width = stSize.u32Width;
    astVpssChnAttr[VPSS_CHN0].u32Height = stSize.u32Height;
    astVpssChnAttr[VPSS_CHN0].enChnMode = VPSS_CHN_MODE_USER;
    astVpssChnAttr[VPSS_CHN0].enPixelFormat = enPixFormat;
    astVpssChnAttr[VPSS_CHN0].enDynamicRange = enDynamicRange;
    astVpssChnAttr[VPSS_CHN0].enVideoFormat = enVideoFormat;
    astVpssChnAttr[VPSS_CHN0].enCompressMode = enCompressMode;
    astVpssChnAttr[VPSS_CHN0].stFrameRate.s32SrcFrameRate = 30;
    astVpssChnAttr[VPSS_CHN0].stFrameRate.s32DstFrameRate = 30;
    astVpssChnAttr[VPSS_CHN0].u32Depth = 0;
    abChnEnable[VPSS_CHN0] = HI_TRUE;

    /* 启动VPSS */
    s32Ret = SAMPLE_COMM_VPSS_Start(pstCtx->VpssGrp, abChnEnable,
        &stVpssGrpAttr, astVpssChnAttr);
    if (HI_SUCCESS != s32Ret) {
        LOG_ERROR("启动VPSS失败! s32Ret=0x%x", s32Ret);
        goto VI_STOP;
    }

    /* 步骤6: 绑定VI到VPSS */
    s32Ret = SAMPLE_COMM_VI_Bind_VPSS(pstCtx->ViPipe, pstCtx->ViChn,
        pstCtx->VpssGrp);
    if (HI_SUCCESS != s32Ret) {
        LOG_ERROR("绑定VI到VPSS失败! s32Ret=0x%x", s32Ret);
        goto VPSS_STOP;
    }

    /* 等待ISP稳定输出 */
    LOG_INFO("等待ISP稳定...");
    sleep(2);

    pstCtx->bStarted = HI_TRUE;
    LOG_INFO("视频采集模块初始化完成 (原始:%dx%d, NNIE:%dx%d)",
        stSize.u32Width, stSize.u32Height, NNIE_INPUT_WIDTH, NNIE_INPUT_HEIGHT);

    return HI_SUCCESS;

VPSS_STOP:
    SAMPLE_COMM_VPSS_Stop(pstCtx->VpssGrp, abChnEnable);
VI_STOP:
    SAMPLE_COMM_VI_StopVi(&stViConfig);
SYS_EXIT:
    SAMPLE_COMM_SYS_Exit();
    return s32Ret;
}

HI_S32 VIDEO_CAPTURE_GetFrame(VIDEO_CAPTURE_CTX_S* pstCtx, HI_U8* pu8BgrBuf, HI_U32 u32BufSize)
{
    HI_S32 s32Ret;
    VIDEO_FRAME_INFO_S stFrameInfo;
    VIDEO_FRAME_S* pstFrame;
    HI_U8* pu8YuvData;
    HI_U32 u32FrameSize;

    if (!pstCtx->bStarted) {
        LOG_ERROR("视频采集模块未初始化!");
        return HI_FAILURE;
    }

    /* 检查缓冲区大小 */
    if (u32BufSize < NNIE_INPUT_WIDTH * NNIE_INPUT_HEIGHT * 3) {
        LOG_ERROR("BGR缓冲区太小! 需要%d, 提供%d",
            NNIE_INPUT_WIDTH * NNIE_INPUT_HEIGHT * 3, u32BufSize);
        return HI_FAILURE;
    }

    /* 从VPSS通道获取帧 */
    s32Ret = HI_MPI_VPSS_GetChnFrame(pstCtx->VpssGrp, pstCtx->VpssChn,
        &stFrameInfo, 2000);  /* 超时2000ms */
    if (HI_SUCCESS != s32Ret) {
        LOG_ERROR("获取VPSS帧失败! s32Ret=0x%x", s32Ret);
        return s32Ret;
    }

    pstFrame = &stFrameInfo.stVFrame;

    /* 映射帧数据到用户空间 */
    u32FrameSize = pstFrame->u32Stride[0] * pstFrame->u32Height * 3 / 2;
    pu8YuvData = (HI_U8*)HI_MPI_SYS_Mmap(pstFrame->u64PhyAddr[0], u32FrameSize);
    if (NULL == pu8YuvData) {
        LOG_ERROR("映射帧数据失败!");
        HI_MPI_VPSS_ReleaseChnFrame(pstCtx->VpssGrp, pstCtx->VpssChn, &stFrameInfo);
        return HI_FAILURE;
    }

    /* NV21转BGR Planar */
    NV21_To_BGR_Planar(pu8YuvData, pu8BgrBuf,
        pstFrame->u32Width, pstFrame->u32Height, pstFrame->u32Stride[0]);

    /* 解除映射 */
    HI_MPI_SYS_Munmap(pu8YuvData, u32FrameSize);

    /* 释放帧 */
    s32Ret = HI_MPI_VPSS_ReleaseChnFrame(pstCtx->VpssGrp, pstCtx->VpssChn, &stFrameInfo);
    if (HI_SUCCESS != s32Ret) {
        LOG_ERROR("释放VPSS帧失败! s32Ret=0x%x", s32Ret);
    }

    return s32Ret;
}

HI_S32 VIDEO_CAPTURE_Deinit(VIDEO_CAPTURE_CTX_S* pstCtx)
{
    HI_BOOL abChnEnable[VPSS_MAX_PHY_CHN_NUM] = {0};
    SAMPLE_VI_CONFIG_S stViConfig;

    if (!pstCtx->bStarted) {
        return HI_SUCCESS;
    }

    LOG_INFO("视频采集模块反初始化...");

    /* 解绑VI-VPSS */
    SAMPLE_COMM_VI_UnBind_VPSS(pstCtx->ViPipe, pstCtx->ViChn, pstCtx->VpssGrp);

    /* 停止VPSS */
    abChnEnable[VPSS_CHN0] = HI_TRUE;
    abChnEnable[VPSS_CHN1] = HI_TRUE;
    SAMPLE_COMM_VPSS_Stop(pstCtx->VpssGrp, abChnEnable);

    /* 停止VI */
    SAMPLE_COMM_VI_GetSensorInfo(&stViConfig);
    SAMPLE_COMM_VI_StopVi(&stViConfig);

    /* 系统退出 */
    SAMPLE_COMM_SYS_Exit();

    pstCtx->bStarted = HI_FALSE;
    LOG_INFO("视频采集模块反初始化完成");

    return HI_SUCCESS;
}
