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

#Meson docs:
#
#For setting optimization levels and toggling debug, you can either set
#the buildtype option, or you can set the optimization and debug options
#which give finer control over the same.
#
project('libsnap_core', 'C', 'CPP',
	# Get version number from file.
	# Fallback to "more" for Windows compatibility.
	version: run_command(find_program('cat', 'more'),
		files('VERSION'), check: false).stdout().strip(),
	default_options: [
			  'werror=true',
			  'b_pie=true',
			  'b_staticpic=true',
			  'prefix=/opt/mellanox/devemu',
			  # always add debug symbols (-g)
			  'debug=true',
			  'optimization=3'
			  ],
	meson_version: '>= 0.55.0'
)

cc = meson.get_compiler('c')
cpp = meson.get_compiler('cpp')

libsnap_core_depends = []
libsnap_core_depends += dependency('libibverbs', required : true)
libsnap_core_depends += dependency('libmlx5', required : true)

conf_data = configuration_data()

#todo: in the future consider subproject/system install
flexio_include = []
flexio_pfx = get_option('with-flexio')
if (flexio_pfx != '')
	message('Checking flexio in ' + flexio_pfx)
	flexio_include = [flexio_pfx + '/include', flexio_pfx + '/include/libflexio']
	flexio = cc.find_library('flexio',
			dirs: flexio_pfx + '/lib',
			has_headers: 'libflexio/flexio.h',
			header_include_directories: include_directories(flexio_include),
			required : true)
	libsnap_core_depends += flexio
	conf_data.set10('HAVE_FLEXIO', 1, description : 'Enable DPA host side support')
endif

conf_data.set10('HAVE_DECL_IBV_QUERY_GID_EX',
		cc.has_function('ibv_query_gid_ex', prefix : '#include <infiniband/verbs.h>', dependencies: libsnap_core_depends),
		description: 'Set HAVE_DECL_IBV_QUERY_GID_EX if ibv_query_gid_ex() is supported')

configure_file(output : 'config.h', configuration : conf_data)

subdir('src')
subdir('tests')
