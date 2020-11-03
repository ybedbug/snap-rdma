# NAME

controller_virtio_blk_create - Create new SNAP-based VirtIO BLK controller

# DESCRIPTION

Create new VirtIO BLK controller over specific PCI function on host.

To specify the PCI function to open controller upon, either "pci_bdf"
("84:00.2") or "pci_index" (0) must be provided, but not both.
The mapping for pci_bdf and pci_index can be queried by running
emulation_functions_list method.

The response contains a uniquely identified controller name.

# Request object

{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "controller_virtio_blk_create",

  "params": {
    "emulation_manager": "mlx5_0",
    "pci_bdf": "83:00.2",
    "num_queues": 8,
    "queue_depth": 32,
    "size_max": 4096,
    "seg_max": 4,
    "bdev_type": "spdk",
    "bdev": "Null0",
    "serial": "ABCDEFG"
  }
}

# Response object

{
  "jsonrpc": "2.0",
  "id": 1,
  "result": "VblkEmu0"
}


# AUTHOR

Nitzan Carmi <nitzanc@mellanox.com>
Max Gurtovoy <maxg@mellanox.com>
