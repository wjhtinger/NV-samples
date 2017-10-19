/*
 * eglstrm_setup.c
 *
 * Copyright (c) 2015, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "log_utils.h"
#include "eglstrm_setup.h"

EXTENSION_LIST(EXTLST_EXTERN)

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
        LOG_ERR("send_fd: socket");
        return -1;
    }
    LOG_DBG("send_fd: sock_fd: %d\n", sock_fd);

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

    LOG_DBG("send_fd: Wait is done\n");

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
    LOG_DBG("send_fd: sendmsg: res: %d\n", res);

    if(res <= 0) {
        LOG_ERR("send_fd: sendmsg");
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
        LOG_ERR("receive_fd: socket");
        return -1;
    }
    LOG_DBG("receive_fd: listen_fd: %d\n", listen_fd);

    unlink(socket_name);

    memset(&sock_addr, 0, sizeof(struct sockaddr_un));
    sock_addr.sun_family = AF_UNIX;
    strncpy(sock_addr.sun_path,
            socket_name,
            sizeof(sock_addr.sun_path)-1);

    if (bind(listen_fd,
             (const struct sockaddr*)&sock_addr,
             sizeof(struct sockaddr_un))) {
        LOG_ERR("receive_fd: bind");
        return -1;
    }

    if (listen(listen_fd, 1)) {
        LOG_ERR("receive_fd: listen");
        return -1;
    }

    connect_fd = accept(
                    listen_fd,
                    (struct sockaddr *)&connect_addr,
                    &connect_addr_len);
    LOG_DBG("receive_fd: connect_fd: %d\n", connect_fd);
    close(listen_fd);
    unlink(socket_name);
    if (connect_fd < 0) {
        LOG_ERR("receive_fd: accept");
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
        LOG_ERR("receive_fd: recvmsg");
        return -1;
    }

    cmsg = CMSG_FIRSTHDR(&msg);
    if (!cmsg) {
        LOG_DBG("receive_fd: NULL message header\n");
        return -1;
    }
    if (cmsg->cmsg_level != SOL_SOCKET) {
        LOG_DBG("receive_fd: Message level is not SOL_SOCKET\n");
        return -1;
    }
    if (cmsg->cmsg_type != SCM_RIGHTS) {
        LOG_DBG("receive_fd: Message type is not SCM_RIGHTS\n");
        return -1;
    }

    data = CMSG_DATA(cmsg);
    recvfd = *(int *)data;

    return recvfd;
}

/* Print current EGLStream state information*/
void EGLStreamPrintStateInfo(EGLint streamState)
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
    printf("%s:: invalid state %d\n", __func__, streamState);
}

static EGLStreamKHR
EGLStreamCreate(EGLDisplay display,
                NvBool fifoMode)
{
    static const EGLint streamAttrMailboxMode[] = { EGL_NONE };
    static const EGLint streamAttrFIFOMode[] = { EGL_STREAM_FIFO_LENGTH_KHR, 4, EGL_NONE };

    return(eglCreateStreamKHR(display,
                fifoMode ? streamAttrFIFOMode : streamAttrMailboxMode));
}

static int EGLStreamSetAttr(EGLDisplay display,
                            EGLStreamKHR eglStream)
{
    EGLint fifo_length = 0, latency = 0, timeout = 0;
    // Set stream attribute
    if(!eglStreamAttribKHR(display, eglStream, EGL_CONSUMER_LATENCY_USEC_KHR, 16000)) {
        LOG_ERR("EGLStreamSetAttr: eglStreamAttribKHR EGL_CONSUMER_LATENCY_USEC_KHR failed\n");
        return 0;
    }
    if(!eglStreamAttribKHR(display, eglStream, EGL_CONSUMER_ACQUIRE_TIMEOUT_USEC_KHR, 16000)) {
        LOG_ERR("EGLStreamSetAttr: eglStreamAttribKHR EGL_CONSUMER_ACQUIRE_TIMEOUT_USEC_KHR failed\n");
        return 0;
    }

    // Get stream attributes
    if(!eglQueryStreamKHR(display, eglStream, EGL_STREAM_FIFO_LENGTH_KHR, &fifo_length)) {
        LOG_ERR("%s: eglQueryStreamKHR EGL_STREAM_FIFO_LENGTH_KHR failed\n", __func__);
    }
    if(!eglQueryStreamKHR(display, eglStream, EGL_CONSUMER_LATENCY_USEC_KHR, &latency)) {
        LOG_ERR("Consumer: eglQueryStreamKHR EGL_CONSUMER_LATENCY_USEC_KHR failed\n");
    }
    if(!eglQueryStreamKHR(display, eglStream, EGL_CONSUMER_ACQUIRE_TIMEOUT_USEC_KHR, &timeout)) {
        LOG_ERR("Consumer: eglQueryStreamKHR EGL_CONSUMER_ACQUIRE_TIMEOUT_USEC_KHR failed\n");
    }

    if(fifo_length)
        LOG_DBG("EGL Stream consumer - Mode: FIFO Length: %d\n",  fifo_length);
    else
        LOG_DBG("EGL Stream consumer - Mode: Mailbox\n");
    LOG_DBG("EGL Stream consumer - Latency: %d usec\n", latency);
    LOG_DBG("EGL Stream consumer - Timeout: %d usec\n", timeout);

    return 1;
}

