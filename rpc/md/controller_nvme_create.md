# NAME

controller_nvme_create - Create new SNAP-based NVMe controller

# DESCRIPTION

Create new NVMe controller over specific PCI function on host.

The response contains a uniquely identified controller name.

# Request object

{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "controller_nvme_create",

  "params": {
    "pci_func": 1,
    "conf_file": "/etc/nvme_snap/nvme0.json"
  }
}

# Response object

{
  "jsonrpc": "2.0",
  "id": 1,
  "result": "NvmeEmu0"
}


# AUTHOR

Nitzan Carmi <nitzanc@mellanox.com>
