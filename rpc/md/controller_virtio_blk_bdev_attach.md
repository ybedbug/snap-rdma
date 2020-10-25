# NAME

controller_virtio_blk_bdev_attach - attach block device to given
                                    VirtIO BLK controller

# DESCRIPTION

Attach (pre-created) block device to given VirtIO BLK controller, so
it will act as its backend

# Request object

{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "controller_virtio_blk_bdev_attach",

  "params": {
    "name": "VblkEmu0pf0",
    "bdev": "Null0",
    "bdev_type": "spdk"
  }
}

# Response object

{
  "jsonrpc": "2.0",
  "id": 1,
  "result": True
}


# AUTHOR

Nitzan Carmi <nitzanc@mellanox.com>
