#!/bin/sh

echo "-- start make depend:"

SHDIR=$(dirname `readlink -f $0`)
# 脚本的执行目录在build中，所有cd到根目录
echo "make_depend.sh execute dir:" $SHDIR

CRYPTO_DIR=./crypto
CRYPTOPP_DIR=./crypto/cryptopp/

ROCKSDB_DIR=./rocksdb

PROTOBUF_DIR=./protobuf

BOOST_DIR=./boost

LIBFMT_DIR=./libfmt
SPDLOG_DIR=./spdlog


COMPILE_NUM=`cat /proc/cpuinfo| grep  "processor" | wc -l`;

# cryptopp
cd $SHDIR
if [ -d ${CRYPTOPP_DIR} ]; 
then 
    echo "cryptopp compile";
else
    mkdir -p ${CRYPTO_DIR};
    unzip ./3rd/cryptopp-CRYPTOPP_8_2_0.zip -d ./;
    mv cryptopp-CRYPTOPP_8_2_0 cryptopp;
    mv cryptopp ${CRYPTO_DIR};
    cd ${CRYPTOPP_DIR} && make -j$COMPILE_NUM;
fi;

# rocksdb
cd $SHDIR
if [ -d ${ROCKSDB_DIR} ]; 
then 
    echo "rocksdb compile";
else
    unzip ./3rd/rocksdb-6.4.6.zip -d ./;
    mv rocksdb-6.4.6 rocksdb;
    cd ${ROCKSDB_DIR} && make static_lib -j$COMPILE_NUM;
fi;

# protobuf
cd $SHDIR
if [ -d ${PROTOBUF_DIR} ]; 
then 
    echo "protobuf compile";
else
    unzip ./3rd/protobuf-3.11.1.zip -d ./;
    mv protobuf-3.11.1 protobuf;
    cd ${PROTOBUF_DIR} && ./autogen.sh && ./configure && make -j$COMPILE_NUM;
fi;

# boost
cd $SHDIR
if [ -d ${BOOST_DIR} ]; 
then 
    echo "boost ok";
else
    tar -zxvf  ./3rd/boost-1.72.tar.gz ./;
fi;

# libfmt
cd $SHDIR
if [ -d ${LIBFMT_DIR} ]; \
then \
    echo "libfmt compile";\
else\
    tar -xvf ./3rd/libfmt-7.1.3.tar.gz ;\
    mv fmt-7.1.3 libfmt;\
    cd ${LIBFMT_DIR} && cmake . && make -j$COMPILE_NUM;\
fi;\

# spdlog
cd $SHDIR
if [ -d ${SPDLOG_DIR} ]; \
then \
    echo "spdlog compile";\
else\
    tar -xvf ./3rd/spdlog-1.8.2.tar.gz ;\
    mv spdlog-1.8.2 spdlog;\
    cd ${SPDLOG_DIR} && fmt_DIR=../libfmt cmake -DSPDLOG_FMT_EXTERNAL=yes . && make -j$COMPILE_NUM;\
fi;\

cd $1
echo "-- make depend done"




