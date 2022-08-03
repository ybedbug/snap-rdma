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

if not meson.is_cross_build()
    message('DPA applications will not be compiled')
    subdir_done()
endif

if not flexio.found()
	error('FLEXIO is required for DPA compilation')
	subdirt_done()
endif

message('DPA applications will be compiled')

dpa_cflags = [
	'-mno-relax', '-mcmodel=medany', '-march=rv64imac', '-mabi=lp64',
	'-fno-builtin-fprintf', '-fno-builtin-printf',
	'-DE_MODE_LE', '-DUSER_SPACE', '-D__DPA=1'
]

dpa_cflags += common_cflags

dpa_link_script = meson.current_source_dir() + '/flexio_linker.ld'

#
# Consider adding flexio_dev and :flexio_os.a as
# external dependancies
#
dpa_ldflags = [
	'-static', '-nostartfiles',
	'-T', dpa_link_script,
	'-L' + flexio_pfx + '/lib',
	'-lflexio_dev', '-lflexio_os'
]

libdpa_include = include_directories('.', '..', '../src', flexio_include)
libdpa_sources = [
	'dpa_common.c',
	'dpa_start.c',
	'../src/snap_dma.c',
	'../src/snap_dma_dv.c',
	'../src/snap_dpa_p2p.c'
]

libdpa = static_library('dpa',
			libdpa_sources,
			c_args : dpa_cflags,
			dependencies : [],
			include_directories : libdpa_include,
			install: false,
			native: false)

libdpa_dep = declare_dependency(
			dependencies : [],
			include_directories : libdpa_include,
			sources : libdpa_sources)

# test binaries
dpa_tests = [
	'dpa_hello',
	'dpa_hello_event',
	'dpa_rt_test_polling',
	'dpa_rt_test_event',
	'dpa_cmd_lat_bench',
	'dpa_dma_test'
]

foreach t : dpa_tests
	executable(t, t + '.c',
		c_args : dpa_cflags,
		link_args : dpa_ldflags,
		link_depends : dpa_link_script,
		dependencies : [ libdpa_dep ],
		install : false,
		native : false
		)
endforeach

#
# installable (todo) apps
dpa_apps = [
	'dpa_virtq_split'
]

#
# TODO: setup install path
foreach app : dpa_apps
	executable(app, app + '.c',
		c_args : dpa_cflags,
		link_args : dpa_ldflags,
		link_depends : dpa_link_script,
		dependencies : [ libdpa_dep ],
		install : true,
		native : false
		)
endforeach
