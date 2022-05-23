#!/bin/bash -eEl

if [ "$CCACHE_ENABLE" = true ]; then
    export PATH="/usr/lib/ccache:/usr/lib64/ccache:$PATH"
fi

if [ ! -d .git ]; then
    echo "Error: should be run from project root"
    exit 1
fi

echo "==== Running coverity ===="

ncpus=$(cat /proc/cpuinfo|grep processor|wc -l)
export AUTOMAKE_JOBS=$ncpus


if [ ! -z "$GCC_VER" ]; then
#   Coverity: the highest supported version of gcc is 9 (=> 10 not supported yet)
#   https://sig-docs.synopsys.com/polaris/topics/c_coverity-compatible-platforms.html
#   Override the CXX/GCC/CC vars if the GCC_VER variable is passed from the docker file
#   On Ubuntu 22.04 we need to use gcc 9 (defined inside docker file)
#   Overrided gcc used ONLY for coverity scan
    echo "GCC_VER passed from the docker file: ${GCC_VER}"
    test -f /usr/bin/g++-${GCC_VER} && export CXX=/usr/bin/g++-${GCC_VER}
    test -f /usr/bin/gcc-${GCC_VER} && export GCC=/usr/bin/gcc-${GCC_VER}
    export CC=/usr/bin/gcc-${GCC_VER}
fi

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
