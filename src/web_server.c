/**
 * @file web_server.c
 * @brief 轻量级HTTP服务器实现
 *
 * 单线程HTTP服务器，支持:
 * - GET / 返回前端HTML页面
 * - GET /api/detections 返回检测结果JSON
 *
 * 无需外部依赖，使用纯socket实现
 */

#include "web_server.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

/* HTTP响应模板 */
static const char* HTTP_200_JSON =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: application/json\r\n"
    "Access-Control-Allow-Origin: *\r\n"
    "Connection: close\r\n"
    "Content-Length: %d\r\n"
    "\r\n%s";

static const char* HTTP_200_HTML =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Connection: close\r\n"
    "Content-Length: %d\r\n"
    "\r\n%s";

static const char* HTTP_404 =
    "HTTP/1.1 404 Not Found\r\n"
    "Content-Type: text/plain\r\n"
    "Connection: close\r\n"
    "Content-Length: 13\r\n"
    "\r\n404 Not Found";

/**
 * @brief 生成检测结果JSON字符串
 */
static HI_S32 build_detections_json(GLOBAL_DATA_S* pstData, HI_CHAR* pszBuf, HI_U32 u32BufSize)
{
    HI_S32 s32Offset = 0;
    TRACK_RESULTS_S stTracks;
    HI_U32 i;

    /* 加锁读取追踪结果 */
    pthread_mutex_lock(&pstData->stMutex);
    stTracks = pstData->stTracks;
    pthread_mutex_unlock(&pstData->stMutex);

    /* 构建JSON */
    s32Offset += snprintf(pszBuf + s32Offset, u32BufSize - s32Offset,
        "{\"timestamp\":%llu,\"frame_count\":%u,\"fps\":%.1f,\"target_count\":%u,\"targets\":[",
        (unsigned long long)get_timestamp_us(),
        pstData->u32FrameCount,
        pstData->f32Fps,
        stTracks.u32TargetNum);

    for (i = 0; i < stTracks.u32TargetNum && s32Offset < (HI_S32)(u32BufSize - 200); i++) {
        TRACK_TARGET_S* pstT = &stTracks.astTargets[i];
        const char* pszClassName = (pstT->u32ClassId < YOLO_CLASS_NUM) ?
            CLASS_NAMES[pstT->u32ClassId] : "unknown";

        if (i > 0) {
            s32Offset += snprintf(pszBuf + s32Offset, u32BufSize - s32Offset, ",");
        }

        s32Offset += snprintf(pszBuf + s32Offset, u32BufSize - s32Offset,
            "{\"id\":%u,\"class\":\"%s\",\"class_id\":%u,\"confidence\":%.2f,"
            "\"cx\":%.1f,\"cy\":%.1f,\"w\":%.1f,\"h\":%.1f,\"age\":%u}",
            pstT->u32TrackId, pszClassName, pstT->u32ClassId, pstT->f32Confidence,
            pstT->f32X, pstT->f32Y, pstT->f32W, pstT->f32H, pstT->u32Age);
    }

    s32Offset += snprintf(pszBuf + s32Offset, u32BufSize - s32Offset, "]}");

    return s32Offset;
}

/**
 * @brief 读取HTML文件内容
 */
static HI_S32 read_html_file(const HI_CHAR* pszPath, HI_CHAR* pszBuf, HI_U32 u32BufSize)
{
    FILE* pFile = fopen(pszPath, "r");
    if (NULL == pFile) {
        return -1;
    }
    HI_S32 s32Len = fread(pszBuf, 1, u32BufSize - 1, pFile);
    fclose(pFile);
    if (s32Len > 0) {
        pszBuf[s32Len] = '\0';
    }
    return s32Len;
}

/**
 * @brief 处理单个HTTP请求
 */
static HI_VOID handle_request(HI_S32 s32ClientFd, WEB_SERVER_CTX_S* pstCtx)
{
    HI_CHAR szRequest[2048];
    HI_CHAR szResponse[WEB_MAX_RESPONSE_SIZE + 512];
    HI_CHAR szBody[WEB_MAX_RESPONSE_SIZE];
    HI_S32 s32RecvLen, s32BodyLen;

    /* 读取请求 */
    s32RecvLen = recv(s32ClientFd, szRequest, sizeof(szRequest) - 1, 0);
    if (s32RecvLen <= 0) {
        close(s32ClientFd);
        return;
    }
    szRequest[s32RecvLen] = '\0';

    /* 解析请求路径 */
    if (strncmp(szRequest, "GET / ", 6) == 0 || strncmp(szRequest, "GET /index.html", 15) == 0) {
        /* 返回前端页面 */
        HI_CHAR szHtmlPath[512];
        snprintf(szHtmlPath, sizeof(szHtmlPath), "%s/index.html", pstCtx->pszWebRoot);

        s32BodyLen = read_html_file(szHtmlPath, szBody, sizeof(szBody));
        if (s32BodyLen < 0) {
            send(s32ClientFd, HTTP_404, strlen(HTTP_404), 0);
        } else {
            HI_S32 s32RespLen = snprintf(szResponse, sizeof(szResponse),
                HTTP_200_HTML, s32BodyLen, szBody);
            send(s32ClientFd, szResponse, s32RespLen, 0);
        }
    } else if (strncmp(szRequest, "GET /api/detections", 19) == 0) {
        /* 返回检测结果JSON */
        s32BodyLen = build_detections_json(pstCtx->pstGlobalData, szBody, sizeof(szBody));
        HI_S32 s32RespLen = snprintf(szResponse, sizeof(szResponse),
            HTTP_200_JSON, s32BodyLen, szBody);
        send(s32ClientFd, szResponse, s32RespLen, 0);
    } else {
        /* 404 */
        send(s32ClientFd, HTTP_404, strlen(HTTP_404), 0);
    }

    close(s32ClientFd);
}

