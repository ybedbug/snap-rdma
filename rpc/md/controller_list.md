# NAME

controller_list - List all active SNAP-Based controllers with their
                  characteristics

# DESCRIPTION

User can create/delete controllers running over emulated functions,
using controller_'<'protocol'>'_create/delete API.
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
      "subnqn": "nqn.2014.08.org.nvmexpress.snap:cnode1",
      "cntlid": 1,
      "name": "NvmeEmu0",
      "emulation_manager": "mlx5_2",
      "type": "nvme",
      "pci_index": 0,
      "pci_bdf": "83:00.1"
    },
    {
      "name": "VblkEmu0",
      "emulation_manager": "mlx5_0",
      "type": "virtio_blk",
      "pci_index": 0,
      "pci_bdf": "84:00.1"
    }
  ]
}


# AUTHOR

Nitzan Carmi <nitzanc@mellanox.com>
Max Gurtovoy <maxg@mellanox.com>
