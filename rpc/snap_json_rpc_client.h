/*
 * Copyright (c) 2020 Mellanox Technologies, Inc.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef SNAP_JSON_RPC_CLIENT_H
#define SNAP_JSON_RPC_CLIENT_H

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <errno.h>
#include <poll.h>

#include <fcntl.h>
#include <unistd.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>

#define SNAP_JSON_RPC_RECV_BUF_SIZE_INIT (8 * 1024)
#define SNAP_JSON_RPC_RECV_BUF_MAX_SIZE (128 * 1024)

struct snap_json_rpc_client_response {
	char *buf;
	size_t length;
};

struct snap_json_rpc_client {
	int sockfd;
	bool connected;

	bool rsp_ready;
	size_t recv_buf_size;
	size_t recv_offset;
	char *recv_buf;

	size_t send_len;
	size_t send_offset;
	char *send_buf;

};

struct snap_json_rpc_client *snap_json_rpc_client_open(const char *addr);
void snap_json_rpc_client_close(struct snap_json_rpc_client *client);
int snap_json_rpc_client_send_req(struct snap_json_rpc_client *client,
				  void *buf, size_t length);
int snap_json_rpc_wait_for_response(struct snap_json_rpc_client *client);
void snap_json_rpc_client_reset_send_buf(struct snap_json_rpc_client *client);
void snap_json_rpc_put_response(struct snap_json_rpc_client_response *rsp);
struct snap_json_rpc_client_response*
snap_json_rpc_get_response(struct snap_json_rpc_client *client);

#endif
