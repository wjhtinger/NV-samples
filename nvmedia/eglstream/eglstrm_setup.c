/*
 * Copyright (c) 2014-2016, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

//
// DESCRIPTION:   Common egl stream functions
//

#include "eglstrm_setup.h"
#include "log_utils.h"
#include "egl_utils.h"
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#ifdef NVMEDIA_GHSI
#include <netinet/in.h>
#include <arpa/inet.h>
#include <EGL/eglext.h>

#define MAX_ATTRIB    (31)
static int gsock = -1;
#endif
EXTENSION_LIST(EXTLST_EXTERN)
#ifndef NVMEDIA_GHSI
/* Send <fd_to_send> (a file descriptor) to another process */
/* over a unix domain socket named <socket_name>.           */
/* <socket_name> can be any nonexistant filename.           */
static int EGLStreamSendfd(const char *socket_name, int fd_to_send)
{
    int sock_fd;
    struct sockaddr_un sock_addr;
    struct msghdr msg;
    struct iovec iov[1];
    char ctrl_buf[CMSG_SPACE(sizeof(int))];
    struct cmsghdr *cmsg = NULL;
    void *data;
    int res;
    int wait_loop = 0;

    sock_fd = socket(PF_UNIX, SOCK_STREAM, 0);
    if(sock_fd < 0) {
        LOG_ERR("EGLStreamSendfd: socket");
        return -1;
    }
    LOG_DBG("EGLStreamSendfd: sock_fd: %d\n", sock_fd);

    memset(&sock_addr, 0, sizeof(struct sockaddr_un));
    sock_addr.sun_family = AF_UNIX;
    strncpy(sock_addr.sun_path,
            socket_name,
            sizeof(sock_addr.sun_path)-1);

    while(connect(sock_fd,
                (const struct sockaddr*)&sock_addr,
                sizeof(struct sockaddr_un))) {
        if(wait_loop < 60) {
            if(!wait_loop)
                printf("Waiting for EGL stream producer ");
            else
                printf(".");
            fflush(stdout);
            sleep(1);
            wait_loop++;
        } else {
            printf("\nWaiting timed out\n");
            return -1;
        }
    }
    if(wait_loop)
        printf("\n");

    LOG_DBG("EGLStreamSendfd: Wait is done\n");

    memset(&msg, 0, sizeof(msg));

    iov[0].iov_len  = 1;    // must send at least 1 byte
    iov[0].iov_base = "x";  // any byte value (value ignored)
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

    memset(ctrl_buf, 0, sizeof(ctrl_buf));
    msg.msg_control = ctrl_buf;
    msg.msg_controllen = sizeof(ctrl_buf);

    cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    data = CMSG_DATA(cmsg);
    *(int *)data = fd_to_send;

    msg.msg_controllen = cmsg->cmsg_len;

    res = sendmsg(sock_fd, &msg, 0);
    LOG_DBG("EGLStreamSendfd: sendmsg: res: %d\n", res);

    if(res <= 0) {
        LOG_ERR("EGLStreamSendfd: sendmsg");
        return -1;
    }

    close(sock_fd);

    return 0;
}