/**
 * @brief Web服务器线程函数
 */
static HI_VOID* web_server_thread(HI_VOID* pArg)
{
    WEB_SERVER_CTX_S* pstCtx = (WEB_SERVER_CTX_S*)pArg;
    struct sockaddr_in stClientAddr;
    socklen_t u32AddrLen = sizeof(stClientAddr);

    LOG_INFO("Web服务器线程启动, 端口: %d", pstCtx->s32Port);

    while (pstCtx->bRunning) {
        HI_S32 s32ClientFd = accept(pstCtx->s32ServerFd,
            (struct sockaddr*)&stClientAddr, &u32AddrLen);

        if (s32ClientFd < 0) {
            if (pstCtx->bRunning) {
                /* 非关闭状态的accept错误 */
                usleep(10000); /* 10ms */
            }
            continue;
        }

        /* 设置接收超时 */
        struct timeval tv;
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        setsockopt(s32ClientFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        handle_request(s32ClientFd, pstCtx);
    }

    LOG_INFO("Web服务器线程退出");
    return NULL;
}

HI_S32 WEB_SERVER_Start(WEB_SERVER_CTX_S* pstCtx, HI_S32 s32Port,
    const HI_CHAR* pszWebRoot, GLOBAL_DATA_S* pstGlobalData)
{
    struct sockaddr_in stServAddr;
    HI_S32 s32Opt = 1;

    LOG_INFO("启动Web服务器, 端口: %d, 根目录: %s", s32Port, pszWebRoot);

    pstCtx->s32Port = s32Port;
    pstCtx->pstGlobalData = pstGlobalData;
    pstCtx->pszWebRoot = (HI_CHAR*)pszWebRoot;
    pstCtx->bRunning = HI_FALSE;

    /* 创建socket */
    pstCtx->s32ServerFd = socket(AF_INET, SOCK_STREAM, 0);
    if (pstCtx->s32ServerFd < 0) {
        LOG_ERROR("创建socket失败: %s", strerror(errno));
        return HI_FAILURE;
    }

    /* 设置SO_REUSEADDR */
    setsockopt(pstCtx->s32ServerFd, SOL_SOCKET, SO_REUSEADDR, &s32Opt, sizeof(s32Opt));

    /* 绑定地址 */
    memset(&stServAddr, 0, sizeof(stServAddr));
    stServAddr.sin_family = AF_INET;
    stServAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    stServAddr.sin_port = htons(s32Port);

    if (bind(pstCtx->s32ServerFd, (struct sockaddr*)&stServAddr, sizeof(stServAddr)) < 0) {
        LOG_ERROR("绑定端口失败: %s", strerror(errno));
        close(pstCtx->s32ServerFd);
        return HI_FAILURE;
    }

    /* 开始监听 */
    if (listen(pstCtx->s32ServerFd, 5) < 0) {
        LOG_ERROR("监听失败: %s", strerror(errno));
        close(pstCtx->s32ServerFd);
        return HI_FAILURE;
    }

    /* 启动服务器线程 */
    pstCtx->bRunning = HI_TRUE;
    if (pthread_create(&pstCtx->stThreadId, NULL, web_server_thread, pstCtx) != 0) {
        LOG_ERROR("创建服务器线程失败!");
        close(pstCtx->s32ServerFd);
        return HI_FAILURE;
    }

    LOG_INFO("Web服务器启动成功");
    return HI_SUCCESS;
}

HI_S32 WEB_SERVER_Stop(WEB_SERVER_CTX_S* pstCtx)
{
    if (!pstCtx->bRunning) {
        return HI_SUCCESS;
    }

    LOG_INFO("停止Web服务器...");
    pstCtx->bRunning = HI_FALSE;

    /* 关闭socket以中断accept */
    if (pstCtx->s32ServerFd >= 0) {
        shutdown(pstCtx->s32ServerFd, SHUT_RDWR);
        close(pstCtx->s32ServerFd);
        pstCtx->s32ServerFd = -1;
    }

    /* 等待线程退出 */
    pthread_join(pstCtx->stThreadId, NULL);

    LOG_INFO("Web服务器已停止");
    return HI_SUCCESS;
}
