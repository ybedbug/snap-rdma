# NAME

subsystem_nvme_create - Create a new NVM subsystem for SNAP

# DESCRIPTION

Create new NVM subsystem that will be controlled by one or more NVMe SNAP
controllers. An NVM subsystem includes one or more controllers, zero or
more namespaces, and one or more ports. An NVM subsystem may include a
non-volatile memory storage medium and an interface between the
controller(s) in the NVM subsystem and non-volatile memory storage
medium.

The result in the response object return "true" for success and "false"
in case of failure.

# Request object

{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "subsystem_nvme_create",

  "params": {
    "nqn": "nqn.2014.08.org.nvmexpress.snap:cnode1",
    "serial_number": "MNC12",
    "model_number": "NVIDIA NVMe SNAP Controller",
    "nn": 0xFFFFFFFE,
    "mnan": 1024
  }
}

# Response object

{
  "jsonrpc": "2.0",
  "id": 1,
  "result": "true"
}


# AUTHOR

Max Gurtovoy <maxg@mellanox.com>
