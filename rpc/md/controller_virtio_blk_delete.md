# NAME

controller_virtio_blk_delete - Delete SNAP-based VirtIO BLK controller

# DESCRIPTION

Delete (previously created) VirtIO BLK controller.
Controller can be uniquely identified by controller name
as acquired from controller_virtio_blk_create().

# Request object

{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "controller_virtio_blk_delete"

  "params": {
    "name": "VblkEmu0",
    "force": True
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
