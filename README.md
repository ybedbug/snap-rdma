# snap-rdma Library

This is the userspace library for developing SNAP (Software-defined Network Accelerated Processing)
based applications on Linux RDMA subsystem.
Specifically this contains the userspace library for the following device emulation applications:
1. NVMe SNAP
2. VirtIO-blk SNAP
3. VirtIO-net SNAP

This library will assist in common tasks when interacting with the emulating hardware in order to
present a SNAP device (emulated PCI device) to an external host or a VM (e.g. by using SoC Smart NIC).

# Distros

TBD

# Building

To build and install this library, run:
./autogen.sh && ./configure && make && make install

Typically the autogen and configure steps only need be done the first
time unless configure.ac or Makefile.am changes.

Libraries are installed by default at /usr/local/lib.


# Tests

The library has googletest based unit test suite. To build library with the
tests and googletest installed in the /usr/local/gtest do:

    ./configure --with-gtest=/usr/local/gtest
    make

To run tests do

    make test

or run

    tests/gtest_snap_rdma

To see advanced gtest option like test filtering, etc run

    tests/gtest_snap_rdma --help

To build and install gtest in the /usr/local/gtest do:

    git clone https://github.com/google/googletest
    cd googletest
    cmake -DBUILD_GMOCK=0 -DCMAKE_INSTALL_PREFIX=/usr/local/gtest
    make
    make install

# Documentation

The library uses kernel-doc for the in place documentation.
See https://www.kernel.org/doc/html/latest/doc-guide/kernel-doc.html for details.

To build a stand alone documentation linuxdoc toolchain can be used. For more
details see https://return42.github.io/linuxdoc/kernel-doc-intro.html

Linuxdoc tools produce a reST text output which in turn can be converted to the
variety of formrats (html, pdf, man pages etc...) using Sphinx.
See http://www.sphinx-doc.org

Example of extracting kernel-doc comments and creating html output:

    kernel-autodoc --force src /tmp/snap-docs/
    kernel-autodoc --force ctrl /tmp/snap-docs/
    sphinx-build -b html -C  -Dmaster_doc=index -Dhtml_theme=classic /tmp/snap-docs/ /var/www/snap-docs-html/

Linuxdoc also has a syntax checker which can be used to check documentation for
errors:

    kernel-lintdoc ctrl
    kernel-lintdoc src
