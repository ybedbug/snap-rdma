# NAME

subsystem_nvme_delete - Delete an existing SNAP NVM subsystem

# DESCRIPTION

Delete previously created SNAP NVM subsystem. User should pass subsystem
nqn to identify the subsystem that is going to be destroyed.

The result in the response object return "true" for success and "false"
in case of failure.

# Request object

{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "subsystem_nvme_delete",

  "params": {
    "nqn": "nqn.2014.08.org.nvmexpress.snap:cnode1"
  }
}

# Response object

{
  "jsonrpc": "2.0",
  "id": 1,
  "result": true
}


# AUTHOR

Max Gurtovoy <maxg@mellanox.com>
