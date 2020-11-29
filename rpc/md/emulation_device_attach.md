# NAME

emulation_device_attach - Plug new emulated device as PF on host.

# DESCRIPTION

Attach (plug) new emulated device as host's new PF, as many PCI
configuration space parameters may be defined.
The function returns the new PF entry, as will be shown from now on
in emulation_device_list() RPC command.

# Request object

{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "emulation_device_attach",

  "params": {
    "emulation_manager": "mlx5_0",
    "id": 0x6002,
    "vid": 0x15b3,
    "ssid": 0,
    "ssvid": 0,
    "num_queues": 8,
    "num_msix": 0x8,
    "total_vf": 0x10,
    "queue_depth": 1024
  }
}

# Response object

{
  "jsonrpc": "2.0",
  "id": 1,
  "result":
    {
      "emulation_manager": "mlx5_0",
      "emulation_type": "virtio_blk",
      "pci_type": "physical function",
      "pci_index": 0,
      "pci_bdf": "83:00.2"
    }
}


# AUTHOR

Nitzan Carmi <nitzanc@mellanox.com>
Max Gurtovoy <maxg@mellanox.com>
