# NAME

emulation_device_attach_virtio_blk - Plug new VirtIO BLK emulated device to host.

# DESCRIPTION

Attach (plug) new VirtIO BLK emulated device.
For acquiring block device attributes, block device must be provided,
while queue-level attributes are kept configurable.
For pci attributes flexibility, subsystem_id and subsystem_vendor_id
are configurable. All other PCI attributes fields are pre-determined
by Virtio Specification (v1.1).

The function returns the new PF entry, as will be shown from now on
in emulation_list() RPC command.

# Request object

{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "emulation_device_attach_virtio_blk",

  "params": {
    "emulation_manager": "mlx5_0",
    "bdev_type": "spdk",
    "bdev": "Null0",
    "ssid": 0,
    "ssvid": 0,
    "num_queues": 8,
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
      "pci_id": "0"
    }
}


# AUTHOR

Nitzan Carmi <nitzanc@mellanox.com>
