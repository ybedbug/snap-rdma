# NAME

controller_nvme_namespace_attach - attach block device to to given NVMe controller
                                   as new namespace.

# DESCRIPTION

Attach (pre-created) block device to given NVMe controller, as a namespace with
the desired nsid. Controller can be uniquely identified by controller name,
or alternatively by its subsystem NQN and controller ID.

# Request object

{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "controller_nvme_namespace_attach",

  "params": {
    "name": "NvmeEmu0",
    "bdev": "Null0",
    "bdev_type": "spdk",
    "nsid": 1,
    "qn": "nqn.2014.08.org.nvmexpress.spdk:tgt",
    "protocol": "nvme"
  }
}

 # Or Alternatively:

{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "controller_nvme_namespace_attach",

  "params": {
    "subnqn": "nqn.2014.08.org.nvmexpress.snap:cnode1",
    "cntlid": 1,
    "bdev": "Null0",
    "bdev_type": "spdk",
    "nsid": 1,
    "qn": "nqn.2014.08.org.nvmexpress.spdk:tgt",
    "protocol": "nvme"
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
