# NAME

subsystem_nvme_list - List all active SNAP-Based NVMe subsystems

# DESCRIPTION

Return information for NVM subsystems created for SNAP. Information will
include characteristics such as nqn, serial_number, model_number and list
of controllers that are associated with it.

# Request object

{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "subsystem_nvme_list"
}

# Response object

{
  "jsonrpc": "2.0",
  "id": 1,
  "result": [
    {
      "nqn": "nqn.2014.08.org.nvmexpress.snap:cnode1",
      "serial_number": "MNC12",
      "model_number": "NVIDIA NVMe SNAP Controller",
      "controllers": [
        {
          "name": "NvmeEmu0",
          "cntlid": 0,
          "pci_bdf": "83:00.1",
          "pci_index": 0
        },
        {
          "name": "NvmeEmu1",
          "pci_bdf": "83:00.2",
          "cntlid": 1,
          "pci_index": 1
        }
      ]
    },
    {
      "nqn": "nqn.2014.08.org.nvmexpress.snap:cnode2",
      "serial_number": "MNC14",
      "model_number": "Mellanox NVMe SNAP Controller",
      "controllers": [
        {
          "name": "NvmeEmu4",
          "cntlid": 0,
          "pci_bdf": "88:00.1",
          "pci_index": 0
        },
        {
          "name": "NvmeEmu5",
          "cntlid": 1,
          "pci_bdf": "88:00.2"
          "pci_index": 1
        }
      ]
    }
  ]
}


# AUTHOR

Max Gurtovoy <maxg@mellanox.com>
