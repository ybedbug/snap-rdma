#include "snap_json_rpc_client.h"

/*
 * Must be called with client lock held
 */
static int snap_recv_buf_expand(struct snap_json_rpc_client *client)
{
	void *new_buf;

	if (client->recv_buf_size * 2 > SNAP_JSON_RPC_RECV_BUF_MAX_SIZE)
		return -ENOSPC;

	new_buf = realloc(client->recv_buf, client->recv_buf_size * 2);
	if (!new_buf)
		return -ENOMEM;

	client->recv_buf = new_buf;
	client->recv_buf_size *= 2;

	return 0;
}

/* TODO: implement parsing of buffer to json file */
static int snap_json_parse(void *buf, size_t len)
{
	return 0;
}

/*
 * Must be called with client lock held
 */
static int snap_json_rpc_client_sock_recv(struct snap_json_rpc_client *client)
{
	int ret;

	if (!client->recv_buf) {
		client->recv_buf = calloc(1, SNAP_JSON_RPC_RECV_BUF_SIZE_INIT);
		if (!client->recv_buf)
			return -ENOMEM;
		client->recv_buf_size = SNAP_JSON_RPC_RECV_BUF_SIZE_INIT;
		client->recv_offset = 0;
		client->rsp_ready = false;
	} else if (client->recv_offset == client->recv_buf_size - 1) {
		ret = snap_recv_buf_expand(client);
		if (ret)
			return ret;
	}

	ret = recv(client->sockfd, client->recv_buf + client->recv_offset,
		   client->recv_buf_size - client->recv_offset - 1, 0);
	if (ret < 0) {
		/* For EINTR we pretend that nothing was reveived. */
		if (errno == EINTR)
			return 0;
		else
			return -errno;
	} else if (ret == 0) {
		return -EIO;
	}

	client->recv_offset += ret;
	client->recv_buf[client->recv_offset] = '\0';

	if (!snap_json_parse(client->recv_buf, client->recv_offset + 1))
		client->rsp_ready = true;
	else
		client->rsp_ready = false;

	return 0;
}

/*
 * Must be called with client lock held
 */
static int snap_json_rpc_client_sock_send(struct snap_json_rpc_client *client)
{
	int ret = 0;

	if (!client->send_buf)
		goto out;

	if (client->send_len > 0) {
		ret = send(client->sockfd,
			   client->send_buf + client->send_offset,
			   client->send_len, MSG_NOSIGNAL);
		if (ret < 0) {
			/* For EINTR we pretend that nothing was send. */
			if (errno == EINTR)
				ret = 0;
			else
				ret = -errno;

			goto out;
		}
		client->send_offset += ret;
		client->send_len -= ret;
		ret = 0;
	}

	if (client->send_len == 0)
		snap_json_rpc_client_reset_send_buf(client);

out:
	return ret;
}

/*
 * snap_json_rpc_client_poll() - Poll client socket for completing send/recv
 *                               operations.
 *
 * @client:       snap json rpc client
 *
 * Must be called with client lock held.
 *
 * Return: 1 on success. 0 will indicate that one should try again.
 *         Otherwise, a negative value will indicate on the failure.
 */
static int snap_json_rpc_client_poll(struct snap_json_rpc_client *client)
{
	struct pollfd pfd = {.fd = client->sockfd, .events = POLLIN | POLLOUT};
	int ready = 0;
	int ret;

	ret = poll(&pfd, 1, 1);
	if (ret == -1) {
		if (errno == EINTR)
			/* For EINTR we pretend that nothing was received/send. */
			ret = 0;
		else
			ret = -errno;
	} else if (ret > 0) {
		ret = 0;
		/* try sending a request */
		if (pfd.revents & POLLOUT)
			ret = snap_json_rpc_client_sock_send(client);

		/* try to receive */
		if (ret == 0 && (pfd.revents & POLLIN)) {
			ret = snap_json_rpc_client_sock_recv(client);
			/* Incomplete message in buffer isn't an error. */
			if (ret == -EAGAIN)
				ret = 0;
			else if (ret == 0 && client->rsp_ready)
				ready = 1;
		}
	}

	return ret ? ret : ready;
}

/**
 * snap_json_rpc_client_reset_send_buf - Reset the send buffer which inited
 *                                       for this rpc client
 *
 * @client:       snap json rpc client
 */
void snap_json_rpc_client_reset_send_buf(struct snap_json_rpc_client *client)
{
	if (client->send_buf) {
		free(client->send_buf);
		client->send_buf = NULL;
		client->send_len = 0;
		client->send_offset = 0;
	}
}

/**
 * snap_json_rpc_put_response - Return the rpc response that was previously
 *                              taken from the rpc client.
 *
 * @rsp:       snap json rpc response object
 */
void snap_json_rpc_put_response(struct snap_json_rpc_client_response *rsp)
{
	free(rsp->buf);
	free(rsp);
}

/**
 * snap_json_rpc_get_response - Get the last response that was received from
 *                              the rpc server.
 *
 * @client:       snap json rpc client
 *
 * Return: Response object on success. Null Otherwise.
 */
