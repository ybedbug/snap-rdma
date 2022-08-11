#!/bin/bash -eE

SCRIPT=$(readlink -f "$0")
SCRIPTPATH=$(dirname "$SCRIPT")
WD=${WORKSPACE:-$(dirname $SCRIPTPATH)}
DIRS_TO_CHECK="blk src ctrl rpc"


if ! command -v codespell &> /dev/null; then
  echo "codespell could not be found!"
  exit 1
fi

codespell -I $SCRIPTPATH/codespell_ignore.txt $DIRS_TO_CHECK
