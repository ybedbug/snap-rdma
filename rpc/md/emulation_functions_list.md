# NAME

emulation_functions_list - List all SNAP plugged emulation functions with their characteristics

# DESCRIPTION

Device emulation hardware can support multiple emulation types. For example,
Bluefield-1 adapter support NVMe device emulation and Bluefield-2 support NVMe,
VirtIO block and VirtIO net full device emulation. This method will supply a
list of the online (plugged) emulation functions with their characteristics.

# Request object

{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "emulation_functions_list"
}

# Response object

{
  "jsonrpc": "2.0",
  "id": 1,
  "result": [
    {
      "emulation_manager": "mlx5_0",
      "emulation_type": "nvme",
      "pci_index": 0,
      "pci_bdf": "83:00.2",
      "total_vf": 2,
      "virtual_functions": [
        {
          "vf_index": 0,
          "vf_bdf": "83:00.3"
        },
        {
          "vf_index": 1,
          "vf_bdf": "83:00.4"
        },
      ]
    },
    {
      "emulation_manager": "mlx5_0",
      "emulation_type": "nvme",
      "pci_index": 1,
      "total_vf": 0,
      "pci_bdf": "82:00.2"
    },
    {
      "emulation_manager": "mlx5_0",
      "emulation_type": "virtio_blk",
      "pci_index": 0,
      "total_vf": 0,
      "pci_bdf": "84:00.2"
    },
    {
      "emulation_manager": "mlx5_1",
      "emulation_type": "virtio_net",
      "pci_index": 0,
      "total_vf": 0,
      "pci_bdf": "85:00.4"
    }
  ]
}


# AUTHOR

Max Gurtovoy <maxg@mellanox.com>
Nitzan Carmi <nitzanc@mellanox.com>
