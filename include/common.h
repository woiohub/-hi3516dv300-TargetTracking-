/**
 * @file common.h
 * @brief 公共定义头文件
 *
 * 定义项目通用的数据结构、宏、错误码和常量
 */

#ifndef __COMMON_H__
#define __COMMON_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>

/* 海思MPP头文件 */
#include "hi_common.h"
#include "hi_comm_video.h"
#include "hi_comm_sys.h"
#include "hi_comm_vb.h"
#include "hi_comm_vi.h"
#include "hi_comm_vpss.h"
#include "hi_comm_venc.h"
#include "hi_comm_svp.h"
#include "hi_nnie.h"
#include "mpi_nnie.h"
#include "mpi_vi.h"
#include "mpi_vpss.h"
#include "mpi_sys.h"
#include "mpi_vb.h"
#include "sample_comm.h"
#include "sample_comm_nnie.h"
#include "sample_comm_svp.h"
#include "sample_comm_ive.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

/*========================================================================
 *                              常量定义
 *========================================================================*/

/* NNIE模型输入尺寸 */
#define NNIE_INPUT_WIDTH        416
#define NNIE_INPUT_HEIGHT       416
#define NNIE_INPUT_CHANNEL      3       /* BGR三通道 */

/* 视频帧率 */
#define VIDEO_FPS               30      /* 摄像头采集帧率 */
#define INFER_FPS               1       /* 推理帧率(每秒1帧) */

/* YOLOv3参数(COCO 80类) */
#define YOLO_CLASS_NUM          80      /* COCO 80类 */
#define YOLO_MAX_DET_NUM        1000    /* 最大候选框数量 */
#define YOLO_MAX_ROI_NUM        50      /* 最大检测框输出数量 */
#define YOLO_CONF_THRESH        0.5f    /* 置信度阈值 */
#define YOLO_NMS_THRESH         0.45f   /* NMS阈值 */

/* YOLOv3 anchor定义(基于COCO数据集) */
#define YOLO3_ANCHOR_NUM        3       /* 每个尺度的anchor数量 */
#define YOLO3_SCALE_NUM         3       /* 检测尺度数量 */
#define YOLO3_DET_DIM           (5 + YOLO_CLASS_NUM)  /* 每个anchor检测维度: tx,ty,tw,th,obj + classes */

/* 3个尺度的anchor尺寸 [scale][anchor][wh] */
static const HI_FLOAT YOLO3_ANCHORS[YOLO3_SCALE_NUM][YOLO3_ANCHOR_NUM][2] = {
    {{116, 90}, {156, 198}, {373, 326}},   /* Scale 0: 13x13 (大目标) */
    {{30, 61}, {62, 45}, {59, 119}},       /* Scale 1: 26x26 (中目标) */
    {{10, 13}, {16, 30}, {33, 23}},        /* Scale 2: 52x52 (小目标) */
};

/* 3个尺度的网格尺寸 */
static const HI_U32 YOLO3_GRID_SIZES[YOLO3_SCALE_NUM] = {13, 26, 52};

/* 追踪参数 */
#define TRACKER_MAX_TARGETS     20      /* 最大追踪目标数 */
#define TRACKER_MAX_MISS_COUNT  5       /* 最大未匹配帧数 */
#define TRACKER_MIN_IOU_THRESH  0.3f    /* 最小IoU匹配阈值 */

/* Web服务器参数 */
#define WEB_SERVER_PORT         8080    /* HTTP服务端口 */
#define WEB_MAX_RESPONSE_SIZE   16384   /* 最大响应大小 */

/* 日志宏 */
#define LOG_INFO(fmt, ...)  printf("[INFO] " fmt "\n", ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) printf("[ERROR] " fmt "\n", ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) printf("[DEBUG] " fmt "\n", ##__VA_ARGS__)

/* COCO 80类名称 */
static const char* CLASS_NAMES[] = {
    "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck",
    "boat", "traffic light", "fire hydrant", "stop sign", "parking meter", "bench",
    "bird", "cat", "dog", "horse", "sheep", "cow", "elephant", "bear", "zebra",
    "giraffe", "backpack", "umbrella", "handbag", "tie", "suitcase", "frisbee",
    "skis", "snowboard", "sports ball", "kite", "baseball bat", "baseball glove",
    "skateboard", "surfboard", "tennis racket", "bottle", "wine glass", "cup",
    "fork", "knife", "spoon", "bowl", "banana", "apple", "sandwich", "orange",
    "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair", "couch",
    "potted plant", "bed", "dining table", "toilet", "tv", "laptop", "mouse",
    "remote", "keyboard", "cell phone", "microwave", "oven", "toaster", "sink",
    "refrigerator", "book", "clock", "vase", "scissors", "teddy bear", "hair drier",
    "toothbrush"
};

