# NAME

controller_nvme_namespace_list - List all NVMe namespaces on given controller

# DESCRIPTION

Given NVMe controller, list all attached namespaces, with their
main characteristics.

# Request object

{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "controller_nvme_namespace_list",

  "params": {
    "name": "NvmeEmu0"
  }
}

# Response object

{
  "jsonrpc": "2.0",
  "id": 1,
  "result": [
    {
      "nsid": 1,
      "bdev": "Null0",
      "bdev_type": "spdk"
    },
    {
      "nsid": 2,
      "bdev": "Malloc0",
      "bdev_type": "spdk"
    }
  ]
}


# AUTHOR

Nitzan Carmi <nitzanc@mellanox.com>
