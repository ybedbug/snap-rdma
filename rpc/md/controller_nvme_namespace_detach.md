# NAME

controller_nvme_namespace_detach - remove namespace from NVMe controller

# DESCRIPTION

Remove Namespace with given nsid from NVMe controller. Controller can be
uniquely identified by controller name, or alternatively by its
subsystem NQN and controller ID.

# Request object

{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "controller_nvme_namespace_detach",

  "params": {
    "ctrl": "NvmeEmu0",
    "nsid": 1
  }
}

 # Or Alternatively:

{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "controller_nvme_namespace_detach",

  "params": {
    "subnqn": "nqn.2014.08.org.nvmexpress.snap:cnode1",
    "cntlid": 1,
    "nsid": 1
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
