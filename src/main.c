/**
 * @file main.c
 * @brief 主程序入口
 *
 * Hi3516DV300 + GC2053 端侧目标追踪系统
 *
 * 主循环流程:
 * 1. 初始化视频采集(VI/VPSS)
 * 2. 初始化NNIE推理(YOLOv3)
 * 3. 初始化目标追踪器
 * 4. 启动Web服务器
 * 5. 主循环: 每1秒推理一帧
 *    - 获取VPSS帧 -> YUV转BGR -> NNIE推理 -> YOLOv3后处理 -> 目标追踪 -> 更新共享数据
 */

#include "common.h"
#include "video_capture.h"
#include "nnie_inference.h"
#include "tracker.h"
#include "web_server.h"

/*========================================================================
 *                              全局变量
 *========================================================================*/

static VIDEO_CAPTURE_CTX_S g_stVideoCtx;    /* 视频采集上下文 */
static NNIE_CTX_S g_stNnieCtx;              /* NNIE推理上下文 */
static TRACKER_CTX_S g_stTrackerCtx;        /* 追踪器上下文 */
static WEB_SERVER_CTX_S g_stWebCtx;         /* Web服务器上下文 */
static GLOBAL_DATA_S g_stGlobalData;        /* 全局共享数据 */
static volatile HI_BOOL g_bRunning = HI_TRUE; /* 运行标志 */

/* BGR帧缓冲区 */
static HI_U8 g_au8BgrBuf[NNIE_INPUT_WIDTH * NNIE_INPUT_HEIGHT * 3];

/*========================================================================
 *                              信号处理
 *========================================================================*/

static void signal_handler(HI_S32 s32Signo)
{
    if (SIGINT == s32Signo || SIGTERM == s32Signo) {
        LOG_INFO("收到退出信号, 正在清理...");
        g_bRunning = HI_FALSE;
        g_stGlobalData.bRunning = HI_FALSE;
    }
}

/*========================================================================
 *                              主函数
 *========================================================================*/

