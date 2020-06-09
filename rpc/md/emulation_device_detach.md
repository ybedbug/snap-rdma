# NAME

emulation_device_detach - Detach (unplug) emulated device from host

# DESCRIPTION

Detach plugged emulated device from host.
Unlike attach, there is no importance for which protocol is used by
the device.

# Request object

{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "emulation_device_detach",

  "params": {
    "emulation_manager": "mlx5_0",
    "pci_id": 1
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
