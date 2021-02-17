#!/bin/bash -eE

if [ -e ./configure.ac ]; then
    AC_INIT=$(awk -F "AC_INIT" '/AC_INIT/ {v=$2; \
        gsub("[\\(\\[\\],\\)]*","", v); \
        print v}' ./configure.ac)
    read NAME VER EMAIL<<<$AC_INIT
else
    echo "./configure.ac not found!"
    exit 1
fi

if test -n "$BUILD_NUMBER" ; then
    _rev="$BUILD_NUMBER"
else
    echo "BUILD_NUMBER isn't defined!"
    exit 1
fi

git_tag="v${VER}-${_rev}"

git tag $git_tag
git push origin $git_tag

