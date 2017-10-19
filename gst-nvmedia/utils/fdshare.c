/*
 * fdshare.c
 *
 * Copyright (c) 2015-2016, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "fdshare.h"

// #define DEBUG_PRINT(format, arg...) printf(format, ## arg)
#define DEBUG_PRINT(format, arg...)

/* Send <fd_to_send> (a file descriptor) to another process */
/* over a unix domain socket named <socket_name>.           */
/* <socket_name> can be any nonexistant filename.           */
int send_fd(const char *socket_name, int fd_to_send)
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
        perror("send_fd: socket");
        return -1;
    }
    DEBUG_PRINT("send_fd: sock_fd: %d\n", sock_fd);

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

    DEBUG_PRINT("send_fd: Wait is done\n");

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
    DEBUG_PRINT("send_fd: sendmsg: res: %d\n", res);

    if(res <= 0) {
        perror("send_fd: sendmsg");
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
int receive_fd(const char *socket_name)
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
        perror("receive_fd: socket");
        return -1;
    }
    printf("receive_fd: listen_fd: %d\n", listen_fd);

    unlink(socket_name);

    memset(&sock_addr, 0, sizeof(struct sockaddr_un));
    sock_addr.sun_family = AF_UNIX;
    strncpy(sock_addr.sun_path,
            socket_name,
            sizeof(sock_addr.sun_path)-1);

    if (bind(listen_fd,
             (const struct sockaddr*)&sock_addr,
             sizeof(struct sockaddr_un))) {
        perror("receive_fd: bind");
        return -1;
    }

    if (listen(listen_fd, 1)) {
        perror("receive_fd: listen");
        return -1;
    }

    connect_fd = accept(
                    listen_fd,
                    (struct sockaddr *)&connect_addr,
                    &connect_addr_len);
    printf("receive_fd: connect_fd: %d\n", connect_fd);
    close(listen_fd);
    unlink(socket_name);
    if (connect_fd < 0) {
        perror("receive_fd: accept");
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
        perror("receive_fd: recvmsg");
        return -1;
    }

    cmsg = CMSG_FIRSTHDR(&msg);
    if (!cmsg) {
        DEBUG_PRINT("receive_fd: NULL message header\n");
        return -1;
    }
    if (cmsg->cmsg_level != SOL_SOCKET) {
        DEBUG_PRINT("receive_fd: Message level is not SOL_SOCKET\n");
        return -1;
    }
    if (cmsg->cmsg_type != SCM_RIGHTS) {
        DEBUG_PRINT("receive_fd: Message type is not SCM_RIGHTS\n");
        return -1;
    }

    data = CMSG_DATA(cmsg);
    recvfd = *(int *)data;

    return recvfd;
}