/* Listen on a unix domain socket named <socket_name> and  */
/* receive a file descriptor from another process.         */
/* Returns the file descriptor.  Note: the integer value   */
/* of the file descriptor may be different from the        */
/* integer value in the other process, but the file        */
/* descriptors in each process will refer to the same file */
/* object in the kernel.                                   */
static int EGLStreamReceivefd(const char *socket_name)
{
    int listen_fd;
    struct sockaddr_un sock_addr;
    int connect_fd;
    struct sockaddr_un connect_addr;
    socklen_t connect_addr_len = 0;
    struct msghdr msg;
    struct iovec iov[1];
    char msg_buf[1];
    char ctrl_buf[CMSG_SPACE(sizeof(int))];
    struct cmsghdr *cmsg;
    void *data;
    int recvfd;

    listen_fd = socket(PF_UNIX, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        LOG_ERR("EGLStreamReceivefd: socket");
        return -1;
    }
    LOG_DBG("EGLStreamReceivefd: listen_fd: %d\n", listen_fd);

    unlink(socket_name);

    memset(&sock_addr, 0, sizeof(struct sockaddr_un));
    sock_addr.sun_family = AF_UNIX;
    strncpy(sock_addr.sun_path,
            socket_name,
            sizeof(sock_addr.sun_path)-1);

    if (bind(listen_fd,
             (const struct sockaddr*)&sock_addr,
             sizeof(struct sockaddr_un))) {
        LOG_ERR("EGLStreamReceivefd: bind");
        return -1;
    }

    if (listen(listen_fd, 1)) {
        LOG_ERR("EGLStreamReceivefd: listen");
        return -1;
    }

    connect_fd = accept(
                    listen_fd,
                    (struct sockaddr *)&connect_addr,
                    &connect_addr_len);
    LOG_DBG("EGLStreamReceivefd: connect_fd: %d\n", connect_fd);
    close(listen_fd);
    unlink(socket_name);
    if (connect_fd < 0) {
        LOG_ERR("EGLStreamReceivefd: accept");
        return -1;
    }

    memset(&msg, 0, sizeof(msg));

    iov[0].iov_base = msg_buf;
    iov[0].iov_len  = sizeof(msg_buf);
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

    msg.msg_control = ctrl_buf;
    msg.msg_controllen = sizeof(ctrl_buf);

    if (recvmsg(connect_fd, &msg, 0) <= 0) {
        LOG_ERR("EGLStreamReceivefd: recvmsg");
        return -1;
    }

    cmsg = CMSG_FIRSTHDR(&msg);
    if (!cmsg) {
        LOG_DBG("EGLStreamReceivefd: NULL message header\n");
        return -1;
    }
    if (cmsg->cmsg_level != SOL_SOCKET) {
        LOG_DBG("EGLStreamReceivefd: Message level is not SOL_SOCKET\n");
        return -1;
    }
    if (cmsg->cmsg_type != SCM_RIGHTS) {
        LOG_DBG("EGLStreamReceivefd: Message type is not SCM_RIGHTS\n");
        return -1;
    }

    data = CMSG_DATA(cmsg);
    recvfd = *(int *)data;

    return recvfd;
}
#else
static int ConnectProducer(TestArgs *args)
{
    int sock;
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_addr.s_addr = inet_addr(args->ip);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(args->socketport);

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        LOG_ERR("create socket failed\n");
        return sock;
    }

    while (!connect(sock, (struct sockaddr*)&server_addr,
                sizeof(server_addr))) {
        LOG_DBG("Waiting for consumer\n");
        sleep(2);
    }

    return sock;
}

static int BindSocket(int sock, TestArgs *args)
{
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(args->socketport);

    return bind(sock, (struct sockaddr *)&server_addr,
            sizeof(server_addr));
}

static int ConnectConsumer(TestArgs *args)
{
    int retval;
    int server_sock;
    int accept_sock;
    struct sockaddr_in client_addr;
    int client_addr_len = sizeof(client_addr);

    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        LOG_ERR("[C] create socket failed\n");
        goto out;
    }

    retval = BindSocket(server_sock, args);
    if (retval < 0) {
        LOG_ERR("[C] BindSocket failed\n");
        goto out;
    }

    retval = listen(server_sock , 1);
    if (retval < 0) {
        LOG_ERR("[C] ListenSocket failed\n");
        goto out;
    }

    accept_sock = accept(server_sock,
                         (struct sockaddr *)&client_addr,
                         (socklen_t*)(&client_addr_len));
    if (accept_sock < 0) {
        LOG_ERR("[C] CreateAcceptSocket failed\n");
        goto out;
    }

    LOG_DBG("Accept sock = %d ip = %s\n", accept_sock, inet_ntoa(client_addr.sin_addr));

    close(server_sock);
    LOG_DBG("[C] created accept socket\n");

    return accept_sock;
