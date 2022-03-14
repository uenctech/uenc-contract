#!/bin/sh

SHDIR=$(dirname `readlink -f $0`)

name="uenc_"
gitversion=$(git rev-parse --short HEAD)
version=$(sed -n "/std::string g_LinuxCompatible = /p" ${SHDIR}/common/version.h | awk -F '[\"]' '{print $2}')

finalname=${name}""${version}

if [ ${#gitversion} -eq 0 ]
then
    echo "there is no git in your shell"
    exit
else
    finalname=${finalname}"_"${gitversion}
fi;

if [ $1 == 0 ]
then
    finalversion=${finalname}"_""primarynet"
    echo  "${finalversion}"
elif [ $1 == 1 ]
then
    finalversion=${finalname}"_""testnet"
    echo  "${finalversion}"
else
    finalversion=${finalname}"_""devnet"
    echo  "${finalversion}"
fi;

if [ -f $2/bin/uenc ]
then
    mv $2/bin/uenc $2/bin/${finalversion}
else
    echo "uenc not exist"
fi;
 
#sed -i "s/build_commit_hash.*;/build_commit_hash = \"${gitversion}\";/g" ./ca/ca_global.cpp
