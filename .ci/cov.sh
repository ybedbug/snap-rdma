#!/bin/bash -eEl

topdir=$(git rev-parse --show-toplevel)
cd $topdir

if [ ! -d .git ]; then
    echo "Error: should be run from project root"
    exit 1
fi

echo "==== Running coverity ===="

ncpus=$(cat /proc/cpuinfo|grep processor|wc -l)
export AUTOMAKE_JOBS=$ncpus

./autogen.sh

module load tools/cov

./configure

cov_build="cov_build"
rm -rf $cov_build

cov-build --dir $cov_build make -j $ncpus all
cov-analyze --jobs $ncpus $COV_OPT --security --concurrency --dir $cov_build
cov-format-errors --dir $cov_build --html-output $cov_build/html

#nerrors=$(cov-format-errors --dir $cov_build | awk '/Processing [0-9]+ errors?/ { print $2 }')
#rc=$(($rc+$nerrors))

#exit $rc
