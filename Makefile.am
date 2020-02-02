
ACLOCAL_AMFLAGS=-I m4

SUBDIRS = src ctrl tests

EXTRA_DIST = README.md autogen.sh

if HAVE_GTEST
#
# Note: test target has a special meaning for the CI pipeline
# If present it must produce XML in the expected ("gtest") format
# or build will fail
#
GTEST_REPORT_DIR ?= .
test:
	tests/gtest_snap_rdma --gtest_output="xml:$(GTEST_REPORT_DIR)/testAll.xml"
endif
