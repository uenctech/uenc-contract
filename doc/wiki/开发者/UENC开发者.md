---
sidebar_position: 1
---

本章节主要介绍作为一名区块链的开发者所需要的硬件要求，开发环境及怎样获取源码编译运行。  

## 操作系统:
  * Centos7版本(CentOS是免费的、开源的、可以重新分发的开源操作系统 ，CentOS（Community Enterprise Operating System，中文意思是社区企业操作系统）是Linux发行版之一)
  * Ubuntu 20.04(一个以桌面应用为主的Linux操作系统) 是Linux发行版之一

## 开发环境：
### 1. 升级自己的gcc编译器到8.3.1：
gcc是一套由GNU工程开发的支持多种编程语言的编译器。
GCC是自由软件发展过程中的著名例子，由自由软件基金会以GPL协议发布。
GCC是大多数类Unix操作系统（如Linux、BSD、Mac OS X等）的标准的编译器，
GCC同样适用于微软的Windows。GCC支持多种计算机体系芯片，如x86、ARM，并已移植到其他多种硬件平台。
```
 yum install -y centos-release-scl scl-utils-build   
 yum install -y devtoolset-8-toolchain
 scl enable devtoolset-8 bash
```
查看是否升级成功:
```
 gcc --version
```

### 2. 安装或升级camke
UENC的编译过程依靠[cmake](https://cmake.org/)完成，请将cmake安装或升级到v1.21版本以上

下载
```
curl -O https://cmake.org/files/v3.21/cmake-3.21.4.tar.gz
tar zxvf cmake-3.21.4.tar.gz
```

安装依赖库
```
yum install -y openssl-devel
```

编译和安装
```
cd cmake-3.21.4
./configure 
make && make install
```
验证
```
cmake --version
```

### 3. git安装：  
Git 是一个开源的分布式版本控制系统，可以有效、高速地处理从很小到非常大的项目版本管理。
#### (1).删除旧版本的git  
执行:
```
yum  remove git。
```

#### (2).在线安装   
 
```
yum install git -y
```

## 获取UENC源码
```
git clone https://github.com/uenctech/uenc_src
```

##  编译运行
### 安装依赖库：
``` 
 yum install -y zlib zlib-devel  
 yum install -y unzip zip  
 yum install -y autoconf automake libtool
 
```     

### 编译命令：

编译主网程序
```
mkdir build_primary_release && cd build_primary_release
cmake .. -DPRIMARYCHAIN=ON -DCMAKE_BUILD_TYPE=Release
make
```


编译测试网程序
```
mkdir build_test_release && cd build_test_release
cmake .. -DTESTCHAIN=ON -DCMAKE_BUILD_TYPE=Release
make
```


当编译出现问题需要重新编译时：

1. 删除`protobuf`、`crypto`、`rocksdb`文件夹

2. `make clean`

3. 重新编译主网程序或测试网程序

 
### 运行时生成的文件和目录的介绍：
 运行程序  
 ```
 ./ebpc_xxxxx_testnet
 ```
 
 程序运行起来后按下`0`键退出。此时会生成 cert 目录及`config.json,data.db,devpwd.json,logs`文件或文件夹。这些目录和文件介绍如下：

  | 文件或目录 |     描述     | 
 | :--------: | :--------------: | 
 |   cert   | 存放生成的公私密钥对，后缀为".public.key"的文件是公钥文件，后缀为".private.key"为私钥文件 | 
 |   data.db   | 数据库文件 | 
 |   devpwd.json   | 本机访问密码哈希值，当移动端连接时使用该密码进行连接 | 
 |   config.json   | 配置文件 | 
 |   logs   | 日志目录 | 
 
   * 修改`config.json`
   
   当运行节点作为子网节点是无需进行配置，可按默认配置直接运行。如果是公网节点按照如下配置：
 
 1. 将is_public_node的值由false修改为true。
 2. 将server中的IP字段的值设置为自身节点所连接的其他公网节点IP地址。
 3. 将var字段下的local_ip字段设置为自身节点的外网IP地址。
 
 例如，自身节点外网IP地址为`xxx.xxx.xxx.xxx`, 所连接公网地址为`yyy.yyy.yyy.yyy`, 则按如下配置（以测试网公网节点配置为例）

 ```
"is_public_node": true,
    "server": [
        {
            "IP": "yyy.yyy.yyy.yyy",
            "PORT": 11188
        },
    ],
    "var": {
    "k_bucket": "a0dbbd80eb84b9e51f3a0d69727384c651f9bdb5",
    "k_refresh_time": 100,
    "local_ip": "xxx.xxx.xxx.xxx",
    "local_port": 11188,
    "work_thread_num": 4
    }
 ```

## 运行时参数介绍：    
程序启动时是可以带参数的，参数说明如下:  
  
|       参数 |参数说明|
|:---:|:---:|  
|--help  |获取帮助菜单|
|-m       |显示菜单   |
|-s       |设置gas费   |
|-p       |设置打包费 |  

例如设置gas费：

```
    ./uenc_1.3_testnet -s  0.015
```

:::danger
  注意：实际值 = value * 0.000001。实际值最小值：0.000001。
:::


