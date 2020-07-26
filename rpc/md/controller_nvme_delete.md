# NAME

controller_nvme_delete - Delete SNAP-based NVMe controller

# DESCRIPTION

Delete (previously created) NVMe controller.
Controller can be uniquely identified by controller name
as acquired from controller_nvme_create().

# Request object

{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "controller_nvme_delete",

  "params": {
    "name": "NvmeEmu0"
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
Max Gurtovoy <maxg@mellanox.com>
