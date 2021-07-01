/*
 * Copyright (c) 2020 Nvidia Corporation. All rights reserved.
 */

#include <malloc.h>

#include <libflexio/flexio.h>
#include <libflexio/flexio_elf.h>

#include "hello_flexio_com.h"
#include "com_host.h"
#include "com_flow_host.h"

struct app_context {
	struct ibv_context *ibv_ctx;
	struct flexio_process *flexio_process;
	struct flexio_event_handler *flexio_event_handler;
};

#define HELLO_FLEXIO_ENTRY_POINT "net_event_handler"
#define ENTRY_POINT_INIT "event_handler_init"

#define SMAC 0x02427e7feb02

int main(int argc, char **argv)
{
	struct flexio_cq_attr rqcq_attr, sqcq_attr;
	struct flexio_wq_attr rq_attr, sq_attr;
	flexio_uintptr_t *flexio_db_dest_addr;
	const char *elf_file_path, *dev_name;
	struct hello_flexio_data *app_data;
	uint64_t elf_entry_point, sym_size;
	uint64_t entry_point_init;
	struct flexio_cq *flexio_rq_cq_ptr;
	struct flexio_cq *flexio_sq_cq_ptr;
	struct flexio_rq *flexio_rq_ptr;
	struct flexio_sq *flexio_sq_ptr;
	struct ibv_device **dev_list;
	struct app_context *ctx;
	struct ibv_device *dev;
	uint64_t rpc_func_ret;
	size_t elf_size;
	void *elf_buff;
	int i, err;
	struct flow_matcher *matcher;
	struct flow_rule *rule;

	if (argc != 3) {
		pr_err("Usage: ./hello_flexio <dev_name> <elf_file_path>\n");
		return 1;
	}

	dev_name = argv[1];
	elf_file_path = argv[2];

	pr_out("Welcome to Hello Flex IO app.\n");

	dev_list = ibv_get_device_list(NULL);
	if (!dev_list) {
		pr_err("Failed to get IB devices list");
		return 1;
	}

	for (i = 0; dev_list[i]; i++)
		if (!strcmp(ibv_get_device_name(dev_list[i]), dev_name))
			break;
	dev = dev_list[i];
	if (!dev) {
		pr_err("No IB devices found\n");
		goto err_dev_list;
	}
	pr_out("Registered on device %s\n", ibv_get_device_name(dev));

	ctx = malloc(sizeof(*ctx));
	if (!ctx)
		goto err_dev_list;

	ctx->ibv_ctx = ibv_open_device(dev);
	if (!ctx->ibv_ctx) {
		pr_err("Couldn't get ibv conntext for %s\n",
		       ibv_get_device_name(dev));
		goto err_open_device;
	}
	ibv_free_device_list(dev_list);
	dev_list = NULL;

	pr_out("Parsing ELF file '%s'\n", elf_file_path);
	flexio_get_elf_file(elf_file_path, &elf_buff, &elf_size);
	pr_out("ELF file size is %zdB\n", elf_size);

	app_data = (struct hello_flexio_data *)calloc(1, sizeof(*app_data));
	// Set Reserved field to a magic number in order to verify app data copy to Flex IO
	app_data->reserved = HELLO_FLEXIO_MAGIC;

	flexio_process_create(ctx->ibv_ctx, elf_buff, elf_size, &ctx->flexio_process);
	if (!ctx->flexio_process) {
		pr_err("Failed to create Flex IO process.\n");
		return 1;
	}
	app_data->lkey = flexio_process_get_mkey(ctx->flexio_process);

	flexio_get_elf_func_sym_val(elf_buff, HELLO_FLEXIO_ENTRY_POINT, &elf_entry_point,
				    &sym_size);
	pr_out("ELF event handler symbol offset %#lx\n", elf_entry_point);

	pr_out("Creating event handler\n");
	/* TODO - this is a WA in order to allocate a buffer and copy to it later.
		  Remove once event_handler_run() is properly supported.
	*/
	if (flexio_copy_from_host(ctx->flexio_process, (uintptr_t)app_data,
				  sizeof(struct hello_flexio_data), &flexio_db_dest_addr))
	{
		pr_err("Failed to allocate memory on Flex IO heap\n");
		return 1;
	}

	if (flexio_event_handler_create(ctx->flexio_process, elf_entry_point, *flexio_db_dest_addr,
					NULL, NULL, &ctx->flexio_event_handler))
	{
		pr_err("Failed to create Flex IO event handler\n");
		return 1;
	}

	sqcq_attr.log_cq_ring_size = LOG_CQ_RING_SIZE;
	sqcq_attr.flexio_element_type = FLEXIO_CQ_ELEMENT_TYPE_NON_APU_CQ;
	rqcq_attr.log_cq_ring_size = LOG_CQ_RING_SIZE;
	rqcq_attr.flexio_element_type = FLEXIO_CQ_ELEMENT_TYPE_APU_THREAD;
	rqcq_attr.eqn_or_thread_id = ctx->flexio_event_handler->thread->thread_id;
	sq_attr.log_wq_ring_size = LOG_SQ_RING_SIZE;
	sq_attr.log_data_size = LOG_WQ_DATA_ENTRY_SIZE;
	rq_attr.log_wq_ring_size = LOG_RQ_RING_SIZE;
	rq_attr.log_data_size = LOG_WQ_DATA_ENTRY_SIZE;

	pr_out("Creating WQs.\n");
	pr_out("Creating CQ for SQ.\n");
	if (flexio_cq_create(ctx->flexio_process, ctx->ibv_ctx, &sqcq_attr, &flexio_sq_cq_ptr)) {
		pr_err("Failed to create Flex IO CQ.\n");
		return 1;
	}
	app_data->sq_cq_data = *flexio_sq_cq_ptr->hw_cq;
	pr_out("CQ number %#x was created for SQ.\n", app_data->sq_cq_data.cq_num);

	if (flexio_sq_create(ctx->flexio_process, ctx->ibv_ctx, flexio_sq_cq_ptr->hw_cq->cq_num,
			     &sq_attr, &flexio_sq_ptr)) {
		pr_err("Failed to create Flex IO SQ.\n");
		return 1;
	}
	app_data->sq_data = *flexio_sq_ptr->hw_sq;
	pr_out("SQ number %#x was created.\n", app_data->sq_data.sq_num);

	pr_out("Creating CQ for RQ.\n");
	if (flexio_cq_create(ctx->flexio_process, ctx->ibv_ctx, &rqcq_attr, &flexio_rq_cq_ptr)) {
		pr_err("Failed to create Flex IO CQ.\n");
		return 1;
	}
	app_data->rq_cq_data = *flexio_rq_cq_ptr->hw_cq;
	pr_out("CQ number %#x was created for RQ.\n", app_data->rq_cq_data.cq_num);
	if (flexio_rq_create(ctx->flexio_process, ctx->ibv_ctx, flexio_rq_cq_ptr->hw_cq, &rq_attr,
			     &flexio_rq_ptr)) {
		pr_err("Failed to create Flex IO RQ.\n");
		return 1;
	}
	app_data->rq_data = *flexio_rq_ptr->hw_rq;
	pr_out("RQ number %#x was created.\n", app_data->rq_data.rq_num);

	pr_out("Copy app DB to Flex IO.\n");
	if (flexio_do_ldma_copy_from_host(ctx->flexio_process, (uintptr_t)app_data,
					  sizeof(struct hello_flexio_data), *flexio_db_dest_addr)) {
		pr_err("Failed to copy app data to Flex IO.\n");
		return 1;
	}

	free(app_data);
	pr_out("flexio_event handler app DB dest address %#lx\n", *flexio_db_dest_addr);
	/* TODO - add this back and change event handler create once SimX has support */
	/*
	if (flexio_event_handler_run(ctx->flexio_event_handler, *flexio_db_dest_addr)) {
		pr_err("Failed to run event handler\n");
		return 1;
	}
	*/

	if (flexio_get_elf_func_sym_val(elf_buff, ENTRY_POINT_INIT, &entry_point_init,
					  &sym_size))
	{
		pr_err("Failed to find function symbol for '%s'.\n", ENTRY_POINT_INIT);
		return 1;
	}
	pr_out("RPC INIT offset %#lx\n", entry_point_init);

	if (flexio_process_call(ctx->flexio_process, entry_point_init, *flexio_db_dest_addr, 2, 3,
				  &rpc_func_ret))
	{
		pr_err("Remote process call failed. Press Enter to exit\n");
		getchar();
		return 1;
	}

	pr_out("Create flow steering rules.\n");
	matcher = create_matcher(ctx->ibv_ctx);
	rule = create_rule(matcher, flexio_rq_ptr->tir, SMAC);

	pr_out("Waiting for traffic, press any key to exit.\n");
	getchar();
	pr_out("Hello Flex IO is done.\n");

	/* Clean-up */
	free(elf_buff);

	err = destroy_rule(rule);
	if (err) {
		pr_out("Failed to destroy rule (err %d)\n", err);
		goto err_ret;
	}

	err = destroy_matcher(matcher);
	if (err) {
		pr_out("Failed to destroy matcher (err %d)\n", err);
		goto err_ret;
	}

	err = flexio_memory_free(flexio_db_dest_addr);
	if (err) {
		pr_out("Failed to dealloc application data memory on Flex IO heap (err %d)\n", err);
		goto err_ret;
	}

	err = flexio_rq_destroy(ctx->flexio_process, flexio_rq_ptr);
	if (err) {
		pr_out("Failed to destroy RQ (err %d)\n", err);
		goto err_ret;
	}

	err = flexio_sq_destroy(ctx->flexio_process, flexio_sq_ptr);
	if (err) {
		pr_out("Failed to destroy SQ (err %d)\n", err);
		goto err_ret;
	}

	err = flexio_cq_destroy(ctx->flexio_process, flexio_rq_cq_ptr);
	if (err) {
		pr_out("Failed to destroy CQ (err %d)\n", err);
		goto err_ret;
	}

	err = flexio_cq_destroy(ctx->flexio_process, flexio_sq_cq_ptr);
	if (err) {
		pr_out("Failed to destroy CQ (err %d)\n", err);
		goto err_ret;
	}

	err = flexio_event_handler_destroy(ctx->flexio_process, ctx->flexio_event_handler);
	if (err) {
		pr_out("Failed to destroy event handler (err %d)\n", err);
		goto err_ret;
	}

	err = flexio_process_destroy(ctx->flexio_process);
	if (err) {
		pr_out("Failed to destroy process (err %d)\n", err);
		goto err_ret;
	}

	err = ibv_close_device(ctx->ibv_ctx);
	if (err) {
		pr_out("Failed to close IBV device (err %d)\n", err);
		goto err_ret;
	}
	free(ctx);

	return 0;

err_open_device:
	free(ctx);
err_dev_list:
	if (dev_list)
		ibv_free_device_list(dev_list);
err_ret:
	return 1;
}
