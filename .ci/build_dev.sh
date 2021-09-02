#!/bin/bash -eE

if [ "$CCACHE_ENABLE" = true ]; then
    export PATH="/usr/lib/ccache:/usr/lib64/ccache:$PATH"
fi

create_dist_tarball() {

    # create dist tarball in current directory
    TARBALL="$NAME-$VER.tar.gz"
    if [ ! -e $TARBALL ] ; then
        autoreconf -ivf
        ./configure --prefix=/usr --with-gtest=no
        make distcheck
    fi
}

build_rpm() {
    create_dist_tarball
    mkdir -p $HOME/rpmbuild/{SOURCES,RPMS,SRPMS,SPECS,BUILD,BUILDROOT}
    rpmbuild -ta $NAME-$VER.tar.gz
}

build_deb() {
    create_dist_tarball
    if [ -e $NAME-$VER ]; then
        rm -rf $NAME-$VER
    fi
    tar xf ${NAME}-$VER.tar.gz
    cd ${NAME}-$VER
    dpkg-buildpackage -uc -us -rfakeroot
}

bd=$(dirname $0)
wdir=$(dirname $bd)

if [ -e ./configure.ac ]; then
    AC_INIT=$(awk -F "AC_INIT" '/AC_INIT/ {v=$2; \
        gsub("[\\(\\[\\],\\)]*","", v); \
        print v}' ./configure.ac)
    read NAME VER EMAIL<<<$AC_INIT
else
    echo "./configure.ac not found!"
    exit 1
fi

if test -n "$ghprbPullId" ; then
    _rev="pr${ghprbPullId}"
else
    BUILD_NUMBER="dev${BUILD_NUMBER:-1}"
    _rev="${BUILD_NUMBER}"
fi

if [[ -f /etc/debian_version ]]; then
    build_deb
elif [[ -f /etc/redhat-release ]] || [[ -f /etc/euleros-release ]]; then
    build_rpm
else
    echo "Not supported Linux version!"
    exit 1
fi
