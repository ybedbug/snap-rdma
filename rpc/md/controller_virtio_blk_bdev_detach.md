# NAME

controller_virtio_blk_bdev_detach - remove bdev from VirtIO BLK controller

# DESCRIPTION

Remove previously attached bdev from VirtIO BLK controller

# Request object

{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "controller_virtio_blk_bdev_detach",

  "params": {
    "ctrl": "VblkEmu0pf0"
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
