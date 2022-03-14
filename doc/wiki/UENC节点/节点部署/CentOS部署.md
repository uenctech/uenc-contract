---
sidebar_position: 2
---

## 代码准备

:::tip tips:
1. 进行接下来的操作之前要先熟悉，学习linux系统控制台的[基础命令](https://m.runoob.com/linux/linux-command-manual.html)。
2. CentOS系统和Ubuntu系统的执行命令一样。
:::

程序的获取有两种方式：网页方式或者命令行方式。程序安装包有两种版本：主网版本和测试网版本，可以根据需求选择版本。

* 可以从github(GitHub是一个面向开源及私有软件项目的托管平台，因为只支持Git作为唯一的版本库格式进行托管，故名GitHub。)上直接[下载](https://github.com/uenctech/uenc-demo/tree/master/uenc) 适用于CentOS 7 环境下的二进制程序。  
 也可以通过以下方法下载：
 
### 网页方式 
 
 从github[网页链接](https://github.com/uenctech/uenc-demo/tree/master/uenc)上直接选择你想要的版本，然后参见命令行方式的unzip和chmod操作。

### 命令行方式
 
 下载主网程序（uenc_xxx_primarynet.zip）
 ```
  wget https://github.com/uenctech/uenc-demo/raw/master/uenc/uenc_1.6.4_primarynet.zip
  unzip uenc_1.6.4_primarynet.zip
  chmod +x uenc_1.6.4_primarynet

 ```
 下载测试网程序(uenc_xxx_testnet.zip)
 ```
  wget https://github.com/uenctech/uenc-demo/raw/master/uenc/uenc_1.6.4_testnet.zip
  unzip uenc_1.6.4_testnet.zip 
  chmod +x uenc_1.6.4_testnet
 ```

 修改安装文件的执行权限并解压缩。

## 节点配置
 
进入到uenc目录 `#cd uenc_xxx_testnet`，运行获取的安装文件
 
:::tip tips:
运行之前查看文件是否赋予可读可执行权限
:::

在运行程序之前，您需要通过修改一些文件来配置程序运行所需要的环境。

1、将程序放到一个目录下面，需要确保该目录具有读、写和执行权限及其以上。

2、这里以测试网为例，运行“获取程序”步骤后得到的二进制文件，等待数秒（例如10秒）后，按0退出当前程序。

 ```
 ./uenc_1.6.4_testnet
 ```

 ## 配置文档修改

若您的centos7系统的宿主计算机的IP地址是公网IP地址，则此计算机是公网节点；若是内网IP地址，则是内网节点。

* 验证节点

  验证节点无需配置，可按前面得到的默认配置直接运行。
 
* 公网节点

 修改配置文件`config.json`。
 
 ```
vim config.json

 ```
  
 如果要搭建**公网节点**按照如下方式进行修改：
  1. 需要将is_public_node的值由false修改为true。
  2. 将server中的IP字段的值设置为自身节点所连接的其他公网节点IP地址(**最新版本不需要此设置**)。
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
    },

 ```
 