out:
    close(server_sock);
    return -1;
}

static EGLStreamKHR CreateConsumerEGLStream(EGLDisplay display,
                                            const EGLint *streamAttr,
                                            TestArgs *args)
{
    EGLint attrList[2*MAX_ATTRIB+1]={0};
    int  attrIndex = 0, ret = 0;
    EGLint streamState = EGL_STREAM_STATE_EMPTY_KHR;
    EGLStreamKHR eglStream = EGL_NO_STREAM_KHR;

    //Create eglstream
    if((gsock = ConnectConsumer(args)) <= 0) {
        LOG_ERR("Failed to connect producer\n");
        return EGL_NO_STREAM_KHR;
    }

    while (streamAttr[attrIndex] != EGL_NONE) {
        attrList[attrIndex] = streamAttr[attrIndex];
        attrIndex++;
    };

    //set attribute list
    attrList[attrIndex++] = EGL_STREAM_PROTOCOL_NV;
    attrList[attrIndex++] = EGL_STREAM_PROTOCOL_SOCKET_NV;

    attrList[attrIndex++] = EGL_STREAM_TYPE_NV;
    if (args->standalone == STANDALONE_CONSUMER)
        attrList[attrIndex++] = EGL_STREAM_CROSS_PROCESS_NV;

    attrList[attrIndex++] = EGL_SOCKET_TYPE_NV;
    attrList[attrIndex++] = EGL_SOCKET_TYPE_INET_NV;

    attrList[attrIndex++] = EGL_SOCKET_HANDLE_NV;
    attrList[attrIndex++] = gsock;

    attrList[attrIndex++] = EGL_STREAM_ENDPOINT_NV;
    attrList[attrIndex++] = EGL_STREAM_CONSUMER_NV;

    attrList[attrIndex++] = EGL_NONE;

    eglStream = eglCreateStreamKHR(display, attrList);
    if (eglStream == EGL_NO_STREAM_KHR) {
        LOG_ERR("consumer failed to create eglstream\n");
        return eglStream;
    }
    // Wait while stream initializes
    do {
        ret = eglQueryStreamKHR(display,
                eglStream,
                EGL_STREAM_STATE_KHR,
                &streamState);

        if (!ret) {
            LOG_ERR("[C] Could not query EGL stream state\n");
            return EGL_NO_STREAM_KHR;
        }
    } while (streamState == EGL_STREAM_STATE_INITIALIZING_NV);

    if (!(streamState == EGL_STREAM_STATE_CREATED_KHR)) {
        LOG_ERR("[C] EGL stream is not in valid starting state\n");
        return EGL_NO_STREAM_KHR;
    }

    return eglStream;
}

