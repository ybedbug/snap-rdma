# NAME

controller_virtio_blk_bdev_list - List bdev attached to VirtIO BLK controller

# DESCRIPTION

List attached bdev for VirtIO BLK controller

# Request object

{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "controller_virtio_blk_bdev_list",

  "params": {
    "ctrl": "VblkEmu0pf0"
  }
}

# Response object

{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "name": "VblkEmu0pf0",
    "bdev": {
      "bdev": "Malloc0",
      "bdev_type": "spdk",
      "block_size": 512,
      "num_blocks": 131072
    }
  }
}


# AUTHOR

Tom Wu <tomwu@nvidia.com>