EglStreamClient *
EGLStreamSingleProcInit(EGLDisplay display,
                        NvBool fifoMode)
{
    EglStreamClient *client = NULL;

    client = malloc(sizeof(EglStreamClient));
    if (!client) {
        LOG_ERR("%s:: failed to alloc memory\n", __func__);
        return NULL;
    }

    client->eglStream = EGLStreamCreate(display, fifoMode);

    if (client->eglStream == EGL_NO_STREAM_KHR) {
        LOG_ERR("%s: Couldn't create eglStream.\n", __func__);
        goto fail;
    }

    client->display = display;

    if (!EGLStreamSetAttr(display, client->eglStream)) {
        LOG_ERR("%s: EGLStreamSetAttr failed\n", __func__);
        eglDestroyStreamKHR(client->display, client->eglStream);
        goto fail;
    }

    LOG_DBG("%s - Finished\n", __func__);
    return client;

fail:
    free(client);
    return NULL;
}

EglStreamClient *
EGLStreamProducerProcInit(EGLDisplay display,
                          NvBool fifoMode)
{
    EglStreamClient *client = NULL;
    EGLNativeFileDescriptorKHR fileDescriptor;
    EGLint streamState = 0;

    client = malloc(sizeof(EglStreamClient));
    if (!client) {
        LOG_ERR("%s:: failed to alloc memory\n", __func__);
        return NULL;
    }

    // Get the file descriptor of the stream from the consumer process
    // and re-create the EGL stream from it.
    fileDescriptor = EGLStreamReceivefd(SOCK_PATH);
    if(fileDescriptor == -1) {
        LOG_ERR("%s: Cannot receive EGL file descriptor to socket: %s\n", __func__, SOCK_PATH);
        goto fail;
    }

    LOG_DBG("Producer file descriptor: %d\n", fileDescriptor);
    client->eglStream = eglCreateStreamFromFileDescriptorKHR(display,
                                                       fileDescriptor);
    close(fileDescriptor);

    if (client->eglStream == EGL_NO_STREAM_KHR) {
       LOG_ERR("%s: Couldn't create EGL Stream from fd.\n", __func__);
       goto fail;
    }

    client->display = display;

    if(!eglQueryStreamKHR(display,
                          client->eglStream,
                          EGL_STREAM_STATE_KHR,
                          &streamState)) {
       LOG_ERR("%s: eglQueryStreamKHR EGL_STREAM_STATE_KHR failed\n", __func__);
       eglDestroyStreamKHR(client->display, client->eglStream);
       goto fail;
    }

    EGLStreamPrintStateInfo(streamState);

    if (!EGLStreamSetAttr(display, client->eglStream)) {
        LOG_ERR("%s: EGLStreamSetAttr failed\n", __func__);
        eglDestroyStreamKHR(client->display, client->eglStream);
        goto fail;
    }

    LOG_DBG("%s - Finished\n", __func__);

    return client;

fail:
    free(client);
    return NULL;
}

EglStreamClient *
EGLStreamConsumerProcInit(EGLDisplay display,
                          NvBool fifoMode)
{
    EglStreamClient *client = NULL;
    EGLNativeFileDescriptorKHR fileDescriptor;
    int res;

    client = malloc(sizeof(EglStreamClient));
    if (!client) {
        LOG_ERR("%s:: failed to alloc memory\n", __func__);
        return NULL;
    }

    client->eglStream = EGLStreamCreate(display, fifoMode);

    if (client->eglStream == EGL_NO_STREAM_KHR) {
        LOG_ERR("%s: Couldn't create eglStream.\n", __func__);
        goto fail;
    }
    client->display = display;

    // In consumer process, get the file descriptor for the EGL stream
    // and send to the producer process.
    fileDescriptor = eglGetStreamFileDescriptorKHR(display, client->eglStream);
    if(fileDescriptor == EGL_NO_FILE_DESCRIPTOR_KHR) {
       LOG_ERR("%s: Cannot get EGL file descriptor\n", __func__);
       eglDestroyStreamKHR(client->display, client->eglStream);
       goto fail;
    }

    LOG_DBG("%s: Consumer file descriptor: %d\n", __func__, fileDescriptor);
    res = EGLStreamSendfd(SOCK_PATH, fileDescriptor);
    if(res == -1) {
       LOG_ERR("%s: Cannot send EGL file descriptor to socket: %s\n", __func__, SOCK_PATH);
       close(fileDescriptor);
       goto fail;
    }

    close(fileDescriptor);

    if (!EGLStreamSetAttr(display, client->eglStream)) {
        LOG_ERR("%s: EGLStreamSetAttr failed\n", __func__);
        eglDestroyStreamKHR(client->display, client->eglStream);
        goto fail;
    }

    LOG_DBG("%s - Finished\n", __func__);

    return client;

fail:
    free(client);
    return NULL;
}

void EGLStreamFini(EglStreamClient *client)
{
    if (client) {
        //eglStreamConsumerReleaseKHR(client.display, eglStream);
        LOG_DBG("EGLStreamFini: Destroy egl stream\n");
        eglDestroyStreamKHR(client->display, client->eglStream);

        /* release the resource */
        free(client);
    }
}
