# NAME

controller_virtio_blk_get_debugstat - query SNAP-based VirtIO BLK controller

# DESCRIPTION

Get debug statistics from VirtIO BLK controller.

# Request object

{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "controller_virtio_blk_get_debugstat",

  "params": {
    "name": "VblkEmu0pf0"
  }
}

# Response object

{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "controllers" : [
      {
        "name": "VblkEmu0pf0",
        "global": {
          "network_error": 10,
          "bad_descriptor_error": 1,
          "invalid_buffer": 0,
          "desc_list_exceed_limit": 2,
          "internal_error": 20
        },
        "queues": [
          {
            "qid": 1,
            "hw_available_index": 9760,
            "sw_available_index": 9762,
            "hw_used_index": 9755,
            "sw_used_index": 9750,
            "hw_received_descs": 100760,
            "hw_completed_descs": 100755
          },
          {
            "qid": 2,
            "hw_available_index": 1061,
            "sw_available_index": 1065,
            "hw_used_index": 1055,
            "sw_used_index": 1052,
            "hw_received_descs": 101761,
            "hw_completed_descs": 101755
          }
        ]
      }
    ]
  }
}


# AUTHOR

Israel Rukshin <israelr@nvidia.com>
