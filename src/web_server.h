/**
 * @file web_server.h
 * @brief 轻量级HTTP服务器模块头文件
 *
 * 提供JSON API和前端HTML页面服务
 */

#ifndef __WEB_SERVER_H__
#define __WEB_SERVER_H__

#include "common.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

/** Web服务器上下文 */
typedef struct hiWEB_SERVER_CTX_S {
    HI_S32 s32ServerFd;         /* 服务器socket fd */
    HI_S32 s32Port;             /* 监听端口 */
    pthread_t stThreadId;       /* 服务器线程ID */
    HI_BOOL bRunning;           /* 运行标志 */
    GLOBAL_DATA_S* pstGlobalData; /* 指向全局共享数据 */
    HI_CHAR* pszWebRoot;       /* 前端文件根目录 */
} WEB_SERVER_CTX_S;

/**
 * @brief 启动Web服务器
 *
 * 在新线程中启动HTTP服务器
 *
 * @param pstCtx 服务器上下文
 * @param s32Port 监听端口
 * @param pszWebRoot 前端文件目录
 * @param pstGlobalData 全局共享数据指针
 * @return HI_SUCCESS成功, 其他失败
 */
HI_S32 WEB_SERVER_Start(WEB_SERVER_CTX_S* pstCtx, HI_S32 s32Port,
    const HI_CHAR* pszWebRoot, GLOBAL_DATA_S* pstGlobalData);

/**
 * @brief 停止Web服务器
 */
HI_S32 WEB_SERVER_Stop(WEB_SERVER_CTX_S* pstCtx);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif

#endif /* __WEB_SERVER_H__ */
