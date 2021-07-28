#!/usr/bin/env python3

import argparse
import sys
import shlex
import json
import socket
import time
import os
import copy

try:
    from shlex import quote
except ImportError:
    from pipes import quote


class JsonRpcSnapException(Exception):
    def __init__(self, message):
        self.message = message


class JsonRpcSnapClient(object):
    decoder = json.JSONDecoder()

    def __init__(self, sockpath, timeout=60.0):
        self.sock = None
        self._request_id = 0
        self.timeout = timeout
        try:
            if os.path.exists(sockpath):
                self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                self.sock.connect(sockpath)
            else:
                raise socket.error("Unix socket '%s' does not exist" % sockpath)
        except socket.error as ex:
            raise JsonRpcSnapException("Error while connecting to %s\n"
                                       "Error details: %s" % (sockpath, ex))

    def __json_to_string(self, request):
        return json.dumps(request)

    def send(self, method, params=None):
        self._request_id += 1
        req = {
            'jsonrpc': '2.0',
            'method': method,
            'id': self._request_id
        }
        if params:
            req['params'] = copy.deepcopy(params)

        self.sock.sendall(self.__json_to_string(req).encode("utf-8"))
        return self._request_id

    def __string_to_json(self, request_str):
        try:
            obj, idx = self.decoder.raw_decode(request_str)
            return obj
        except ValueError:
            return None

    def recv(self):
        timeout = self.timeout
        start_time = time.process_time()
        response = None
        buf = ""

        while not response:
            try:
                timeout = timeout - (time.process_time() - start_time)
                self.sock.settimeout(timeout)
                buf += self.sock.recv(4096).decode("utf-8")
                response = self.__string_to_json(buf)
            except socket.timeout:
                break
            except ValueError:
                continue  # incomplete response; keep buffering

        if not response:
            raise JsonRpcSnapException("Response Timeout")
        return response

    def call(self, method, params={}):
        req_id = self.send(method, params)
        response = self.recv()

        if 'error' in response:
            params["method"] = method
            params["req_id"] = req_id
            msg = "\n".join(["request:", "%s" % json.dumps(params, indent=2),
                             "Got JSON-RPC error response",
                             "response:",
                             json.dumps(response['error'], indent=2)])
            raise JsonRpcSnapException(msg)

        return response['result']


