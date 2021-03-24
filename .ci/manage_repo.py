#!/usr/bin/env python3

__author__ = "Andrii Holovchenko"
__version__ = '1.0'

import requests
import os
import json
import argparse
import sys


def usage():
    parser = argparse.ArgumentParser(description='Manage NEXUS 3 repositories')
    parser.add_argument('-n', '--name', type=str, required=True,
                        help='Set repository name')
    parser.add_argument('-u', '--url', type=str, required=True,
                        help='Nexus 3 repository URL. Example: http://swx-repos.mtr.labs.mlnx')
    parser.add_argument('-U', '--user', type=str, required=True,
                        help='Nexus 3 API username')
    parser.add_argument('-P', '--password', type=str, required=True,
                        help='Nexus 3 API password')

    main_parser = argparse.ArgumentParser()

    subparsers = main_parser.add_subparsers(help='Nexus repository type',
                                       dest='repo_type')
    subparsers.required = True

    # YUM subparser
    p_yum = subparsers.add_parser('yum', parents=[parser],
                                  add_help=False)
    p_yum.add_argument('-d', '--repo-data-depth', type=int, default=1,
                       help='Repository data depth')
    p_yum.add_argument('--blob-store', dest='blob_store', type=str, default='default',
                       help='Blob store used to store repository contents')
    p_yum.add_argument('--write-policy', dest='write_policy', type=str, choices=['allow_once', 'allow', 'deny'],
                       default='allow_once', help='controls if deployments of and updates to assets are allowed')
    p_yum.add_argument('-a', '--action', choices=['delete', 'create', 'show'],
                       default='show', help='Action to execute (default: show)')

    # APT subparser
    p_apt = subparsers.add_parser('apt', parents=[parser],
                                  add_help=False)
    p_apt.add_argument('-a', '--action', choices=['delete', 'create', 'show'],
                       default='show', help='Action to execute (default: show)')
    p_apt.add_argument('--blob-store', dest='blob_store', type=str, default='default',
                       help='Blob store used to store repository contents')
    p_apt.add_argument('--write-policy', dest='write_policy', type=str, choices=['allow_once', 'allow', 'deny'],
                       default='allow_once', help='controls if deployments of and updates to assets are allowed')
    p_apt.add_argument('--distro', type=str,
                       help='UBUNTU/DEBIAN distribution (Example: bionic, focal)')
    p_apt.add_argument('--passphrase', type=str,
                       help='Passphrase to access PGP signing key')

    p_apt_group = p_apt.add_mutually_exclusive_group()
    p_apt_group.add_argument('--keypair', type=str, default='default',
                             help='PGP signing key pair (armored private key e.g. gpg --export-secret-key --armor)')
    p_apt_group.add_argument('--keypair-file', type=argparse.FileType('r'), dest='keypair_file',
                             help='Read PGP signing key pair from a file')

    #args = parser.parse_args()
    args = main_parser.parse_args()

    if args.repo_type == 'apt':
        if args.action == 'create':
            if not args.keypair or not args.keypair_file:
                main_parser.error('one of the arguments --keypair --keypair-file is required')
            if not args.distro:
                main_parser.error('the following arguments are required: --distro')

    return args

def delete_repository(url, name, user, password):
    api_url = "%s/service/rest/v1/repositories/%s" % (url, name)
    response = requests.delete(api_url, auth=requests.auth.HTTPBasicAuth(user, password))

    if response.status_code == 204:
        print('Repository has been deleted: %s' % name)
        return 0
    elif response.status_code == 404:
        print('Repository not found: %s' % name)
    elif response.status_code == 403:
        print('Insufficient permissions to delete repository: %s' % name)
    else:
        print('Error')
    sys.exit(1)


def get_repositories(url):
    api_url = "%s/service/rest/v1/repositories" % (url)
    response = requests.get(api_url)
    if response.status_code == 200:
        return json.loads(response.content)
    sys.exit(1)


