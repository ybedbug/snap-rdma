
ACLOCAL_AMFLAGS=-I m4

SUBDIRS = src ctrl tests

EXTRA_DIST = README.md autogen.sh

# do not install tests
install:
	$(MAKE) -C src install
	$(MAKE) -C ctrl install
