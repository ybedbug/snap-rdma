# NAME

emulation_managers_list - List all SNAP emulation managers with their characteristics

# DESCRIPTION

List all emulation managers existing in the system.
Every emulation manager may have different capabilities, such as
controlling hotplug devices, or specific protocols.

# Request object

{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "emulation_managers_list"
}

# Response object

{
  "jsonrpc": "2.0",
  "id": 1,
  "result": [
    {
      "emulation_manager": "mlx5_0",
      "hotplug_support": True,
      "supported_types": ["nvme", "virtio_blk"]
    },
    {
      "emulation_manager": "mlx5_1",
      "hotplug_support": True,
      "supported_types": ["virtio_net"]
    }
  ]
}


# AUTHOR

Nitzan Carmi <nitzanc@mellanox.com>
