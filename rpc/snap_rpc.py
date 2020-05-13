#!/usr/bin/env python3

from rpc.client import print_json, print_dict, JSONRPCException

import logging
import argparse
import rpc
import sys
import shlex

try:
    from shlex import quote
except ImportError:
    from pipes import quote


def print_array(a):
    print(" ".join((quote(v) for v in a)))


def main():
    parser = argparse.ArgumentParser(
        description='Mellanox SNAP JSON-RPC 2.0 command line interface')
    parser.add_argument('-s', dest='server_addr',
                        help='RPC domain socket path or IP address',
                        default='/var/tmp/spdk.sock')
    parser.add_argument('-p', dest='port',
                        help='RPC port number (if server_addr is IP address)',
                        default=5260, type=int)
    parser.add_argument('-t', dest='timeout',
                        help='Timeout as a floating point number expressed in '
                             'seconds waiting for response. Default: 60.0',
                        default=60.0, type=float)
    parser.add_argument('-v', dest='verbose',
                        choices=['DEBUG', 'INFO', 'ERROR'],
                        help="""Set verbosity level. """)
    subparsers = parser.add_subparsers(help='Mellanox SNAP JSON-RPC 2.0 Client methods',
                                       dest='called_rpc_name')

    def emulation_list(args):
        print_dict(args.client.call('emulation_list'))
    __help = 'List all SNAP plugged emulation functions with their characteristics'
    p = subparsers.add_parser('emulation_list', help=__help)
    p.set_defaults(func=emulation_list)

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
            except JSONRPCException as ex:
                print("Exception:")
                print(executed_rpc.strip() + " <<<")
                print(ex.message)
                exit(1)

    args = parser.parse_args()
    args.client = rpc.client.JSONRPCClient(args.server_addr,
                                           args.port,
                                           args.timeout,
                                           log_level=getattr(logging,
                                                             args.verbose))
    if hasattr(args, 'func'):
        try:
            call_rpc_func(args)
        except JSONRPCException as ex:
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
