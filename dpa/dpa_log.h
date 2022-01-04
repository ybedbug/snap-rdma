/*
 * Copyright © 202 NVIDIA CORPORATION & AFFILIATES. ALL RIGHTS RESERVED.
 *
 * This software product is a proprietary product of Nvidia Corporation and its affiliates
 * (the "Company") and all right, title, and interest in and to the software
 * product, including all associated intellectual property rights, are and
 * shall remain exclusively with the Company.
 *
 * This software product is governed by the End User License Agreement
 * provided with the software product.
 */
#ifndef _DPA_LOG_H
#define _DPA_LOG_H

/* TODO: replace snap_* logger with this */

enum {
	DPA_LOG_LEVEL_MAX = 0,
	DPA_LOG_LEVEL_DBG,
	DPA_LOG_LEVEL_INFO,
	DPA_LOG_LEVEL_WARN,
	DPA_LOG_LEVEL_ERR,
};

#define _DPA_LOG_COMMON(_level, fmt, ...) \
	do { \
		dpa_logger(__FILE__, __LINE__, DPA_LOG_LEVEL_##_level, \
			#_level, fmt, ## __VA_ARGS__);  \
	} while (0);


#if DPA_TRACE_DATA
#define dpa_debug_data(_fmt, ...) \
	_DPA_LOG_COMMON(DBG, _fmt, ## __VA_ARGS__)
#else
#define dpa_debug_data(_fmt, ...)
#endif

#ifdef SNAP_DEBUG
#define dpa_debug(_fmt, ...) \
	_DPA_LOG_COMMON(DBG, _fmt, ## __VA_ARGS__)
#else
#define dpa_debug(_fmt, ...)
#endif

#define dpa_info(_fmt, ...) \
	_DPA_LOG_COMMON(INFO, _fmt, ## __VA_ARGS__)

#define dpa_warn(_fmt, ...) \
	_DPA_LOG_COMMON(WARN, _fmt, ## __VA_ARGS__)

#define dpa_error(_fmt, ...) \
	_DPA_LOG_COMMON(ERR, _fmt, ## __VA_ARGS__)

#define dpa_fatal(_fmt, ...) \
	do { \
		_DPA_LOG_COMMON(ERR, _fmt, ## __VA_ARGS__) \
		dpa_error_freeze(); \
	} while(0);

#define dpa_assertv_always(_expr, _fmt, ...) \
	do { \
		if (!(_expr)) { \
			dpa_fatal("assertion failure: %s " _fmt, #_expr, ## __VA_ARGS__); \
		} \
	} while(0);

void dpa_logger(const char *file_name, unsigned int line_num,
		int level, const char *level_c, const char *format, ...);
void dpa_error_freeze();
#endif
