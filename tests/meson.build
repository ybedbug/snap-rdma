#
# Copyright (c) 2021 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
#
# This software product is a proprietary product of NVIDIA CORPORATION &
# AFFILIATES (the "Company") and all right, title, and interest in and to the
# software product, including all associated intellectual property rights, are
# and shall remain exclusively with the Company.
#
# This software product is governed by the End User License Agreement
# provided with the software product.
#

if not get_option('enable-gtest')
	warning('Skipping compilation of tests')
	subdir_done()
endif

cpp_args = [
	'-std=c++11', '-fpermissive'
]

gtest = dependency('gtest_main',
		required : true,
		not_found_message :
		'gtest is not found. Check that PKG_CONFIG_PATH is set. Ex: ' +
		'PKG_CONFIG_PATH=/usr/local/gtest/lib64/pkgconfig/',
		native : true
		)

gtest_snap_dma_srcs = [
	'gtest_example.cc',
	'test_snap_dma.cc',
	'tests_common.cc'
	]

gtest_snap_dma = executable('gtest_snap_dma',
		gtest_snap_dma_srcs,
		cpp_args : cpp_args,
		dependencies : [ libsnap_core_dep, gtest ],
		pie : false,
		install : false,
		native : true
		)

#
# verbose is going to be supported since 0.60
#
test('dma', gtest_snap_dma,
		env : 'MLXDEV=mlx5_0',
		protocol : 'gtest',
		verbose : true)

if flexio.found()

gtest_snap_dpa_srcs = [
	'test_snap_dpa.cc',
	'tests_common.cc'
	]

gtest_snap_dpa = executable('gtest_snap_dpa',
		gtest_snap_dpa_srcs,
		cpp_args : cpp_args,
		dependencies : [ libdpa_core_dep, gtest ],
		pie : false,
		install : false,
		native : true
		)

endif
