# NAME

controller_nvme_namespace_list - List all NVMe namespaces on given controller

# DESCRIPTION

Given NVMe controller, list all attached namespaces, with their
main characteristics. Controller can be uniquely identified by
controller name, or alternatively by its subsystem NQN and controller ID.

# Request object

{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "controller_nvme_namespace_list",

  "params": {
    "name": "NvmeEmu0"
  }
}

 # Or Alternatively:

{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "controller_nvme_namespace_list",

  "params": {
    "subnqn": "nqn.2014.08.org.nvmexpress.snap:cnode1",
    "cntlid": 1
  }
}

# Response object

{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "name": "NvmeEmu2pf0",
    "cntlid": 0,
    "Namespaces": [
      {
        "nsid": 1,
        "bdev": "Null0",
        "bdev_type": "spdk",
        "qn": "nqn.2014.08.org.nvmexpress.spdk:tgt",
        "protocol": "nvme"
      },
      {
        "nsid": 2,
        "bdev": "Malloc0",
        "bdev_type": "spdk",
        "qn": "nqn.2014.08.org.nvmexpress.spdk:tgt",
        "protocol": "nvme"
      }
    ]
  }
}


# AUTHOR

Nitzan Carmi <nitzanc@mellanox.com>
