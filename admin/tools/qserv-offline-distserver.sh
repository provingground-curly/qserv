#!/bin/sh

# First, with lsstsw tools : 
# rebuild lsst qserv qserv_testdata
# publish -t current -b bXX lsst qserv qserv_testdata

PUBLIC_HTML=/lsst/home/fjammes/public_html/qserv-offline
DISTSERVER_ROOT=/lsst/home/fjammes/src/lsstsw-offline/distserver
EUPS_PKGROOT="${DISTSERVER_ROOT}/production"

EUPS_VERSION=${EUPS_VERSION:-1.5.0}
EUPS_TARBALL="$EUPS_VERSION.tar.gz"
EUPS_TARURL="https://github.com/RobertLuptonTheGood/eups/archive/$EUPS_TARBALL"

EUPS_GITREPO="https://github.com/RobertLuptonTheGood/eups.git"


git_update_bare() {
    if [ -z "$1" ]; then
        echo "git_update_bare() requires one arguments"
        exit 1
    fi
    local giturl=$1
    local product=$(basename ${giturl})
    local retval=1

    if [ ! -d ${product} ]; then
        echo "Cloning ${giturl}"
        git clone --bare ${giturl} ||
        git remote add origin  ${giturl} && retval=0
    else
        echo "Updating ${giturl}"
        cd ${product}
        git fetch origin +refs/heads/*:refs/heads/* && retval=0
        cd .. 
    fi

    if [ ! retval ]; then 
        echo "ERROR : git update failed"
    fi  
    return ${retval}
}

if [ ! -d ${DISTSERVER_ROOT} ]; then
    mkdir ${DISTSERVER_ROOT} ||
    {
        echo "Unable to create local distribution directory : ${DISTSERVER_ROOT}"
        exit 1
    }

fi
cd ${DISTSERVER_ROOT} ||
{
    echo "Unable to go to local distribution directory directory : ${DISTSERVER_ROOT}"
    exit 1
}

echo
echo "Downloading eups tarball"
echo "========================"
echo
if [ ! -s ${EUPS_TARBALL} ]; then
    curl -L ${EUPS_TARURL} > ${EUPS_TARBALL} ||
    {
        echo "Unable to download eups tarball from ${EUPS_TARURL}"
        exit 1
    }
fi

echo
echo "Checking for distribution server data existence"
echo "==============================================="
echo
if [ ! -d ${EUPS_PKGROOT} ]; then
    echo "Directory for distribution server data doesn't exist."
    echo "Please create it using lsstsw with package mode"
    exit 1
fi

# newinstall.sh in EUPS_PKGROOT is obsolete
echo
echo "Downloading LSST stack install script"
echo "====================================="
echo
curl -O http://sw.lsstcorp.org/eupspkg/newinstall.sh
mv newinstall.sh ${EUPS_PKGROOT}


if ! git_update_bare ${EUPS_GITREPO}; then
    echo "Unable to synchronize with next git repository : ${EUPS_GITREPO}"
    exit 1
fi

EUPS_TARURL="file://${DISTSERVER_ROOT}/$EUPS_TARBALL"
EUPS_GITREPO="${DISTSERVER_ROOT}/eups.git"

TOP_DIR=$(basename ${DISTSERVER_ROOT})
tar zcvf ${PUBLIC_HTML}/qserv-offline-distserver.tar.gz -C ${DISTSERVER_ROOT}/.. ${TOP_DIR}

echo "export EUPS_PKGROOT=${EUPS_PKGROOT}"
echo "export EUPS_TARURL=${EUPS_TARURL}"
echo "export EUPS_GIT_REPO=${EUPS_GITREPO}"