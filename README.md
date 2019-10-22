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

TBD
