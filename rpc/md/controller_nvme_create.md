# NAME

controller_nvme_create - Create new SNAP-based NVMe controller

# DESCRIPTION

Create new NVMe controller over specific PCI function on host for a
specific NVM subsystem.

To specify the PCI function to open controller upon, either "pci_bdf"
("84:00.2") or "pci_index" (0) must be provided, but not both.
The mapping for pci_bdf and pci_index can be queried by running
emulation_functions_list method.

The response contains a uniquely identified controller name, and its
unique controller ID.

# Request object

{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "controller_nvme_create",

  "params": {
    "nqn": "nqn.2014.08.org.nvmexpress.snap:cnode1",
    "emulation_manager": "mlx5_2",
    "pci_bdf": "84:00.2",
    "conf_file": "/etc/nvme_snap/nvme0.json",
    "nr_io_queues": 32,
    "mdts": 4,
    "max_namespaces": 0,
    "quirks": 0
  }
}

# Response object

{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "name": "NvmeEmu0",
    "cntlid": 1
  }
}


# AUTHOR

Nitzan Carmi <nitzanc@mellanox.com>
Max Gurtovoy <maxg@mellanox.com>
