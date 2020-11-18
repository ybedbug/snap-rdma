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
        }
        if args.pci_bdf:
            params['pci_bdf'] = args.pci_bdf
        if args.pci_index != -1:
            params['pci_index'] = args.pci_index
        args.client.call('emulation_device_detach', params)
    p = subparsers.add_parser('emulation_device_detach',
                              help='Detach (Unplug) SNAP device from host')
    p.add_argument('emu_manager', help='Emulation manager', type=str)
    p.add_argument('-d', '--pci_bdf', help='PCI device to start emulation on. '
                   'Must be set if \'--pci_index\' is not set',
                   type=str, required=False)
    p.add_argument('-i', '--pci_index', help='PCI index to start emulation on. '
                   'Must be set if \'--pci_bdf\' is not set',
                   default=-1, type=int, required=False)
    p.set_defaults(func=emulation_device_detach)

    def emulation_device_attach_virtio_blk(args):
        params = {
            'emulation_manager': args.emu_manager,
            'ssid': args.ssid,
            'ssvid': args.ssvid,
        }
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
        result = args.client.call('emulation_device_attach_virtio_blk', params)
        print(json.dumps(result, indent=2))
    p = subparsers.add_parser('emulation_device_attach_virtio_blk',
                              help='Attach (plug) VirtIO BLK SNAP device '
                                   'to host')
    p.add_argument('emu_manager', help='Emulation manager', type=str)
    p.add_argument('--ssid', help='Subsystem ID', type=int, default=0)
    p.add_argument('--ssvid', help='Subsystem Vendor ID', type=int, default=0)
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
    p.set_defaults(func=emulation_device_attach_virtio_blk)

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
    p.add_argument('--bdev_type', help='Block device type', type=str,
                   choices=["spdk"], required=True)
    p.add_argument('--bdev', help='Block device to use as backend', type=str,
                   required=True)
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

    def controller_nvme_delete(args):
        params = {
            'name': args.name,
        }
        args.client.call('controller_nvme_delete', params)
    p = subparsers.add_parser('controller_nvme_delete',
                              help='Destroy NVMe SNAP controller')
    p.add_argument('name', help='Controller Name', type=str)
    p.set_defaults(func=controller_nvme_delete)

    def controller_nvme_create(args):
        if args.pci_bdf is None and args.pci_index == -1:
            raise JsonRpcSnapException("Either pci_bdf or pci_index must "
                                       "be configured")
        if args.pci_bdf is not None and args.pci_index != -1:
            raise JsonRpcSnapException("pci_bdf and pci_index cannot be "
                                       "both configured")
        params = {
            'nqn': args.nqn,
            'emulation_manager': args.emu_manager,
        }
        if args.pci_bdf:
            params['pci_bdf'] = args.pci_bdf
        if args.pci_index != -1:
            params['pci_index'] = args.pci_index
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

        result = args.client.call('controller_nvme_create', params)
        print(json.dumps(result, indent=2).strip('"'))
    p = subparsers.add_parser('controller_nvme_create',
                              help='Create new NVMe SNAP controller')
    p.add_argument('nqn', help='NVMe subsystem nqn', type=str)
    p.add_argument('emu_manager', help='Emulation manager', type=str)
    p.add_argument('-d', '--pci_bdf', help='PCI device to start emulation on. '
                   'Must be set if \'--pci_index\' is not set',
                   type=str, required=False)
    p.add_argument('-i', '--pci_index', help='PCI index to start emulation on. '
                   'Must be set if \'--pci_bdf\' is not set',
                   default=-1, type=int, required=False)
    p.add_argument('-c', '--conf', help='JSON configuration file to use',
                   type=str, required=False)
    p.add_argument('-n', '--nr_io_queues', help='IO queue number to NVMe controller',
                   default=-1, type=int, required=False)
    p.add_argument('-t', '--mdts', help='Maximum Data Transfer Size',
                   default=-1, type=int, required=False)
    p.add_argument('-m', '--max_namespaces', help='Maximun number of namespace',
                   default=-1, type=int, required=False)
    p.add_argument('-q', '--quirks', help='Bitmask for enabling specific NVMe '
                   'driver quirks in order to work with non NVMe spec compliant drivers',
                   default=-1, type=int, required=False)
    p.add_argument('-r', '--rdma_device', help='BlueField1 compatibility option. Should be '
                   'SF hca name. Usually "mlx5_2"',
                   type=str, required=False)
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
        params = {
            'ctrl': args.ctrl,
            'nsid': args.nsid,
        }
        args.client.call('controller_nvme_namespace_detach', params)
    p = subparsers.add_parser('controller_nvme_namespace_detach',
                              help='Delete NVMe emulation attached namespace')
    p.add_argument('ctrl', help='Controller Name', type=str)
    p.add_argument('nsid', help='Namespace id (NSID) to delete', type=int)
    p.set_defaults(func=controller_nvme_namespace_detach)

    def controller_nvme_namespace_attach(args):
        params = {
            'ctrl': args.ctrl,
            'bdev_type': args.bdev_type,
            'bdev': args.bdev,
            'nsid': args.nsid,
        }
        args.client.call('controller_nvme_namespace_attach', params)
    p = subparsers.add_parser('controller_nvme_namespace_attach',
                              help='Add new NVMe emulation namespace')
    p.add_argument('ctrl', help='Controller Name', type=str)
    p.add_argument('bdev_type', help='Block device type', type=str,
                   choices=["spdk"])
    p.add_argument('bdev', help='Block device to use as backend', type=str)
    p.add_argument('nsid', help='Namespace id (NSID)', type=int)
    p.set_defaults(func=controller_nvme_namespace_attach)

    def controller_nvme_namespace_list(args):
        params = {
            'ctrl': args.ctrl,
        }
        result = args.client.call('controller_nvme_namespace_list', params)
        print(json.dumps(result, indent=2))
    p = subparsers.add_parser('controller_nvme_namespace_list',
                              help='List attached namespaces on '
                                   'NVMe controller')
    p.add_argument('ctrl', help='Controller Name', type=str)
    p.set_defaults(func=controller_nvme_namespace_list)

    def subsystem_nvme_create(args):
        params = {
            'nqn': args.nqn,
            'serial_number': args.serial_number,
            'model_number': args.model_number,
        }
        args.client.call('subsystem_nvme_create', params)
    p = subparsers.add_parser('subsystem_nvme_create',
                              help='Create new NVMe subsystem')
    p.add_argument('nqn', help='Subsystem NQN', type=str)
    p.add_argument('serial_number', help='Subsystem serial number', type=str)
    p.add_argument('model_number', help='Subsystem model number', type=str)
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

    def dpu_exec(args):
        file_args = None
        params = {
            'file': args.file,
        }
        if args.args:
            file_args = []
            for i in args.args.strip().split(' '):
                file_args.append(i)
            params['args'] = file_args
        result = args.client.call('dpu_exec', params)
        print(json.dumps(result, indent=2))
    p = subparsers.add_parser('dpu_exec',
                              help='Execute a program file with provide arguments')
    p.add_argument('file', help='Executable program file name', type=str)
    p.add_argument('-a', dest='args', help="""whitespace-separated list of
                   arguments to file enclosed in quotes. This parameter
                   can be ommited. Example:
                   '--op=connect --paths=2 --policy=round-robin --protocol=nvme --qn=sub0 '
                   '--hostqn=nqn.2020-10.snic.rsws05:1 --transport=rdma '
                   '--paths=adrfam:ipv4/traddr:1.1.1.1/trsvcid:4420,adrfam:ipv4/traddr:1.1.1.2/trsvcid:4421' etc""",
                   required=False)
    p.set_defaults(func=dpu_exec)

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