struct snap_json_rpc_client_response*
snap_json_rpc_get_response(struct snap_json_rpc_client *client)
{
	struct snap_json_rpc_client_response *rsp;

	if (!client->rsp_ready)
		goto out_err;

	rsp = calloc(1, sizeof(*rsp));
	if (!rsp)
		goto out_err;

	rsp->length = client->recv_offset + 1;
	rsp->buf = calloc(1, rsp->length);
	if (!rsp->buf)
		goto out_free;

	memcpy(rsp->buf, client->recv_buf, rsp->length);
	/* after consuming the response, we can rewind the offset */
	client->recv_offset = 0;
	client->rsp_ready = false;

	return rsp;

out_free:
	free(rsp);
out_err:
	return NULL;
}

/**
 * snap_json_rpc_wait_for_response - Receive response for the previously sent
 *                                   request buffer to rpc server.
 *
 * @client:       snap json rpc client
 *
 * Return: 0 on success. Otherwise, a negative value will indicate on the failure.
 */
int snap_json_rpc_wait_for_response(struct snap_json_rpc_client *client)
{
	int ret = 0;

	while (ret == 0 || ret == -ENOTCONN)
		ret = snap_json_rpc_client_poll(client);

	/* snap_json_rpc_client_poll return 1 on success */
	if (ret == 1)
		ret = 0;

	return ret;
}

/**
 * snap_json_rpc_client_send_req() - Send request buffer to rpc server.
 *
 * @client:       snap json rpc client
 * @buf:          Buffer to send to rpc server
 * @length:       length of the buffer to send
 *
 * Send json RPC buffer to the server. Client is allowed to send 1 request at a
 * given time and synchronize the response. In order to receive a response for
 * the posted request, one should call snap_json_rpc_wait_for_response before
 * issuing a new request.
 *
 * Return: 0 on success. Otherwise, a negative value will indicate on the failure.
 */
int snap_json_rpc_client_send_req(struct snap_json_rpc_client *client,
				  void *buf, size_t length)
{
	int ret = 0;
	void *json_start, *json_end;

	if (!client->connected || client->send_buf) {
		ret = -EAGAIN;
		goto out;
	}

	/* Invalid json file */
	if (snap_json_parse(buf, length)) {
		ret = -EINVAL;
		goto out;
	}

	/*
	 * For now we only check that the buffer starts with { char and ends
	 * with } char. All other checks will be done by the server.
	 */
	json_start = strchr(buf, '{');
	if (!json_start) {
		//the buffer is not in a valid json format
		ret = -EINVAL;
		goto out;
	}
	json_end = strrchr(buf, '}');
	if (!json_end) {
		//the buffer is not in a valid json format
		ret = -EINVAL;
		goto out;
	}

	client->send_buf = calloc(1, length);
	if (!client->send_buf) {
		ret = -ENOMEM;
		goto out;
	}
	memcpy(client->send_buf, buf, length);
	/* make sure we send only the json object */
	client->send_offset = json_start - buf;
	client->send_len = json_end - json_start + 1;
out:
	return ret;
}

/**
 * snap_json_rpc_client_open() - Opens and create a new snap json rpc client
 * @addr:       Address of the json rpc server
 *
 * Return: Returns snap_json_rpc_client in case of success, NULL otherwise and
 * errno will be set to indicate the failure reason.
 */
struct snap_json_rpc_client *snap_json_rpc_client_open(const char *addr)
{
	struct snap_json_rpc_client *client;
	struct sockaddr_un addr_un = {}; /* Unix Domain Socket supported */
	int ret, flags;

	client = calloc(1, sizeof(*client));
	if (!client) {
		errno = -ENOMEM;
		goto out_err;
	}

	addr_un.sun_family = AF_UNIX;
	ret = snprintf(addr_un.sun_path, sizeof(addr_un.sun_path), "%s", addr);
	if (ret < 0 || (size_t)ret >= sizeof(addr_un.sun_path)) {
		errno = -EINVAL;
		goto out_free;
	}

	client->sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (client->sockfd < 0)
		goto out_free;

	flags = fcntl(client->sockfd, F_GETFL);
	if (flags < 0 || fcntl(client->sockfd, F_SETFL,
			       flags | O_NONBLOCK) < 0)
		goto out_free_socket;

	ret = connect(client->sockfd, (struct sockaddr *)&addr_un,
		      sizeof(addr_un));
	if (ret)
		goto out_free_socket;
	else
		client->connected = true;

	return client;

out_free_socket:
	close(client->sockfd);
out_free:
	free(client);
out_err:
	return NULL;
}

/**
 * snap_json_rpc_client_close() - Destroy a snap json rpc client
 * @client:       snap json rpc client
 *
 * Destroy and free a snap json rpc client.
 */
void snap_json_rpc_client_close(struct snap_json_rpc_client *client)
{
	if (client->recv_buf)
		free(client->recv_buf);
	if (client->send_buf)
		free(client->send_buf);
	close(client->sockfd);
	free(client);
}
