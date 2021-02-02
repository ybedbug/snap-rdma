#!/bin/bash -eE

SCRIPT=$(readlink -f "$0")
SCRIPTPATH=$(dirname "$SCRIPT")
WD=${WORKSPACE:-$(dirname $SCRIPTPATH)}
DIRS_TO_CHECK="blk src ctrl rpc"

code_style_check() {

    if ! command -v checkpatch.pl &> /dev/null; then
        echo "checkpatch.pl could not be found!"
        exit 1
    fi

    # Gather files to check with ext .c and .h
    files=$(for d in ${DIRS_TO_CHECK}; do find $WD/$d/ -type f -name '*.[ch]'; done)

    # Set output dir
    out_dir="$WD/code_style"
    [ -d $out_dir ] && rm -rf $out_dir
    mkdir -p $out_dir

    # Find checkpatch configuration file
    test -f "${SCRIPTPATH}/.checkpatch.conf" && _home="${SCRIPTPATH}"

    total_err=0

    for f in $files; do

        echo -e "\n****************************************************"
        echo "Check:  $f"
        echo -e "****************************************************\n"

        out_log="$out_dir/${f##*/}.log"

        HOME="${_home:-${HOME}}" checkpatch.pl \
            --mailback --no-tree --file --color=always \
            --summary-file --show-types $f | \
            tee -a $out_log || true

        err_num=$(grep total: $out_log | awk -F ' ' '{print $3}')
        total_err=$(( total_err + err_num ))

    done

    #exit $total_err
    exit 0
}

code_style_check
