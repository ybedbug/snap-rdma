#
# Copyright (c) 2021-2022 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
#
# This software product is a proprietary product of NVIDIA CORPORATION &
# AFFILIATES (the "Company") and all right, title, and interest in and to the
# software product, including all associated intellectual property rights, are
# and shall remain exclusively with the Company.
#
# This software product is governed by the End User License Agreement
# provided with the software product.
#

libsnap_core_include = include_directories('.', '..', flexio_include)

libsnap_core_sources = [
	'mlx5_ifc.h',
	'snap_dma.h',
	'snap_dma_internal.h',
	'snap_dma_stat.h',
	'snap_dpa.h',
	'snap_env.h',
	'snap_macros.h',
	'snap_mb.h',
	'snap_mr.h',
	'snap_qp.h',
	'snap_umr.h',
	'snap_dma.c',
	'snap_dma_control.c',
	'snap_dma_dv.c',
	'snap_dma_verbs.c',
	'snap_dpa.c',
	'snap_env.c',
	'snap_mr.c',
	'snap_qp.c',
	'snap_umr.c'
]

#TODO remove once DOCA moves to work over dev_emu_dma_* API
install_headers('snap_dma.h', 'snap_qp.h', 'snap_macros.h', 'snap_mr.h', 'snap_env.h', 'snap_dma_stat.h')

libsnap_core = static_library('snap_core',
			libsnap_core_sources,
			dependencies : libsnap_core_depends,
			include_directories : libsnap_core_include,
			install: false)

libsnap_core_dep = declare_dependency(
			dependencies : libsnap_core_depends,
			include_directories : libsnap_core_include,
			sources : libsnap_core_sources)