def main():
    parser = argparse.ArgumentParser(
        description='Mellanox SNAP JSON-RPC 2.0 command line interface')
    parser.add_argument('-s', dest='server_addr',
                        help='RPC domain socket path',
                        default='/var/tmp/spdk.sock')
    parser.add_argument('-t', dest='timeout',
                        help='Timeout as a floating point number expressed in '
                             'seconds waiting for response. Default: 60.0',
                        default=60.0, type=float)
    parser.add_argument('-v', dest='verbose',
                        choices=['DEBUG', 'INFO', 'ERROR'],
                        help="""Set verbosity level. """)
    subparsers = parser.add_subparsers(help='Mellanox SNAP JSON-RPC 2.0 Client methods',
                                       dest='called_rpc_name')

    def emulation_device_detach(args):
        if args.pci_bdf is None and args.pci_index == -1:
            raise JsonRpcSnapException("Either pci_bdf or pci_index must "
                                       "be configured")
        if args.pci_bdf is not None and args.pci_index != -1:
            raise JsonRpcSnapException("pci_bdf and pci_index cannot be "
                                       "both configured")
        params = {
            'emulation_manager': args.emu_manager,
            'device_type': args.type,
        }
        if args.pci_bdf:
            params['pci_bdf'] = args.pci_bdf
        if args.pci_index != -1:
            params['pci_index'] = args.pci_index
        args.client.call('emulation_device_detach', params)
    p = subparsers.add_parser('emulation_device_detach',
                              help='Detach (Unplug) SNAP device from host')
    p.add_argument('emu_manager', help='Emulation manager', type=str)
    p.add_argument('type', help='Device type', type=str,
                   choices=['nvme', 'virtio_blk'])
    p.add_argument('-d', '--pci_bdf', help='PCI device to start emulation on. '
                   'Must be set if \'--pci_index\' is not set',
                   type=str, required=False)
    p.add_argument('-i', '--pci_index', help='PCI index to start emulation on. '
                   'Must be set if \'--pci_bdf\' is not set',
                   default=-1, type=int, required=False)
    p.set_defaults(func=emulation_device_detach)

    def emulation_device_attach(args):
        params = {
            'emulation_manager': args.emu_manager,
            'device_type': args.type,
        }
        if args.id:
            params['id'] = args.id
        if args.vid:
            params['vid'] = args.vid
        if args.ssid:
            params['ssid'] = args.ssid
        if args.ssvid:
            params['ssvid'] = args.ssvid
        if args.revid:
            params['revid'] = args.revid
        if args.class_code:
            params['class_code'] = args.class_code
        if args.bdev_type:
            params['bdev_type'] = args.bdev_type
        if args.bdev:
            params['bdev'] = args.bdev
        if args.num_queues:
            params['num_queues'] = args.num_queues
        if args.queue_depth:
            params['queue_depth'] = args.queue_depth
        if args.total_vf:
            params['total_vf'] = args.total_vf
        if args.num_msix:
            params['num_msix'] = args.num_msix
        result = args.client.call('emulation_device_attach', params)
        print(json.dumps(result, indent=2))
    p = subparsers.add_parser('emulation_device_attach',
                              help='Attach (plug) VirtIO BLK SNAP device '
                                   'to host')
    p.add_argument('emu_manager', help='Emulation manager', type=str)
    p.add_argument('type', help='Device type', type=str,
                   choices=['nvme', 'virtio_blk'])
    p.add_argument('--id', help='Device ID', type=int, required=False)
    p.add_argument('--vid', help='Vendor ID', type=int, required=False)
    p.add_argument('--ssid', help='Subsystem Device ID', type=int,
                   required=False)
    p.add_argument('--ssvid', help='Subsystem Vendor ID', type=int,
                   required=False)
    p.add_argument('--revid', help='Revision ID', type=int, required=False)
    p.add_argument('--class_code', help='Class Code', type=int, required=False)
    p.add_argument('--bdev_type', help='Block device type', type=str,
                   choices=["spdk", "none"], required=False)
    p.add_argument('--bdev', help='Block device to use as backend', type=str,
                   required=False)
    p.add_argument('--num_queues', help='Number of queues', type=int)
    p.add_argument('--queue_depth', help='Queue depth', type=int)
    p.add_argument('--total_vf', help='Maximal num of VFs allowed', type=int,
                   required=False)
    p.add_argument('--num_msix', help='MSI-X vector size', type=int,
                   required=False)
    p.set_defaults(func=emulation_device_attach)

    def controller_virtio_blk_lm_enable(args):
        params = {
            'name': args.name,
            'lm_channel_name': args.lm_channel_name,
        }

        args.client.call('controller_virtio_blk_lm_enable', params)
    p = subparsers.add_parser('controller_virtio_blk_lm_enable',
                              help='Enable VirtIO BLK SNAP controller live migration')
    p.add_argument('name', help='Controller Name', type=str)
    p.add_argument('lm_channel_name', help='Live migration channel name', type=str)
    p.set_defaults(func=controller_virtio_blk_lm_enable)

    def controller_virtio_blk_lm_disable(args):
        params = {
            'name': args.name,
        }

        args.client.call('controller_virtio_blk_lm_disable', params)
    p = subparsers.add_parser('controller_virtio_blk_lm_disable',
                              help='Disable VirtIO BLK SNAP controller live migration')
    p.add_argument('name', help='Controller Name', type=str)
    p.set_defaults(func=controller_virtio_blk_lm_disable)

    def controller_virtio_blk_suspend(args):
        params = {
            'name': args.name,
        }

        args.client.call('controller_virtio_blk_suspend', params)
    p = subparsers.add_parser('controller_virtio_blk_suspend',
                              help='Suspend VirtIO BLK SNAP controller')
    p.add_argument('name', help='Controller Name', type=str)
    p.set_defaults(func=controller_virtio_blk_suspend)

    def controller_virtio_blk_resume(args):
        params = {
            'name': args.name,
        }

        args.client.call('controller_virtio_blk_resume', params)
    p = subparsers.add_parser('controller_virtio_blk_resume',
                              help='Resume VirtIO BLK SNAP controller')
    p.add_argument('name', help='Controller Name', type=str)
    p.set_defaults(func=controller_virtio_blk_resume)

    def controller_virtio_blk_state(args):
        params = {
            'name': args.name,
        }

        if args.save and args.restore:
            raise JsonRpcSnapException("save and restore cannnot be both configured")
        if args.save:
            params['save'] = args.save
        if args.restore:
            params['restore'] = args.restore

        args.client.call('controller_virtio_blk_state', params)
    p = subparsers.add_parser('controller_virtio_blk_state', help='Save or restore controller state')
    p.add_argument('name', help='Controller Name', type=str)
    p.add_argument('--save', help='save state to the given file', type=str, required=False)
    p.add_argument('--restore', help='retore state from the given file', type=str, required=False)
    p.set_defaults(func=controller_virtio_blk_state)

    def controller_virtio_blk_delete(args):
        params = {
            'name': args.name,
        }
        if args.f:
            params['force'] = args.f

        args.client.call('controller_virtio_blk_delete', params)
    p = subparsers.add_parser('controller_virtio_blk_delete',
                              help='Destroy VirtIO BLK SNAP controller')
    p.add_argument('name', help='Controller Name', type=str)
    p.add_argument('--f', '--force', help='Force controller deletion',
                   required=False, action='store_true')
    p.set_defaults(func=controller_virtio_blk_delete)

    def controller_virtio_blk_create(args):
        if args.pci_bdf is None and args.pf_id == -1:
            raise JsonRpcSnapException("Either pci_bdf or pf_id must "
                                       "be configured")
        if args.pci_bdf is not None and args.pf_id != -1:
            raise JsonRpcSnapException("pci_bdf and pf_id cannot be "
                                       "both configured")
        if args.suspend and args.recover:
            raise JsonRpcSnapException("suspend and recover cannot be "
                                       "both configured")
        params = {
            'emulation_manager': args.emu_manager,
            'bdev_type': args.bdev_type,
        }
        if args.pci_bdf:
            params['pci_bdf'] = args.pci_bdf
        if args.pf_id != -1:
            params['pf_id'] = args.pf_id
        if args.pf_id != -1 and args.vf_id != -1:
            params['vf_id'] = args.vf_id
        if args.num_queues:
            params['num_queues'] = args.num_queues
        if args.queue_depth:
            params['queue_depth'] = args.queue_depth
        if args.size_max:
            params['size_max'] = args.size_max
        if args.seg_max:
            params['seg_max'] = args.seg_max
        if args.bdev:
            params['bdev'] = args.bdev
        if args.serial:
            params['serial'] = args.serial
        if args.force_in_order:
            params['force_in_order'] = args.force_in_order
        if args.suspend:
            params['suspend'] = args.suspend
        if args.recover:
            params['recover'] = args.recover
        if args.mem:
            params['mem'] = args.mem            
        result = args.client.call('controller_virtio_blk_create', params)
        print(json.dumps(result, indent=2).strip('"'))
    p = subparsers.add_parser('controller_virtio_blk_create',
                              help='Create new VirtIO BLK SNAP controller')
    p.add_argument('emu_manager', help='Emulation manager', type=str)
    p.add_argument('-d', '--pci_bdf', help='PCI device to start emulation on. '
                   'Must be set if \'--pf_id\' is not set',
                   type=str, required=False)
    p.add_argument('--pf_id', help='PCI PF index to start emulation on. '
                   'Must be set if \'--pci_bdf\' is not set',
                   default=-1, type=int, required=False)
    p.add_argument('--vf_id', help='PCI VF index to start emulation on. '
                   '\'--pf_id\' must also be set to take effect',
                   default=-1, type=int, required=False)
    p.add_argument('--num_queues', help='Number of queues', type=int)
    p.add_argument('--queue_depth', help='Queue depth', type=int)
    p.add_argument('--size_max', help='size_max PCI register value', type=int)
    p.add_argument('--seg_max', help='seg_max PCI register value', type=int)
    p.add_argument('--bdev_type', help='Block device type', type=str,
                   choices=["spdk", "none"], required=True)
    p.add_argument('--bdev', help='Block device to use as backend', type=str,
                   required=False)
    p.add_argument('--serial', help='Serial number for the controller',
                   type=str, required=False)
    p.add_argument('--force_in_order', help='Force handle I/O completions in-order ',
                   required=False, action='store_true')
    p.add_argument('--suspend', help='Created controller is in the SUSPENDED state. '
                   'The controller must be explicitely resumed ',
                   required=False, action='store_true')
    p.add_argument('--recover', help='Recover controller data from host memory ',
                   required=False, action='store_true')
    p.add_argument('--mem', help='Memory model', type=str,
                   required=False, choices=['static', 'pool']) 
    p.set_defaults(func=controller_virtio_blk_create)

    def controller_virtio_blk_bdev_attach(args):
        params = {
            'name': args.name,
            'bdev_type': args.bdev_type,
            'bdev': args.bdev,
        }
        if args.size_max:
            params['size_max'] = args.size_max
        if args.seg_max:
            params['seg_max'] = args.seg_max
        args.client.call('controller_virtio_blk_bdev_attach', params)
    p = subparsers.add_parser('controller_virtio_blk_bdev_attach',
                              help='Attach bdev to VirtIO BLK controller')
    p.add_argument('name', help='Controller Name', type=str)
    p.add_argument('bdev_type', help='Block device type', type=str,
                   choices=["spdk"])
    p.add_argument('bdev', help='Block device to use as backend', type=str)
    p.add_argument('--size_max', help='size_max PCI register value', type=int)
    p.add_argument('--seg_max', help='seg_max PCI register value', type=int)
    p.set_defaults(func=controller_virtio_blk_bdev_attach)

    def controller_virtio_blk_bdev_detach(args):
        params = {
            'name': args.name,
        }
        args.client.call('controller_virtio_blk_bdev_detach', params)
    p = subparsers.add_parser('controller_virtio_blk_bdev_detach',
                              help='Detach bdev from VirtIO BLK controller')
    p.add_argument('name', help='Controller Name', type=str)
    p.set_defaults(func=controller_virtio_blk_bdev_detach)

    def controller_virtio_blk_bdev_list(args):
        params = {
            'name': args.name,
        }
        result = args.client.call('controller_virtio_blk_bdev_list', params)
        print(json.dumps(result, indent=2))
    p = subparsers.add_parser('controller_virtio_blk_bdev_list',
                              help='List bdev attached to VirtIO BLK controller')
    p.add_argument('name', help='Controller Name', type=str)
    p.set_defaults(func=controller_virtio_blk_bdev_list)

    def controller_virtio_blk_get_debugstat(args):
        params = {
        }
        if args.name:
            params['name'] = args.name
        result = args.client.call('controller_virtio_blk_get_debugstat', params)
        print(json.dumps(result, indent=2))
    p = subparsers.add_parser('controller_virtio_blk_get_debugstat',
                              help='Get debug statistics from VirtIO BLK controller')
    p.add_argument('-c', '--name', help='Controller Name', type=str,
                   required=False)
    p.set_defaults(func=controller_virtio_blk_get_debugstat)

    def controller_nvme_delete(args):
        if args.subnqn is None and args.cntlid != -1:
            raise JsonRpcSnapException("subnqn and cntlid must be both configured,"
                                       " or neither of them should be configured");
        if args.subnqn is not None and args.cntlid == -1:
            raise JsonRpcSnapException("subnqn and cntlid must be both configured,"
                                       " or neither of them should be configured");
        if args.name is None and args.cntlid == -1:
            raise JsonRpcSnapException("Either ctrl name or subnqn/cntlid pair must be configured");
        if args.name is not None and args.cntlid != -1:
            raise JsonRpcSnapException("ctrl name and subnqn/cntlid pair cannot both be configured");
        params = {
        }
        if args.name:
            params['name'] = args.name
        if args.subnqn:
            params['subnqn'] = args.subnqn
        if args.cntlid != -1:
            params['cntlid'] = args.cntlid
        args.client.call('controller_nvme_delete', params)
    p = subparsers.add_parser('controller_nvme_delete',
                              help='Destroy NVMe SNAP controller')
    p.add_argument('-c', '--name', help='Controller Name. Must be set if \'--nqn\' '
                   'and \'--cntlid\' are not set', type=str, required=False)
    p.add_argument('-n', '--subnqn', help='NVMe subsystem nqn.'
                   ' Must be set if \'--name\' is not set', type=str, required=False)
    p.add_argument('-i', '--cntlid', help='Controller Identifier in NVMe subsystem.'
                   ' Must be set if \'--name\' is not set',
                   default=-1, type=int, required=False)
    p.set_defaults(func=controller_nvme_delete)

    def controller_nvme_create(args):
        if args.pci_bdf is None and args.pf_id == -1:
            raise JsonRpcSnapException("Either pci_bdf or pf_id must "
                                       "be configured")
        if args.pci_bdf is not None and args.pf_id != -1:
            raise JsonRpcSnapException("pci_bdf and pf_id cannot be "
                                       "both configured")

        if args.nqn is None and args.subsys_id == -1:
            raise JsonRpcSnapException("Either nqn  or subsys_id must "
                                       "be configured")
        if args.nqn is not None and args.subsys_id != -1:
            raise JsonRpcSnapException("nqn and subsys_id cannot be "
                                       "both configured")

        params = {
            'emulation_manager': args.emu_manager,
        }
        if args.nqn:
            params['nqn'] = args.nqn
        if args.subsys_id:
            params['subsys_id'] = args.subsys_id
        if args.pci_bdf:
            params['pci_bdf'] = args.pci_bdf
        if args.pf_id != -1:
            params['pf_id'] = args.pf_id
        if args.pf_id != -1 and args.vf_id != -1:
            params['vf_id'] = args.vf_id
        if args.conf:
            params['conf_file'] = args.conf
        if args.nr_io_queues != -1:
            params['nr_io_queues'] = args.nr_io_queues
        if args.mdts != -1:
            params['mdts'] = args.mdts
        if args.max_namespaces != -1:
            params['max_namespaces'] = args.max_namespaces
        if args.quirks != -1:
            params['quirks'] = args.quirks
        if args.rdma_device:
            params['rdma_device'] = args.rdma_device
        if args.version:
            params['version'] = args.version
        if args.mem:
            params['mem'] = args.mem            

        result = args.client.call('controller_nvme_create', params)
        print(json.dumps(result, indent=2).strip('"'))
    p = subparsers.add_parser('controller_nvme_create',
                              help='Create new NVMe SNAP controller')
    p.add_argument('--nqn', help='NVMe subsystem nqn. Must be set if'
                   ' \'--subsys_id\' is not set', type=str, required=False)
    p.add_argument('--subsys_id', help='NVMe subsystem id. '
                   'Must be set if \'--nqn\' is not set',
                    default=-1, type=int, required=False)
    p.add_argument('emu_manager', help='Emulation manager', type=str)
    p.add_argument('-d', '--pci_bdf', help='PCI device to start emulation on. '
                   'Must be set if \'--pf_id\' is not set',
                   type=str, required=False)
    p.add_argument('--pf_id', help='PCI PF index to start emulation on. '
                   'Must be set if \'--pci_bdf\' is not set',
                   default=-1, type=int, required=False)
    p.add_argument('--vf_id', help='PCI VF index to start emulation on. '
                   '\'--pf_id\' must also be set to take effect',
                   default=-1, type=int, required=False)
    p.add_argument('-c', '--conf', help='JSON configuration file to use',
                   type=str, required=False)
    p.add_argument('-n', '--nr_io_queues', help='IO queue number to NVMe controller',
                   default=-1, type=int, required=False)
    p.add_argument('-t', '--mdts', help='Maximum Data Transfer Size',
                   default=-1, type=int, required=False)
    p.add_argument('-m', '--max_namespaces', help='Maximun number of namespace',
                   default=1024, type=int, required=False)
    p.add_argument('-q', '--quirks', help='Bitmask for enabling specific NVMe '
                   'driver quirks in order to work with non NVMe spec compliant drivers',
                   default=-1, type=int, required=False)
    p.add_argument('-r', '--rdma_device', help='BlueField1 compatibility option. Should be '
                   'SF hca name. Usually "mlx5_2"',
                   type=str, required=False)
    p.add_argument('-vs', '--version', help='Host driver NVM Express specification version ',
                   default="1.3.0", type=str, required=False)
    p.add_argument('--mem', help='Memory model', type=str,
                   required=False, choices=['static', 'pool']) 
    p.set_defaults(func=controller_nvme_create)

    def controller_list(args):
        params = {}
        if args.type:
            params['type'] = args.type

        result = args.client.call('controller_list')
        print(json.dumps(result, indent=2))
    __help = 'List all SNAP active controllers with their characteristics'
    p = subparsers.add_parser('controller_list', help=__help)
    p.add_argument('-t', '--type', help='Controller Type',
                   choices=["nvme", "virtio_blk", "virtio_net"], type=str,
                   required=False)
    p.set_defaults(func=controller_list)

    def emulation_functions_list(args):
        result = args.client.call('emulation_functions_list')
        print(json.dumps(result, indent=2))
    __help = 'List all SNAP plugged emulation functions with their characteristics'
    p = subparsers.add_parser('emulation_functions_list', help=__help)
    p.set_defaults(func=emulation_functions_list)

    def emulation_managers_list(args):
        result = args.client.call('emulation_managers_list')
        print(json.dumps(result, indent=2))
    __help = 'List all SNAP emulation managers with their characteristics'
    p = subparsers.add_parser('emulation_managers_list', help=__help)
    p.set_defaults(func=emulation_managers_list)

    def controller_nvme_namespace_detach(args):
        if args.subnqn is None and args.cntlid != -1:
            raise JsonRpcSnapException("subnqn and cntlid must be both configured,"
                                       " or neither of them should be configured");
        if args.subnqn is not None and args.cntlid == -1:
            raise JsonRpcSnapException("subnqn and cntlid must be both configured,"
                                       " or neither of them should be configured");
        if args.ctrl is None and args.cntlid == -1:
            raise JsonRpcSnapException("Either ctrl name or subnqn/cntlid pair must be configured");
        if args.ctrl is not None and args.cntlid != -1:
            raise JsonRpcSnapException("ctrl name and subnqn/cntlid pair cannot both be configured");
        params = {
            'nsid': args.nsid,
        }
        if args.ctrl:
            params['ctrl'] = args.ctrl
        if args.subnqn:
            params['subnqn'] = args.subnqn
        if args.cntlid != -1:
            params['cntlid'] = args.cntlid
        args.client.call('controller_nvme_namespace_detach', params)
    p = subparsers.add_parser('controller_nvme_namespace_detach',
                              help='Delete NVMe emulation attached namespace')
    p.add_argument('-c', '--ctrl', help='Controller Name. Must be set if \'--nqn\' '
                   'and \'--cntlid\' are not set', type=str, required=False)
    p.add_argument('-n', '--subnqn', help='NVMe subsystem nqn.'
                   ' Must be set if \'--ctrl\' is not set', type=str, required=False)
    p.add_argument('-i', '--cntlid', help='Controller Identifier in NVMe subsystem.'
                   ' Must be set if \'--ctrl\' is not set',
                   default=-1, type=int, required=False)
    p.add_argument('nsid', help='Namespace id (NSID) to delete', type=int)
    p.set_defaults(func=controller_nvme_namespace_detach)

    def controller_nvme_namespace_attach(args):
        if args.subnqn is None and args.cntlid != -1:
            raise JsonRpcSnapException("subnqn and cntlid must be both configured,"
                                       " or neither of them should be configured");
        if args.subnqn is not None and args.cntlid == -1:
            raise JsonRpcSnapException("subnqn and cntlid must be both configured,"
                                       " or neither of them should be configured");
        if args.ctrl is None and args.cntlid == -1:
            raise JsonRpcSnapException("Either ctrl name or subnqn/cntlid pair must be configured");
        if args.ctrl is not None and args.cntlid != -1:
            raise JsonRpcSnapException("ctrl name and subnqn/cntlid pair cannot both be configured");
        params = {
            'bdev_type': args.bdev_type,
            'bdev': args.bdev,
            'nsid': args.nsid,
        }
        if args.ctrl:
            params['ctrl'] = args.ctrl
        if args.subnqn:
            params['subnqn'] = args.subnqn
        if args.cntlid != -1:
            params['cntlid'] = args.cntlid
        if args.qn:
            params['qn'] = args.qn
        if args.protocol:
            params['protocol'] = args.protocol
        if args.uuid:
            params['uuid'] = args.uuid
        if args.nguid:
            params['nguid'] = args.nguid
        if args.eui64:
            params['eui64'] = args.eui64
        args.client.call('controller_nvme_namespace_attach', params)
    p = subparsers.add_parser('controller_nvme_namespace_attach',
                              help='Add new NVMe emulation namespace')
    p.add_argument('-c', '--ctrl', help='Controller Name. Must be set if \'--nqn\' '
                   'and \'--cntlid\' are not set', type=str, required=False)
    p.add_argument('-n', '--subnqn', help='NVMe subsystem nqn.'
                   ' Must be set if \'--ctrl\' is not set', type=str, required=False)
    p.add_argument('-i', '--cntlid', help='Controller Identifier in NVMe subsystem.'
                   ' Must be set if \'--ctrl\' is not set',
                   default=-1, type=int, required=False)
    p.add_argument('bdev_type', help='Block device type', type=str)
    p.add_argument('bdev', help='Block device to use as backend', type=str)
    p.add_argument('nsid', help='Namespace id (NSID)', type=int)
    p.add_argument('-q', '--qn', help='QN of remote target which provide this ns', type=str)
    p.add_argument('-p', '--protocol', help='protocol used', type=str)
    p.add_argument('-g', '--nguid', help='Namespace globally unique identifier',
                   required=False)
    p.add_argument('-e', '--eui64', help='Namespace EUI-64 identifier',
                   required=False)
    p.add_argument('-u', '--uuid', help='Namespace UUID', required=False)
    p.set_defaults(func=controller_nvme_namespace_attach)

    def controller_nvme_namespace_list(args):
        if args.subnqn is None and args.cntlid != -1:
            raise JsonRpcSnapException("subnqn and cntlid must be both configured,"
                                       " or neither of them should be configured");
        if args.subnqn is not None and args.cntlid == -1:
            raise JsonRpcSnapException("subnqn and cntlid must be both configured,"
                                       " or neither of them should be configured");
        if args.ctrl is None and args.cntlid == -1:
            raise JsonRpcSnapException("Either ctrl name or subnqn/cntlid pair must be configured");
        if args.ctrl is not None and args.cntlid != -1:
            raise JsonRpcSnapException("ctrl name and subnqn/cntlid pair cannot both be configured");
        params = {
        }
        if args.ctrl:
            params['ctrl'] = args.ctrl
        if args.subnqn:
            params['subnqn'] = args.subnqn
        if args.cntlid != -1:
            params['cntlid'] = args.cntlid
        result = args.client.call('controller_nvme_namespace_list', params)
        print(json.dumps(result, indent=2))
    p = subparsers.add_parser('controller_nvme_namespace_list',
                              help='List attached namespaces on '
                                   'NVMe controller')
    p.add_argument('-c', '--ctrl', help='Controller Name. Must be set if \'--nqn\' '
                   'and \'--cntlid\' are not set', type=str, required=False)
    p.add_argument('-n', '--subnqn', help='NVMe subsystem nqn.'
                   ' Must be set if \'--ctrl\' is not set', type=str, required=False)
    p.add_argument('-i', '--cntlid', help='Controller Identifier in NVMe subsystem.'
                   ' Must be set if \'--ctrl\' is not set',
                   default=-1, type=int, required=False)
    p.set_defaults(func=controller_nvme_namespace_list)

    def subsystem_nvme_create(args):
        params = {
            'serial_number': args.serial_number,
            'model_number': args.model_number,
        }
        if args.nqn:
            params['nqn'] = args.nqn
        if args.max_nsid:
            params['number_namespaces'] = args.max_nsid
        if args.maximum_namespaces:
            params['maximum_namespaces'] = args.maximum_namespaces
        result = args.client.call('subsystem_nvme_create', params)
        print(json.dumps(result, indent=2).strip('"'))
    p = subparsers.add_parser('subsystem_nvme_create',
                              help='Create new NVMe subsystem')
    p.add_argument('--nqn', help='Subsystem NQN', type=str, required=False)
    p.add_argument('serial_number', help='Subsystem serial number', type=str)
    p.add_argument('model_number', help='Subsystem model number', type=str)
    p.add_argument('-nn', '--max_nsid',
                   help='Maximum value of a valid NSID for the NVM subsystem.',
                   type=int, required=False)
    p.add_argument('-mnan', '--maximum_namespaces',
                   help='Maximum number of namespaces supported by the NVM subsystem.',
                   type=int, required=False)
    p.set_defaults(func=subsystem_nvme_create)

    def subsystem_nvme_delete(args):
        params = {
            'nqn': args.nqn,
        }
        args.client.call('subsystem_nvme_delete', params)
    p = subparsers.add_parser('subsystem_nvme_delete',
                              help='Delete NVMe subsystem')
    p.add_argument('nqn', help='Subsystem NQN', type=str)
    p.set_defaults(func=subsystem_nvme_delete)

    def subsystem_nvme_list(args):
        result = args.client.call('subsystem_nvme_list')
        print(json.dumps(result, indent=2))
    p = subparsers.add_parser('subsystem_nvme_list',
                              help='List NVMe subsystems')
    p.set_defaults(func=subsystem_nvme_list)
    
    def controller_nvme_get_iostat(args):
        params = {
        }
        if args.name:
            params['name'] = args.name

        result = args.client.call('controller_nvme_get_iostat', params)
        print(json.dumps(result, indent=2).strip('"'))
    p = subparsers.add_parser('controller_nvme_get_iostat',
                        help='Return NVMe SNAP controller I/O statistics')
    p.add_argument('-c', '--name', help='Controller Name', 
                   type=str, required=False)
    
    p.set_defaults(func=controller_nvme_get_iostat)
    
    def controller_nvme_get_debugstat(args):
        params = {
                'fw_counters': args.fw_counters
        }
        if args.name:
            params['name'] = args.name

        result = args.client.call('controller_nvme_get_debugstat', params)
        print(json.dumps(result, indent=2).strip('"'))
    p = subparsers.add_parser('controller_nvme_get_debugstat',
                help='Return NVMe SNAP controller debug statistics')
    p.add_argument('-c', '--name', help='Controller Name',
                   type=str, required=False)
    p.add_argument('-fw', '--firmware', dest='fw_counters', 
                   action='store_true', 
                   help='Force using firmware counters (relevant for CC mode')
    p.set_defaults(fw_counters=False)

    p.set_defaults(func=controller_nvme_get_debugstat)
            
    def controller_virtio_blk_get_iostat(args):
        params = {
        }
        if args.name:
            params['name'] = args.name

        result = args.client.call('controller_virtio_blk_get_iostat', params)
        print(json.dumps(result, indent=2).strip('"'))
    p = subparsers.add_parser('controller_virtio_blk_get_iostat',
                              help='Return VirtIO BLK SNAP controller I/O statistics')
    p.add_argument('-c', '--name', help='Controller Name', 
                   type=str, required=False)
    p.set_defaults(func=controller_virtio_blk_get_iostat)

    def mlnx_snap_params_list(args):
        params = {}

        result = args.client.call('mlnx_snap_params_list')
        print(json.dumps(result, indent=2))
    __help = 'List of parameters used in mlnx_snap ( /etc/default/mlnx_snap )'
    p = subparsers.add_parser('mlnx_snap_params_list', help=__help)
    p.set_defaults(func=mlnx_snap_params_list)
    
    def storage_admin(args):
        params = {
            'protocol': args.protocol,
            'op': args.op,
        }
        if args.policy:
            params['policy'] = args.policy
        if args.qn:
            params['qn'] = args.qn
        if args.hostqn:
            params['hostqn'] = args.hostqn
        if args.path:
            params['paths'] = args.path
        if args.dev:
            params['dev'] = args.dev
        if args.type:
            params['type'] = args.type

        result = args.client.call('storage_admin', params)
        print(json.dumps(result, indent=2))        
    p = subparsers.add_parser('storage_admin',
                              help='Execute a storage_admin command on ARM')
    p.add_argument('protocol', help='Storage protocol to use', type=str,
                   choices=['nvme'])
    p.add_argument('op', help='Operation to be run on ARM', type=str,
                   choices=['connect', 'disconnect', 'discover', 'get_bdev_info'])
    p.add_argument('--policy', help="IO policy to use", required=False,
                   type=str)
    p.add_argument('--qn', help="Remote qualified name", required=False,
                   type=str)
    p.add_argument('--hostqn', help="Host qualified name", required=False,
                   type=str)
    p.add_argument('--path', action="append",
                   help="Path(s) for remote", required=False,
                   type=str)
    p.add_argument('--dev', help="Block Device", required=False, type=str)
    p.add_argument('--type', help="Block Device Type",
                   choices=['kernel', 'spdk'], required=False, type=str)
    p.set_defaults(func=storage_admin)

    def mempool_get_debugstat(args):
        params = {
        }
        result = args.client.call('mempool_get_debugstat', params)
        print(json.dumps(result, indent=2))
    p = subparsers.add_parser('mempool_get_debugstat',
                              help='Get debug statistics from memory pool')
    p.set_defaults(func=mempool_get_debugstat)
    
    def call_rpc_func(args):
        args.func(args)

    def execute_script(parser, client, fd):
        executed_rpc = ""
        for rpc_call in map(str.rstrip, fd):
            if not rpc_call.strip():
                continue
            executed_rpc = "\n".join([executed_rpc, rpc_call])
            args = parser.parse_args(shlex.split(rpc_call))
            args.client = client
            try:
                call_rpc_func(args)
            except JsonRpcSnapException as ex:
                print("Exception:")
                print(executed_rpc.strip() + " <<<")
                print(ex.message)
                exit(1)

    args = parser.parse_args()
    args.client = JsonRpcSnapClient(args.server_addr, args.timeout)
    if hasattr(args, 'func'):
        try:
            call_rpc_func(args)
        except JsonRpcSnapException as ex:
            print(ex)
            exit(1)
    elif sys.stdin.isatty():
        # No arguments and no data piped through stdin
        parser.print_help()
        exit(1)
    else:
        execute_script(parser, args.client, sys.stdin)


if __name__ == "__main__":
    main()
