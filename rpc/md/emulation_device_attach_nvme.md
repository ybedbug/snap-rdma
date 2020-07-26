# NAME

emulation_device_attach_nvme - Plug new emulated NVMe device to host.

# DESCRIPTION

Attach (plug) a new emulated NVMe device with the given parameters.
Currently the following parameters are configurable:  device_id,
vendor_id, subsystem_id and subsystem_vendor_id. All other PCI
attributes fields are pre-determined by NVMe specification (v1.1).

The function returns the new PF entry, as will be shown from now on
in emulation_list() RPC command.

# Request object

{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "emulation_device_attach_nvme",

  "params": {
    "emulation_manager": "mlx5_0",
    "did": 0x6001, //Mellanox device id
    "vid": 0x15b3, //Mellanox vendor id
    "ssid": 0, // Indicates the sub-system identifier
    "ssvid": 0 // Indicates the sub-system vendor identifier
  }
}

# Response object

{
  "jsonrpc": "2.0",
  "id": 1,
  "result":
    {
      "emulation_manager": "mlx5_0",
      "emulation_type": "nvme",
      "pci_type": "physical function",
      "pci_bdf": "83:00.2"
    }
}


# AUTHOR

Max Gurtovoy <maxg@mellanox.com>
