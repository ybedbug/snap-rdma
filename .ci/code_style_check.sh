#!/bin/bash -eE

SCRIPT=$(readlink -f "$0")
SCRIPTPATH=$(dirname "$SCRIPT")
WD=${WORKSPACE:-$(dirname $SCRIPTPATH)}
DIRS_TO_CHECK="blk src ctrl rpc"

code_style_check() {

    if test -f "./checkpatch.pl"; then
    	CHECKPATCH_PL=./checkpatch.pl
    else
        if ! command -v checkpatch.pl &> /dev/null; then
            echo "checkpatch.pl could not be found!"
            exit 1
        fi
    	CHECKPATCH_PL=checkpatch.pl
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

        HOME="${_home:-${HOME}}" $CHECKPATCH_PL \
            --mailback --no-tree --file --color=always \
            --summary-file --show-types $f | \
            tee -a $out_log || true

	err_num=$(grep ERROR $out_log | wc -l)
	warn_num=$(grep WARNING $out_log | wc -l)
	echo "$f Error Summary: $err_num errors, $warn_num warnings"
        total_err=$(( total_err + err_num + warn_num))

    done

    exit $total_err
}

code_style_check
