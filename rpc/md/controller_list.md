# NAME

controller_list - List all active SNAP-Based controllers with their
                  characteristics

# DESCRIPTION

User can create/delete controllers running over emulated functions,
using controller_<protocol>_create/delete API.
This method will supply a list of all active (created) controllers
with their characteristics.

# Request object

{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "controller_list"
}

# Response object

{
  "jsonrpc": "2.0",
  "id": 1,
  "result": [
    {
      "name": "NvmeEmu0",
      "type": "nvme",
      "pci_id": "0"
    },
    {
      "name": "VblkEmu0",
      "type": "virtio_blk",
      "pci_id": "1"
    }
  ]
}


# AUTHOR

Nitzan Carmi <nitzanc@mellanox.com>
