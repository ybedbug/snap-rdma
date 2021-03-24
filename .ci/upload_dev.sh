#!/bin/bash -eEx

upload_deb() {

    # Import gpg key
    gpg --import ${GPG_KEY_PATH}

    shopt -s nullglob

    deb_pkgs=(${name}*${VER}-${REV}_${arch}*.deb)
    for deb_pkg in ${deb_pkgs[@]}; do
        test -e $deb_pkg
        echo "INFO: Signing package ${deb_pkg##*/}"
        dpkg-sig -k ${gpg_key_name} -s builder ${deb_pkg}
        upload_url="${REPO_URL}/${repo_name}/"
        echo "INFO: Uploading package ${deb_pkg} to ${upload_url}"
        curl -u "${REPO_USER}:${REPO_PASS}" -H "Content-Type: multipart/form-data" \
            --data-binary "@${deb_pkg}" "${upload_url}"

        if test -n "$release_dir" ; then
            upload_dir="${release_dir}/${repo_name}/${arch}/"
            mkdir -p $upload_dir
            cp -f $deb_pkg $upload_dir
        fi
    done
}

upload_rpm() {

    releasever=$(rpm --eval "%{rhel}")

    shopt -s nullglob

    rpms_location=(${HOME}/rpmbuild/RPMS/${arch}/${name}-*${VER}-${REV}*.rpm)
    for rpm_location in ${rpms_location[@]}; do
        test -f $rpm_location
        rpm_name="${rpm_location##*/}"
        upload_uri="${REPO_URL}/${repo_name}/${releasever}/${arch}/${rpm_name}"
        echo "INFO: Uploading ${rpm_name} to ${upload_uri}"
        curl --fail --user "${REPO_USER}:${REPO_PASS}" \
            --upload-file $rpm_location \
            ${upload_uri}

        if test -n "$release_dir" ; then
            upload_dir="${release_dir}/${repo_name}/${releasever}/${arch}/"
            mkdir -p $upload_dir
            cp -f $rpm_location $upload_dir
        fi
    done

    srpms_location=(${HOME}/rpmbuild/SRPMS/${name}-*${VER}-${REV}*.src.rpm)
    for srpm_location in ${srpms_location[@]}; do
        test -f $srpm_location
        srpm_name="${srpm_location##*/}"
        upload_uri="${REPO_URL}/${repo_name}/${releasever}/SRPMS/${srpm_name}"
        echo "INFO: Uploading ${srpm_name} to ${upload_uri}"
        curl --user "${REPO_USER}:${REPO_PASS}" \
            --upload-file ${srpm_location} \
            ${upload_uri}

        if test -n "$release_dir" ; then
            upload_dir="${release_dir}/${repo_name}/${releasever}/SRPMS/"
            mkdir -p $upload_dir
            cp -f $srpm_location $upload_dir
        fi
    done
}

bd=$(dirname $0)
user=${USER:-root}
: ${REPO_URL:?REPO_URL is not found!}
: ${REPO_USER:?REPO_USER is not found!}
: ${REPO_PASS:?REPO_PASS is not found!}

if [ -e ./configure.ac ]; then
    AC_INIT=$(awk -F "AC_INIT" '/AC_INIT/ {v=$2; \
        gsub("[\\(\\[\\],\\)]*","", v); \
        print v}' ./configure.ac)
    read name VER EMAIL<<<$AC_INIT
else
    echo "./configure.ac not found!"
    exit 1
fi

repo_name="${name}-${VER}"

if test -n "$ghprbPullId" ; then
    REV="pr${ghprbPullId}"
    repo_name="${repo_name}-pr"
else
    REV="dev${BUILD_NUMBER:-1}"
    repo_name="${repo_name}-dev"
fi

if [[ -f /etc/debian_version ]]; then

    codename=$(lsb_release -cs)
    repo_name="${repo_name}-${codename}-apt"
    arch=$(dpkg --print-architecture)
    : ${GPG_KEY_PATH:? GPG_KEY_PATH is not found!}
    gpg_key_name=$(echo ${GPG_KEY_PATH##*/} | cut -d . -f 1)

    apt update && apt install python3-requests -y

    # Create APT repository
    ${bd}/manage_repo.py apt --name ${repo_name} \
        --url $( echo $REPO_URL | grep -oP 'http.?://([a-z]+-?\.?)+:?\d+') \
        --user ${REPO_USER} --password ${REPO_PASS} \
        --action create --distro ${codename} \
        --keypair-file ${GPG_KEY_PATH%.*}.priv \
        --write-policy allow

    upload_deb

elif [[ -f /etc/redhat-release ]]; then

    repo_name="${repo_name}-yum"
    arch=$(uname -m)

    yum install python3-requests -y || yum install python36-requests -y

    # Create YUM repository
    ${bd}/manage_repo.py yum --name ${repo_name} \
        --url $( echo $REPO_URL | grep -oP 'http.?://([a-z]+-?\.?)+:?\d+') \
        --user ${REPO_USER} --password ${REPO_PASS} \
        --repo-data-depth 2 --write-policy allow \
        --action create

    upload_rpm

else

    echo "Not supported Linux version!"
    exit 1

fi
