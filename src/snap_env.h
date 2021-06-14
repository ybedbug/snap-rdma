/*
 * Copyright (c) 2021 Nvidia Corporation.  All rights reserved.
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
 *	copyright notice, this list of conditions and the following
 *	disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *	copyright notice, this list of conditions and the following
 *	disclaimer in the documentation and/or other materials
 *	provided with the distribution.
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
 * Returns: 0 on success, otherwize -EINVAL.
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
int snap_env_getenv(const char *env_name);

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
 * @iter:       inernal iterator
 * @buf:	buffer to save environment data
 * @buf_size:   buffer size
 *
 * NOTES: Should be used during snap RPC call.
 *
 * Returns: iterator to the next environment entry or NULL.
 */
snap_env_iter_t snap_env_dump_env_entry(snap_env_iter_t iter, char *buf, unsigned int buf_size);

#endif
