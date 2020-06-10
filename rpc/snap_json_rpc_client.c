#include "snap_json_rpc_client.h"

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
	close(client->sockfd);
	free(client);
}
