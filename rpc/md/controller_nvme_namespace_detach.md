# NAME

controller_nvme_namespace_detach - remove namespace from NVMe controller

# DESCRIPTION

Remove Namespace with given nsid from NVMe controller

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

# Response object

{
  "jsonrpc": "2.0",
  "id": 1,
  "result": True
}


# AUTHOR

Nitzan Carmi <nitzanc@mellanox.com>
