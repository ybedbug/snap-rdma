# NAME

emulation_device_detach - Detach (unplug) emulated device from host

# DESCRIPTION

Detach plugged emulated device from host.
Unlike attach, there is no importance for which protocol is used by
the device.

To specify the PCI function to open controller upon, either "pci_bdf"
("84:00.2") or "pci_index" (0) must be provided, but not both.
The mapping for pci_bdf and pci_index can be queried by running
emulation_functions_list method.

# Request object

{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "emulation_device_detach",

  "params": {
    "emulation_manager": "mlx5_0",
    "pci_bdf": "83:00.2"
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