static EGLStreamKHR CreateProducerEGLStream(EGLDisplay display,
                                            const EGLint *streamAttr,
                                            TestArgs *args)
{
    EGLint streamState = EGL_STREAM_STATE_EMPTY_KHR;
    EGLint attrList[2*MAX_ATTRIB+1]={0};
    int attrIndex = 0, ret = 0;
    EGLStreamKHR eglStream = EGL_NO_STREAM_KHR;

    if ((gsock = ConnectProducer(args)) <= 0) {
        LOG_ERR("Failed to connect to consumer\n");
        return EGL_NO_STREAM_KHR;
    }

    while (streamAttr[attrIndex] != EGL_NONE) {
        attrList[attrIndex] = streamAttr[attrIndex];
        attrIndex++;
    };

    //set attribute list
    attrList[attrIndex++] = EGL_CONSUMER_LATENCY_USEC_KHR;
    attrList[attrIndex++] = 0;

    attrList[attrIndex++] = EGL_STREAM_PROTOCOL_NV;
    attrList[attrIndex++] = EGL_STREAM_PROTOCOL_SOCKET_NV;

    attrList[attrIndex++] = EGL_STREAM_TYPE_NV;
    if (args->standalone == STANDALONE_PRODUCER)
        attrList[attrIndex++] = EGL_STREAM_CROSS_PROCESS_NV;

    attrList[attrIndex++] = EGL_SOCKET_TYPE_NV;
    attrList[attrIndex++] = EGL_SOCKET_TYPE_INET_NV;

    attrList[attrIndex++] = EGL_SOCKET_HANDLE_NV;
    attrList[attrIndex++] = gsock;

    attrList[attrIndex++] = EGL_STREAM_ENDPOINT_NV;
    attrList[attrIndex++] = EGL_STREAM_PRODUCER_NV;

    attrList[attrIndex++] = EGL_NONE;

    eglStream = eglCreateStreamKHR(display, attrList);
    if (eglStream == EGL_NO_STREAM_KHR) {
        LOG_ERR("producer failed to create eglstream\n");
        return eglStream;
    }
    // Wait while stream initializes
    do {
        ret = eglQueryStreamKHR(display,
                                eglStream,
                                EGL_STREAM_STATE_KHR,
                                &streamState);

        if (!ret) {
            LOG_ERR("[P] Could not query EGL stream state\n");
            return EGL_NO_STREAM_KHR;
        }
    } while (streamState == EGL_STREAM_STATE_INITIALIZING_NV);

    if (!(streamState == EGL_STREAM_STATE_CREATED_KHR) &&
            !(streamState == EGL_STREAM_STATE_CONNECTING_KHR)) {
        LOG_ERR("EGL stream is not in valid starting state\n");
        return EGL_NO_STREAM_KHR;
    }

    return eglStream;
}
#endif
void PrintEGLStreamState(EGLint streamState)
{
    #define STRING_VAL(x) {""#x"", x}
    struct {
        char *name;
        EGLint val;
    } EGLState[9] = {
        STRING_VAL(EGL_STREAM_STATE_CREATED_KHR),
        STRING_VAL(EGL_STREAM_STATE_CONNECTING_KHR),
        STRING_VAL(EGL_STREAM_STATE_EMPTY_KHR),
        STRING_VAL(EGL_STREAM_STATE_NEW_FRAME_AVAILABLE_KHR),
        STRING_VAL(EGL_STREAM_STATE_OLD_FRAME_AVAILABLE_KHR),
        STRING_VAL(EGL_STREAM_STATE_DISCONNECTED_KHR),
        STRING_VAL(EGL_BAD_STREAM_KHR),
        STRING_VAL(EGL_BAD_STATE_KHR),
        { NULL, 0 }
    };
    int i = 0;

    while(EGLState[i].name) {
        if(streamState == EGLState[i].val) {
            printf("%s\n", EGLState[i].name);
            return;
        }
        i++;
    }
    printf("Invalid %d\n", streamState);
}

