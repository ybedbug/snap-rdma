# NAME

controller_nvme_async_event_enable - Enable Async event on nvme controller

# DESCRIPTION

Enable async event type by specify --event-bit on given NVMe controller.
(refer to nvme1.4 specification at "Figure 291: Asynchronous Event
Configuration – Command Dword 11" to check which bit represent what event)
If no async event bit is provide, then it will enable all events.
Controller can be uniquely identified by controller name,
or alternatively by its subsystem NQN and controller ID.

# Request object

{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "controller_nvme_async_event_enable",

  "params": {
    "ctrl": "NvmeEmu0pf0"
    "event_bit": 8
  }
}

 # Or Alternatively:

{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "controller_nvme_async_event_enable",

  "params": {
    "subnqn": "nqn.2014.08.org.nvmexpress.snap:cnode1",
    "cntlid": 1
    "event_bit": 8
  }
}

# Response object

{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "Enable async event ": "Succeed"
  }
}


# AUTHOR

Tom Wu <tomwu@nvidia.com>
