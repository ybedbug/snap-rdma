# NAME

storage_admin - execute a storage administration command in ARM side using
                provided arguments and return the results

# DESCRIPTION

Execute a dedicated storage administration script under a pre-defined directory on BuleFiled OS.
Will return the output from the result (errno) and output (stdout/stderr) of the script.

# Request object

{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "storage_admin",
  "params": {
    "op": "connect",
    "protocol": "nvme",
    "policy": "default",
    "qn": "sub0",
    "hostqn": "nqn.bf1",
    "paths": ["transport=rdma/adrfam=ipv4/traddr=1.1.1.1/trsvcid=4420","transport=rdma/adrfam=ipv4/traddr=2.2.2.2/trsvcid=4421"]
  }
}

# Response object

{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "status": 0,
    "output": "type=kernel:dev=/dev/nvme0n1:block_size=512,type=kernel:dev=..."
  }
}

# AUTHOR

Tom Wu <tomwu@nvidia.com>
