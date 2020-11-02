# NAME

dpu_exec - execute a program with provided arguments and return the results

# DESCRIPTION

Execute a program which stored under a pre-defined directory on BuleFiled OS.
The arguments, if exist to the script, need to construct to a comma-seperated list.
Will return the output from stdout of this program if its execute status is 0,
otherwise, will return the output from stderr.

# Request object

{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "dpu_exec"

  "params": {
    "file": "configure_multipath.py",
    "args": ["--op=connect", "--policy=round-robin", "--protocol=nvme", "--qn=sub0", "--hostqn=nqn.2020-10.snic.rsws05:1", "--transport=rdma", "--paths=adrfam:ipv4/traddr:1.1.1.1/trsvcid:4420,adrfam:ipv4/traddr:1.1.1.2/trsvcid:4421"],
   }
}

# Response object

{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
        "status:": 0,
        "stdout:": "/dev/nvme0n1",
        "stderr:": "",
    }
}

# AUTHOR

Tom Wu <tomwu@nvidia.com>
