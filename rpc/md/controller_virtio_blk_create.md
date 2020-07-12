# NAME

controller_virtio_blk_create - Create new SNAP-based VirtIO BLK controller

# DESCRIPTION

Create new VirtIO BLK controller over specific PCI function on host.

The response contains a uniquely identified controller name.

# Request object

{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "controller_virtio_blk_create",

  "params": {
    "emulation_manager": "mlx5_0",
    "pci_func": 1,
    "bdev_type": "spdk",
    "bdev": "Null0"
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