HI_S32 main(HI_S32 argc, HI_CHAR* argv[])
{
    HI_S32 s32Ret;
    struct timeval stStartTime, stEndTime;
    HI_FLOAT f32InferTime;

    printf("============================================\n");
    printf("  Hi3516DV300 + GC2053 目标追踪系统\n");
    printf("  YOLOv3 + NNIE + IoU Tracker\n");
    printf("============================================\n\n");

    /* 注册信号处理 */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* 初始化全局数据 */
    memset(&g_stGlobalData, 0, sizeof(GLOBAL_DATA_S));
    g_stGlobalData.bRunning = HI_TRUE;
    pthread_mutex_init(&g_stGlobalData.stMutex, NULL);

    /* 解析命令行参数 */
    const HI_CHAR* pszModelPath = "./model/data/nnie_model/yolov3.wk";
    const HI_CHAR* pszWebRoot = "./web";
    HI_S32 s32WebPort = WEB_SERVER_PORT;

    if (argc >= 2) {
        pszModelPath = argv[1];
    }
    if (argc >= 3) {
        s32WebPort = atoi(argv[2]);
    }
    if (argc >= 4) {
        pszWebRoot = argv[3];
    }

    LOG_INFO("模型文件: %s", pszModelPath);
    LOG_INFO("Web端口: %d", s32WebPort);
    LOG_INFO("前端目录: %s", pszWebRoot);

    /* 步骤1: 初始化视频采集 */
    LOG_INFO("=== 步骤1: 初始化视频采集 ===");
    s32Ret = VIDEO_CAPTURE_Init(&g_stVideoCtx);
    if (HI_SUCCESS != s32Ret) {
        LOG_ERROR("视频采集初始化失败! s32Ret=0x%x", s32Ret);
        goto EXIT;
    }

    /* 步骤2: 初始化NNIE推理 */
    LOG_INFO("=== 步骤2: 初始化NNIE推理 ===");
    s32Ret = NNIE_Init(&g_stNnieCtx, pszModelPath);
    if (HI_SUCCESS != s32Ret) {
        LOG_ERROR("NNIE推理初始化失败! s32Ret=0x%x", s32Ret);
        goto VIDEO_DEINIT;
    }

    /* 步骤3: 初始化目标追踪器 */
    LOG_INFO("=== 步骤3: 初始化目标追踪器 ===");
    s32Ret = TRACKER_Init(&g_stTrackerCtx);
    if (HI_SUCCESS != s32Ret) {
        LOG_ERROR("追踪器初始化失败! s32Ret=0x%x", s32Ret);
        goto NNIE_DEINIT;
    }

    /* 步骤4: 启动Web服务器 */
    LOG_INFO("=== 步骤4: 启动Web服务器 ===");
    s32Ret = WEB_SERVER_Start(&g_stWebCtx, s32WebPort, pszWebRoot, &g_stGlobalData);
    if (HI_SUCCESS != s32Ret) {
        LOG_ERROR("Web服务器启动失败! s32Ret=0x%x", s32Ret);
        goto TRACKER_DEINIT;
    }

    /* 步骤5: 主循环 */
    LOG_INFO("=== 步骤5: 开始目标追踪主循环 ===");
    LOG_INFO("按 Ctrl+C 退出\n");

    while (g_bRunning) {
        gettimeofday(&stStartTime, NULL);

        /* 获取视频帧(BGR格式) */
        s32Ret = VIDEO_CAPTURE_GetFrame(&g_stVideoCtx, g_au8BgrBuf, sizeof(g_au8BgrBuf));
        if (HI_SUCCESS != s32Ret) {
            LOG_ERROR("获取视频帧失败! s32Ret=0x%x", s32Ret);
            usleep(100000); /* 100ms后重试 */
            continue;
        }

        /* NNIE推理 */
        DETECTION_RESULTS_S stDetections;
        s32Ret = NNIE_Forward(&g_stNnieCtx, g_au8BgrBuf, &stDetections);
        if (HI_SUCCESS != s32Ret) {
            LOG_ERROR("NNIE推理失败! s32Ret=0x%x", s32Ret);
            continue;
        }

        /* 目标追踪 */
        TRACK_RESULTS_S stTracks;
        s32Ret = TRACKER_Update(&g_stTrackerCtx, &stDetections, &stTracks);
        if (HI_SUCCESS != s32Ret) {
            LOG_ERROR("目标追踪失败! s32Ret=0x%x", s32Ret);
            continue;
        }

        /* 计算推理耗时 */
        gettimeofday(&stEndTime, NULL);
        f32InferTime = (stEndTime.tv_sec - stStartTime.tv_sec) * 1000.0f +
                       (stEndTime.tv_usec - stStartTime.tv_usec) / 1000.0f;

        /* 更新全局共享数据(供Web服务器读取) */
        pthread_mutex_lock(&g_stGlobalData.stMutex);
        g_stGlobalData.stDetections = stDetections;
        g_stGlobalData.stTracks = stTracks;
        g_stGlobalData.u32FrameCount++;
        g_stGlobalData.f32Fps = 1000.0f / f32InferTime;
        pthread_mutex_unlock(&g_stGlobalData.stMutex);

        /* 打印检测结果 */
        LOG_INFO("[帧%u] 检测:%u 追踪:%u 耗时:%.0fms FPS:%.1f",
            g_stGlobalData.u32FrameCount,
            stDetections.u32ResultNum,
            stTracks.u32TargetNum,
            f32InferTime,
            g_stGlobalData.f32Fps);

        for (HI_U32 i = 0; i < stTracks.u32TargetNum; i++) {
            TRACK_TARGET_S* pstT = &stTracks.astTargets[i];
            const char* pszClass = (pstT->u32ClassId < YOLO_CLASS_NUM) ?
                CLASS_NAMES[pstT->u32ClassId] : "unknown";
            LOG_INFO("  [ID:%u] %s conf:%.2f pos:(%.0f,%.0f) size:%.0fx%.0f",
                pstT->u32TrackId, pszClass, pstT->f32Confidence,
                pstT->f32X, pstT->f32Y, pstT->f32W, pstT->f32H);
        }

        /* 帧率控制: 等待到1秒间隔 */
        gettimeofday(&stEndTime, NULL);
        HI_S32 s32Elapsed = (stEndTime.tv_sec - stStartTime.tv_sec) * 1000000 +
                             (stEndTime.tv_usec - stStartTime.tv_usec);
        if (s32Elapsed < 1000000) { /* 小于1秒 */
            usleep(1000000 - s32Elapsed);
        }
    }

    /* 清理 */
    WEB_SERVER_Stop(&g_stWebCtx);

TRACKER_DEINIT:
    TRACKER_Deinit(&g_stTrackerCtx);

NNIE_DEINIT:
    NNIE_Deinit(&g_stNnieCtx);

VIDEO_DEINIT:
    VIDEO_CAPTURE_Deinit(&g_stVideoCtx);

EXIT:
    pthread_mutex_destroy(&g_stGlobalData.stMutex);
    LOG_INFO("目标追踪系统已退出");
    return s32Ret;
}
