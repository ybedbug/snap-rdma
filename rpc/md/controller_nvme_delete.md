# NAME

controller_nvme_delete - Delete SNAP-based NVMe controller

# DESCRIPTION

Delete (previously created) NVMe controller.
Controller can be uniquely identified by controller name,
or alternatively by its subsystem NQN and controller ID.

# Request object

{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "controller_nvme_delete",

  "params": {
    "name": "NvmeEmu0"
  }
}

 # Or Alternatively:
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "controller_nvme_delete",

  "params": {
    "subnqn": "nqn.2014.08.org.nvmexpress.snap:cnode1",
    "cntlid": 1
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
