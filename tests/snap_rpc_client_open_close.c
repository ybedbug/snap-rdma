#include <stdio.h>
#include <string.h>

#include "snap_json_rpc_client.h"

int main(int argc, char **argv)
{
	char name[256] = {};
	char fname[256] = {};
	int ret = 0, i = 0, opt, buf_len, loops = 1;
	struct snap_json_rpc_client *client;
	struct snap_json_rpc_client_response *rsp;
	FILE *jsonfile;
	void *buf;

	while ((opt = getopt(argc, argv, "n:f:l:")) != -1) {
		switch (opt) {
		case 'n':
			strncpy(name, optarg, sizeof(name));
			break;
		case 'f':
			strncpy(fname, optarg, sizeof(fname));
			break;
		case 'l':
			loops = atoi(optarg);
			break;
		default:
			printf("Usage: snap_rpc_client_open_close -n <name>"
			       " -f <json file path> -l <loops>\n");
			exit(1);
		}
	}


	client = snap_json_rpc_client_open(name);
	if (!client) {
		printf("failed to create rpc client %s\n", name);
		exit(1);
	}

	if (fname[0] == 0)
		goto out_close;

	jsonfile = fopen(fname, "r");
	if (!jsonfile) {
		printf("failed to open file %s\n", optarg);
		ret = -errno;
		goto out_close;
	}

	fseek(jsonfile, 0L, SEEK_END);
	buf_len = ftell(jsonfile);
	fseek(jsonfile, 0L, SEEK_SET);

	buf = calloc(1, buf_len);
	if (!buf) {
		printf("failed to allocate buffer with %d bytes\n", buf_len);
		ret = -ENOMEM;
		goto out_close_file;
	}

	fread(buf, sizeof(char), buf_len, jsonfile);
	printf("The file called %s contains this text:\n%s\n", fname, buf);

	while (i++ < loops) {
		ret = snap_json_rpc_client_send_req(client, buf, buf_len);
		if (ret) {
			printf("failed to send buf to server ret=%d loop %d\n",
			       ret, i);
			goto out_free;
		}

		ret = snap_json_rpc_wait_for_response(client);
		if (ret) {
			printf("failed to wait for rsp buf from server "
			       "ret=%d loop %d\n",
			       ret, i);
			goto out_free;
		}

		rsp = snap_json_rpc_get_response(client);
		if (!rsp) {
			printf("failed to get rsp from client\n");
			goto out_free;
		}

		printf("got %d bytes rsp from client:\n%s\n", rsp->length, rsp->buf);
		snap_json_rpc_put_response(rsp);
	}

out_free:
	free(buf);
out_close_file:
	fclose(jsonfile);
out_close:
	snap_json_rpc_client_close(client);

	return ret;
}
