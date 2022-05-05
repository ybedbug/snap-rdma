/*
 * Copyright © 2021 NVIDIA CORPORATION & AFFILIATES. ALL RIGHTS RESERVED.
 *
 * This software product is a proprietary product of Nvidia Corporation and its affiliates
 * (the "Company") and all right, title, and interest in and to the software
 * product, including all associated intellectual property rights, are and
 * shall remain exclusively with the Company.
 *
 * This software product is governed by the End User License Agreement
 * provided with the software product.
 */

#ifndef SNAP_ENV_H
#define SNAP_ENV_H

/*
 * SNAP RPC feature request
 *
 * Why we need this?
 *
 *  1.	We have a lot of different parameters around SNAP startup and several
 *      values per parameter (/etc/default/mlnx_snap).
 *      Some parameters affect performance, so we need to save them as part of the SNAP Performance report.
 *
 *  2.	On some cases, like:
 *      #SOME_PARAMETER=0
 *      SOME_PARAMETER=0
 *      For SNAP controller this means different behavior and it’s very confusing.
 *      So, parameters must be collected in runtime after controller initialization.
 *
 *  3.	This will be useful during debugging with another team or customer.
 */

/**
 * snap_env_add - add environment variables
 * @env_name:	   environment variable name
 * @default_val:	default value
 *
 * Returns: 0 on success, otherwise -EINVAL.
 */
int snap_env_add(const char *env_name, int default_val);

/**
 * Note: the default value has to reflect how the environment variable
 * shall be used by an appropriate code, even if it was not defined.
 * e.g. The nvme zero copy is enabled by default, even than
 * the NVME_SNAP_ZCOPY not defined.
 */
#define SNAP_ENV_REG_ENV_VARIABLE(env_name, default_val) \
__attribute__((constructor)) void snap_env_register_##env_name(void) \
{\
	snap_env_add(env_name, default_val); \
}

/**
 * snap_env_getenv - get value of environment variable
 * @env_name: environment variable name
 *
 * Returns: -EINVAL on failure, integer value >= 0 upon success
 */
long long snap_env_getenv(const char *env_name);

/**
 * snap_env_is_set - check if the env. variable was set
 * @env_name: environment variable name
 *
 * Returns: 0 if there is no match, otherwise 1.
 */
int snap_env_is_set(const char *env_name);

typedef void *snap_env_iter_t;

/**
 * snap_env_dump_env_entry - dump one environment entry
 * @iter:       internal iterator
 * @buf:	buffer to save environment data
 * @buf_size:   buffer size
 *
 * NOTES: Should be used during snap RPC call.
 *
 * Returns: iterator to the next environment entry or NULL.
 */
snap_env_iter_t snap_env_dump_env_entry(snap_env_iter_t iter, char *buf, unsigned int buf_size);

#endif