EGLStreamKHR EGLStreamInit(EGLDisplay display, TestArgs *args)
{
    EGLStreamKHR eglStream=EGL_NO_STREAM_KHR;
#ifdef EGL_NV_stream_metadata
    //! [docs_eglstream:EGLint]
    static const EGLint streamAttrMailboxMode[] = { EGL_METADATA0_SIZE_NV, 16*1024,
                                                    EGL_METADATA1_SIZE_NV, 16*1024,
                                                    EGL_METADATA2_SIZE_NV, 16*1024,
                                                    EGL_METADATA3_SIZE_NV, 16*1024, EGL_NONE };
    static const EGLint streamAttrFIFOMode[] = { EGL_STREAM_FIFO_LENGTH_KHR, 4,
                                                 EGL_METADATA0_SIZE_NV, 16*1024,
                                                 EGL_METADATA1_SIZE_NV, 16*1024,
                                                 EGL_METADATA2_SIZE_NV, 16*1024,
                                                 EGL_METADATA3_SIZE_NV, 16*1024, EGL_NONE };
    //! [docs_eglstream:EGLint]
#else
    static const EGLint streamAttrMailboxMode[] = { EGL_NONE };
    static const EGLint streamAttrFIFOMode[] = { EGL_STREAM_FIFO_LENGTH_KHR, 4, EGL_NONE };
#endif //EGL_NV_stream_metadata

    EGLint fifo_length = 0, latency = 0, timeout = 0;
    GLint acquireTimeout = 16000;

    LOG_DBG("EGLStreamInit - Start\n");
#ifndef NVMEDIA_GHSI
    if(args->standalone != STANDALONE_PRODUCER) {
        //! [docs_eglstream:eglCreateStreamKHR]
        // Standalone consumer or no standalone mode
        eglStream = eglCreateStreamKHR(display,
            args->fifoMode ? streamAttrFIFOMode : streamAttrMailboxMode);
        if (eglStream == EGL_NO_STREAM_KHR) {
            LOG_ERR("EGLStreamInit: Couldn't create eglStream.\n");
            return 0;
        }
        //! [docs_eglstream:eglCreateStreamKHR]

        if(args->standalone == STANDALONE_CONSUMER) {
            EGLNativeFileDescriptorKHR file_descriptor;
            int res;

            // In standalone consumer case get the file descriptor for the EGL stream
            // send to the procucer process.
            file_descriptor = eglGetStreamFileDescriptorKHR(display, eglStream);
            if(file_descriptor == EGL_NO_FILE_DESCRIPTOR_KHR) {
                LOG_ERR("EGLStreamInit: Cannot get EGL file descriptor\n");
                return 0;
            }
            LOG_DBG("Consumer file descriptor: %d\n", file_descriptor);
            res = EGLStreamSendfd(SOCK_PATH, file_descriptor);
            if(res == -1) {
                LOG_ERR("EGLStreamInit: Cannot send EGL file descriptor to socket: %s\n", SOCK_PATH);
                return 0;
            }
            close(file_descriptor);
        }
    } else {
        // Standalone producer
        EGLNativeFileDescriptorKHR file_descriptor;
        EGLint streamState = 0;

        // Get the file descriptor of the stream from the consumer process
        // and re-create the EGL stream from it.
        file_descriptor = EGLStreamReceivefd(SOCK_PATH);
        if(file_descriptor == -1) {
            LOG_ERR("EGLStreamInit: Cannot receive EGL file descriptor to socket: %s\n", SOCK_PATH);
            return 0;
        }
        LOG_DBG("Producer file descriptor: %d\n", file_descriptor);
        eglStream = eglCreateStreamFromFileDescriptorKHR(
                        display, file_descriptor);
        close(file_descriptor);

        if (eglStream == EGL_NO_STREAM_KHR) {
            LOG_ERR("EGLStreamInit: Couldn't create EGL Stream from fd.\n");
            return 0;
        }
        if(!eglQueryStreamKHR(
                display,
                eglStream,
                EGL_STREAM_STATE_KHR,
                &streamState)) {
            LOG_ERR("EGLStreamInit: eglQueryStreamKHR EGL_STREAM_STATE_KHR failed\n");
            return 0;
        }
        PrintEGLStreamState(streamState);
    }
#else  //NVMEDIA_GHSI
    if((args->standalone != STANDALONE_PRODUCER) && (args->standalone != STANDALONE_CONSUMER))  {
        //! [docs_eglstream:eglCreateStreamKHR]
        // Standalone consumer or no standalone mode
        eglStream = eglCreateStreamKHR(display,
                                       args->fifoMode ? streamAttrFIFOMode : streamAttrMailboxMode);
        if (eglStream == EGL_NO_STREAM_KHR) {
            LOG_ERR("EGLStreamInit: Couldn't create eglStream.\n");
            return 0;
        }
    }
    //! [docs_eglstream:eglCreateStreamKHR]
    if(args->standalone == STANDALONE_CONSUMER) {
        EGLint streamState = 0;
        eglStream = CreateConsumerEGLStream(display,
                                            args->fifoMode ? streamAttrFIFOMode : streamAttrMailboxMode,
                                            args);
        if (eglStream == EGL_NO_STREAM_KHR) {
            LOG_ERR("EGLStreamInit: Failed to create consumer eglstream\n");
            return 0;
        }
        PrintEGLStreamState(streamState);
    }
    else if (args->standalone == STANDALONE_PRODUCER) {
        //standalone producer
        EGLint streamState = 0;
        eglStream = CreateProducerEGLStream(display,
                                            args->fifoMode ? streamAttrFIFOMode : streamAttrMailboxMode,
                                            args);
        if (eglStream == EGL_NO_STREAM_KHR) {
            LOG_ERR("EGLStreamInit: Failed to create eglstream\n");
            return 0;
        }
        PrintEGLStreamState(streamState);
    }

#endif //NVMEDIA_GHSI

    // Set stream attribute
    if(!eglStreamAttribKHR(display, eglStream, EGL_CONSUMER_LATENCY_USEC_KHR, 16000)) {
        LOG_ERR("Consumer: eglStreamAttribKHR EGL_CONSUMER_LATENCY_USEC_KHR failed\n");
    }
    if(!eglStreamAttribKHR(display, eglStream, EGL_CONSUMER_ACQUIRE_TIMEOUT_USEC_KHR, acquireTimeout)) {
        LOG_ERR("Consumer: eglStreamAttribKHR EGL_CONSUMER_ACQUIRE_TIMEOUT_USEC_KHR failed\n");
    }

    // Get stream attributes
    if(!eglQueryStreamKHR(display, eglStream, EGL_STREAM_FIFO_LENGTH_KHR, &fifo_length)) {
        LOG_ERR("Consumer: eglQueryStreamKHR EGL_STREAM_FIFO_LENGTH_KHR failed\n");
    }
    if(!eglQueryStreamKHR(display, eglStream, EGL_CONSUMER_LATENCY_USEC_KHR, &latency)) {
        LOG_ERR("Consumer: eglQueryStreamKHR EGL_CONSUMER_LATENCY_USEC_KHR failed\n");
    }
    if(!eglQueryStreamKHR(display, eglStream, EGL_CONSUMER_ACQUIRE_TIMEOUT_USEC_KHR, &timeout)) {
        LOG_ERR("Consumer: eglQueryStreamKHR EGL_CONSUMER_ACQUIRE_TIMEOUT_USEC_KHR failed\n");
    }

    if(args->fifoMode != (fifo_length > 0)) {
        LOG_ERR("EGLStreamInit: EGL Stream consumer - Unable to set FIFO mode\n");
        args->fifoMode = GL_FALSE;
    }
    if(args->fifoMode)
        printf("EGL Stream consumer - Mode: FIFO Length: %d\n",  fifo_length);
    else
        printf("EGL Stream consumer - Mode: Mailbox\n");
    printf("EGL Stream consumer - Latency: %d usec\n", latency);
    printf("EGL Stream consumer - Timeout: %d usec\n", timeout);

    LOG_DBG("EGLStreamInit - End\n");
    return eglStream;
}


void EGLStreamFini(EGLDisplay display,EGLStreamKHR eglStream)
{
    //eglStreamConsumerReleaseKHR(display, eglStream);
    if ((display != EGL_NO_DISPLAY) && (eglStream != EGL_NO_STREAM_KHR)) {
        eglDestroyStreamKHR(display, eglStream);
    }
#ifdef NVMEDIA_GHSI
    if (gsock != -1) {
        close(gsock);
        gsock = -1;
    }
#endif
}