/*========================================================================
 *                              数据结构
 *========================================================================*/

/** 检测结果(单个目标) */
typedef struct hiDETECTION_RESULT_S {
    HI_U32 u32ClassId;          /* 类别ID */
    HI_FLOAT f32Confidence;     /* 置信度 */
    HI_FLOAT f32X;              /* 左上角X坐标(像素) */
    HI_FLOAT f32Y;              /* 左上角Y坐标(像素) */
    HI_FLOAT f32W;              /* 宽度(像素) */
    HI_FLOAT f32H;              /* 高度(像素) */
} DETECTION_RESULT_S;

/** 检测结果集合 */
typedef struct hiDETECTION_RESULTS_S {
    HI_U32 u32ResultNum;                            /* 检测到的目标数量 */
    DETECTION_RESULT_S astResults[YOLO_MAX_ROI_NUM]; /* 检测结果数组 */
    HI_U64 u64Timestamp;                            /* 时间戳(微秒) */
} DETECTION_RESULTS_S;

/** 追踪目标 */
typedef struct hiTRACK_TARGET_S {
    HI_U32 u32TrackId;          /* 追踪ID */
    HI_FLOAT f32X;              /* 中心X坐标 */
    HI_FLOAT f32Y;              /* 中心Y坐标 */
    HI_FLOAT f32W;              /* 宽度 */
    HI_FLOAT f32H;              /* 高度 */
    HI_FLOAT f32Confidence;     /* 置信度 */
    HI_U32 u32ClassId;          /* 类别ID */
    HI_U32 u32Age;              /* 存活帧数 */
    HI_U32 u32MissCount;        /* 连续未匹配帧数 */
} TRACK_TARGET_S;

/** 追踪结果集合 */
typedef struct hiTRACK_RESULTS_S {
    HI_U32 u32TargetNum;                            /* 追踪目标数量 */
    TRACK_TARGET_S astTargets[TRACKER_MAX_TARGETS]; /* 追踪目标数组 */
} TRACK_RESULTS_S;

/** 全局共享数据(线程间通信) */
typedef struct hiGLOBAL_DATA_S {
    DETECTION_RESULTS_S stDetections;   /* 最新检测结果 */
    TRACK_RESULTS_S stTracks;           /* 最新追踪结果 */
    HI_BOOL bRunning;                   /* 运行标志 */
    pthread_mutex_t stMutex;            /* 互斥锁 */
    HI_U32 u32FrameCount;               /* 帧计数 */
    HI_FLOAT f32Fps;                    /* 当前推理FPS */
} GLOBAL_DATA_S;

/*========================================================================
 *                              工具函数
 *========================================================================*/

/** 获取当前时间(微秒) */
static inline HI_U64 get_timestamp_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (HI_U64)tv.tv_sec * 1000000 + (HI_U64)tv.tv_usec;
}

/** 计算两个边界框的IoU */
static inline HI_FLOAT calc_iou(DETECTION_RESULT_S* pA, DETECTION_RESULT_S* pB)
{
    /* 计算交集区域 */
    HI_FLOAT x1 = (pA->f32X > pB->f32X) ? pA->f32X : pB->f32X;
    HI_FLOAT y1 = (pA->f32Y > pB->f32Y) ? pA->f32Y : pB->f32Y;
    HI_FLOAT x2 = (pA->f32X + pA->f32W < pB->f32X + pB->f32W) ?
                   (pA->f32X + pA->f32W) : (pB->f32X + pB->f32W);
    HI_FLOAT y2 = (pA->f32Y + pA->f32H < pB->f32Y + pB->f32H) ?
                   (pA->f32Y + pA->f32H) : (pB->f32Y + pB->f32H);

    if (x2 <= x1 || y2 <= y1) return 0.0f;

    HI_FLOAT f32Inter = (x2 - x1) * (y2 - y1);
    HI_FLOAT f32AreaA = pA->f32W * pA->f32H;
    HI_FLOAT f32AreaB = pB->f32W * pB->f32H;

    return f32Inter / (f32AreaA + f32AreaB - f32Inter);
}

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif

#endif /* __COMMON_H__ */