def create_yum_repo(url, name, user, password, repo_data_depth,
                    blob_store, write_policy):

    api_url = "%s/service/rest/v1/repositories/yum/hosted" % url

    params = {
          "name": name,
          "online": "true",
          "storage": {
                  "blobStoreName": blob_store,
                  "strictContentTypeValidation": "true",
                  "writePolicy": write_policy
                },
          "cleanup": {
                  "policyNames": [
                            "string"
                          ]
                },
          "component": {
                  "proprietaryComponents": "false"
                },
          "yum": {
                  "repodataDepth": repo_data_depth,
                  "deployPolicy": "STRICT"
                }
    }

    print("Creating hosted yum repository: %s" % name)
    response = requests.post(api_url, json=params,
                             auth=requests.auth.HTTPBasicAuth(user, password))

    if response.status_code == 201:
        print('Done')
    else:
        print('Failed to create repository: %s' % name)
        print('Status code: %s' % response.status_code)
        print('Response: %s' % response.content)
        sys.exit(1)


def get_yum_repo(url, name, user=None, password=None):
    api_url = "%s/service/rest/v1/repositories/yum/hosted/%s" % (url, name)

    auth_params = None

    if user is not None and password is not None:
        auth_params = requests.auth.HTTPBasicAuth(user, password)

    response = requests.get(api_url, auth=auth_params)
    if response.status_code == 200:
        response_json = json.loads(response.content)
        print(json.dumps(response_json, indent=2))
    else:
        print('Failed to get repository: %s' % name)
        print('Status code: %s' % response.status_code)
        print('Response: %s' % response.content)
        sys.exit(1)


def create_apt_repo(url, name, user, password,
                    blob_store, write_policy,
                    distribution, keypair=None, keypair_file=None,
                    passphrase=None):

    api_url = "%s/service/rest/v1/repositories/apt/hosted" % url

    if keypair_file:
        keypair = keypair_file.read()

    params = {
          "name": name,
          "online": "true",
          "storage": {
            "blobStoreName": blob_store,
            "strictContentTypeValidation": "true",
            "writePolicy": write_policy
          },
          "cleanup": {
            "policyNames": [
              "string"
            ]
          },
          "component": {
            "proprietaryComponents": "true"
          },
          "apt": {
            "distribution": distribution
          },
          "aptSigning": {
            "keypair": keypair,
            "passphrase": passphrase
          }
    }

    print("Creating hosted APT repository: %s" % name)
    response = requests.post(api_url, json=params,
                             auth=requests.auth.HTTPBasicAuth(user, password))

    if response.status_code == 201:
        print('Done')
    else:
        print('Failed to create repository: %s' % name)
        print('Status code: %s' % response.status_code)
        print('Response: %s' % response.content)
        sys.exit(1)


def get_apt_repo(url, name, user=None, password=None):
    api_url = "%s/service/rest/v1/repositories/apt/hosted/%s" % (url, name)

    auth_params = None

    if user is not None and password is not None:
        auth_params = requests.auth.HTTPBasicAuth(user, password)

    response = requests.get(api_url, auth=auth_params)
    if response.status_code == 200:
        response_json = json.loads(response.content)
        print(json.dumps(response_json, indent=2))
    else:
        print('Failed to get repository: %s' % name)
        print('Status code: %s' % response.status_code)
        print('Response: %s' % response.content)
        sys.exit(1)

def main(args):
    repo_list = get_repositories(args.url)
    if args.action == 'delete':
        delete_repository(args.url, args.name, args.user, args.password)
    if args.repo_type == 'yum':
        if args.action == 'create':
            if args.name not in [repo['name'] for repo in repo_list]:
                create_yum_repo(args.url, args.name, args.user, args.password,
                                args.repo_data_depth, args.blob_store, args.write_policy)
            else:
                print('Repository already exists: %s' % args.name)
        if args.action == 'show':
            get_yum_repo(args.url, args.name, args.user, args.password)
    if args.repo_type == 'apt':
        if args.action == 'create':
            if args.name not in [repo['name'] for repo in repo_list]:
                create_apt_repo(args.url, args.name, args.user, args.password,
                                args.blob_store, args.write_policy, args.distro,
                                args.keypair, args.keypair_file, args.passphrase)
            else:
                print('Repository already exists: %s' % args.name)
        if args.action == 'show':
            get_apt_repo(args.url, args.name, args.user, args.password)

if __name__ == '__main__':
    args = usage()
    main(args)

