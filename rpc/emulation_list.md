# NAME

emulation_list - List all SNAP plugged emulation functions with their characteristics

# DESCRIPTION

Device emulation hardware can support multiple emulation types. For example,
Bluefield-1 adapter support NVMe device emulation and Bluefield-2 support NVMe,
VirtIO block and VirtIO net full device emulation. This method will supply a
list of the online (plugged) emulation functions with their characteristics.

# Request object

{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "emulation_list"
}

# Response object

{
  "jsonrpc": "2.0",
  "id": 1,
  "result": [
    {
      "emulation_manager": "mlx5_0",
      "emulation_type": "nvme",
      "pci_type": "physical function",
      "pci_id": "0"
    },
    {
      "emulation_manager": "mlx5_0",
      "emulation_type": "nvme",
      "pci_type": "physical function",
      "pci_id": "1"
    },
    {
      "emulation_manager": "mlx5_0",
      "emulation_type": "virtio_blk",
      "pci_type": "physical function",
      "pci_id": "2"
    },
    {
      "emulation_manager": "mlx5_1",
      "emulation_type": "virtio_net",
      "pci_type": "physical function",
      "pci_id": "0"
    }
  ]
}


# AUTHOR

Max Gurtovoy <maxg@mellanox.com>
Nitzan Carmi <nitzanc@mellanox.com>
