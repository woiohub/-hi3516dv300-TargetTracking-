/**
 * @file test_vio.c
 * @brief 最小化VIO测试程序 - 仅测试VI->VPSS帧获取
 * 编译: 使用项目的Makefile
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include "hi_common.h"
#include "hi_comm_video.h"
#include "hi_comm_sys.h"
#include "hi_comm_vb.h"
#include "hi_comm_vi.h"
#include "hi_comm_vpss.h"
#include "mpi_sys.h"
#include "mpi_vi.h"
#include "mpi_vpss.h"
#include "mpi_isp.h"
#include "sample_comm.h"

static HI_BOOL g_bRunning = HI_TRUE;

static void sig_handler(int sig)
{
    (void)sig;
    g_bRunning = HI_FALSE;
}

int main(int argc, char* argv[])
{
    HI_S32 s32Ret;
    SAMPLE_VI_CONFIG_S stViConfig;
    VB_CONFIG_S stVbConf;
    SIZE_S stSize;
    PIC_SIZE_E enPicSize;
    HI_U32 u32BlkSize;

    VI_DEV ViDev = 0;
    VI_PIPE ViPipe = 0;
    VI_CHN ViChn = 0;
    VPSS_GRP VpssGrp = 0;
    VPSS_CHN VpssChn = VPSS_CHN0;

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    printf("=== VIO最小化测试 ===\n");

    /* 配置VI */
    SAMPLE_COMM_VI_GetSensorInfo(&stViConfig);
    stViConfig.s32WorkingViNum = 1;
    stViConfig.as32WorkingViId[0] = 0;
    stViConfig.astViInfo[0].stSnsInfo.MipiDev = ViDev;
    stViConfig.astViInfo[0].stSnsInfo.s32BusId = 0;
    stViConfig.astViInfo[0].stDevInfo.ViDev = ViDev;
    stViConfig.astViInfo[0].stDevInfo.enWDRMode = WDR_MODE_NONE;
    stViConfig.astViInfo[0].stPipeInfo.enMastPipeMode = VI_ONLINE_VPSS_ONLINE;
    stViConfig.astViInfo[0].stPipeInfo.aPipe[0] = ViPipe;
    stViConfig.astViInfo[0].stPipeInfo.aPipe[1] = -1;
    stViConfig.astViInfo[0].stPipeInfo.aPipe[2] = -1;
    stViConfig.astViInfo[0].stPipeInfo.aPipe[3] = -1;
    stViConfig.astViInfo[0].stChnInfo.ViChn = ViChn;
    stViConfig.astViInfo[0].stChnInfo.enPixFormat = PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    stViConfig.astViInfo[0].stChnInfo.enDynamicRange = DYNAMIC_RANGE_SDR8;
    stViConfig.astViInfo[0].stChnInfo.enVideoFormat = VIDEO_FORMAT_LINEAR;
    stViConfig.astViInfo[0].stChnInfo.enCompressMode = COMPRESS_MODE_NONE;

    /* 获取图像尺寸 */
    s32Ret = SAMPLE_COMM_VI_GetSizeBySensor(
        stViConfig.astViInfo[0].stSnsInfo.enSnsType, &enPicSize);
    if (HI_SUCCESS != s32Ret) {
        printf("[ERROR] 获取传感器尺寸失败! 0x%x\n", s32Ret);
        return -1;
    }
    s32Ret = SAMPLE_COMM_SYS_GetPicSize(enPicSize, &stSize);
    if (HI_SUCCESS != s32Ret) {
        printf("[ERROR] 获取图像尺寸失败! 0x%x\n", s32Ret);
        return -1;
    }
    printf("[INFO] 图像尺寸: %dx%d\n", stSize.u32Width, stSize.u32Height);

    /* 配置VB */
    memset(&stVbConf, 0, sizeof(VB_CONFIG_S));
    stVbConf.u32MaxPoolCnt = 2;
    u32BlkSize = COMMON_GetPicBufferSize(stSize.u32Width, stSize.u32Height,
        SAMPLE_PIXEL_FORMAT, DATA_BITWIDTH_8, COMPRESS_MODE_SEG, DEFAULT_ALIGN);
    stVbConf.astCommPool[0].u64BlkSize = u32BlkSize;
    stVbConf.astCommPool[0].u32BlkCnt = 10;
    u32BlkSize = VI_GetRawBufferSize(stSize.u32Width, stSize.u32Height,
        PIXEL_FORMAT_RGB_BAYER_16BPP, COMPRESS_MODE_NONE, DEFAULT_ALIGN);
    stVbConf.astCommPool[1].u64BlkSize = u32BlkSize;
    stVbConf.astCommPool[1].u32BlkCnt = 4;

    s32Ret = SAMPLE_COMM_SYS_Init(&stVbConf);
    if (HI_SUCCESS != s32Ret) {
        printf("[ERROR] SYS_Init失败! 0x%x\n", s32Ret);
        return -1;
    }
    printf("[INFO] SYS_Init成功\n");

    /* 启动VI */
    s32Ret = SAMPLE_COMM_VI_StartVi(&stViConfig);
    if (HI_SUCCESS != s32Ret) {
        printf("[ERROR] VI_StartVi失败! 0x%x\n", s32Ret);
        goto SYS_EXIT;
    }
    printf("[INFO] VI启动成功\n");

    /* 配置VPSS */
    VPSS_GRP_ATTR_S stVpssGrpAttr;
    VPSS_CHN_ATTR_S astVpssChnAttr[VPSS_MAX_PHY_CHN_NUM];
    HI_BOOL abChnEnable[VPSS_MAX_PHY_CHN_NUM] = {0};

    memset(&stVpssGrpAttr, 0, sizeof(VPSS_GRP_ATTR_S));
    stVpssGrpAttr.stFrameRate.s32SrcFrameRate = -1;
    stVpssGrpAttr.stFrameRate.s32DstFrameRate = -1;
    stVpssGrpAttr.enDynamicRange = DYNAMIC_RANGE_SDR8;
    stVpssGrpAttr.enPixelFormat = PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    stVpssGrpAttr.u32MaxW = stSize.u32Width;
    stVpssGrpAttr.u32MaxH = stSize.u32Height;
    stVpssGrpAttr.bNrEn = HI_TRUE;
    stVpssGrpAttr.stNrAttr.enCompressMode = COMPRESS_MODE_FRAME;
    stVpssGrpAttr.stNrAttr.enNrMotionMode = NR_MOTION_MODE_NORMAL;

    memset(astVpssChnAttr, 0, sizeof(astVpssChnAttr));
    astVpssChnAttr[VpssChn].u32Width = stSize.u32Width;
    astVpssChnAttr[VpssChn].u32Height = stSize.u32Height;
    astVpssChnAttr[VpssChn].enChnMode = VPSS_CHN_MODE_USER;
    astVpssChnAttr[VpssChn].enPixelFormat = PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    astVpssChnAttr[VpssChn].enDynamicRange = DYNAMIC_RANGE_SDR8;
    astVpssChnAttr[VpssChn].enVideoFormat = VIDEO_FORMAT_LINEAR;
    astVpssChnAttr[VpssChn].enCompressMode = COMPRESS_MODE_NONE;
    astVpssChnAttr[VpssChn].stFrameRate.s32SrcFrameRate = 30;
    astVpssChnAttr[VpssChn].stFrameRate.s32DstFrameRate = 30;
    astVpssChnAttr[VpssChn].u32Depth = 0;
    abChnEnable[VpssChn] = HI_TRUE;

    s32Ret = SAMPLE_COMM_VPSS_Start(VpssGrp, abChnEnable, &stVpssGrpAttr, astVpssChnAttr);
    if (HI_SUCCESS != s32Ret) {
        printf("[ERROR] VPSS_Start失败! 0x%x\n", s32Ret);
        goto VI_STOP;
    }
    printf("[INFO] VPSS启动成功\n");

    printf("[INFO] 等待ISP稳定...\n");
    sleep(2);

    /* 测试帧获取 */
    printf("[INFO] 开始获取帧...\n");
    VIDEO_FRAME_INFO_S stFrameInfo;
    HI_U32 u32FrameCount = 0;

    while (g_bRunning && u32FrameCount < 30) {
        s32Ret = HI_MPI_VPSS_GetChnFrame(VpssGrp, VpssChn, &stFrameInfo, 2000);
        if (HI_SUCCESS != s32Ret) {
            printf("[ERROR] GetChnFrame失败! 0x%x (帧#%u)\n", s32Ret, u32FrameCount);
            usleep(100000);
            continue;
        }

        u32FrameCount++;
        printf("[OK] 帧#%u: %ux%u, Stride=%u, PixFmt=%u\n",
            u32FrameCount,
            stFrameInfo.stVFrame.u32Width,
            stFrameInfo.stVFrame.u32Height,
            stFrameInfo.stVFrame.u32Stride[0],
            stFrameInfo.stVFrame.enPixelFormat);

        HI_MPI_VPSS_ReleaseChnFrame(VpssGrp, VpssChn, &stFrameInfo);
    }

    printf("\n=== 测试完成: 成功获取 %u 帧 ===\n", u32FrameCount);

    /* 清理 */
    {
        HI_BOOL abCE[VPSS_MAX_PHY_CHN_NUM] = {0};
        abCE[VpssChn] = HI_TRUE;
        SAMPLE_COMM_VPSS_Stop(VpssGrp, abCE);
    }
VI_STOP:
    {
        SAMPLE_VI_CONFIG_S stViCfg;
        memset(&stViCfg, 0, sizeof(stViCfg));
        SAMPLE_COMM_VI_GetSensorInfo(&stViCfg);
        SAMPLE_COMM_VI_StopVi(&stViCfg);
    }
SYS_EXIT:
    SAMPLE_COMM_SYS_Exit();
    return 0;
}
