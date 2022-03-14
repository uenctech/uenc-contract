---
sidebar_position: 3
---

## 代码准备

 * 可以从github(GitHub是一个面向开源及私有软件项目的托管平台，因为只支持Git作为唯一的版本库格式进行托管，故名GitHub。)上直接[下载](https://github.com/uenctech/uenc-demo/tree/master/uenc) 适用于CentOS 7 环境下的二进制程序。  
 也可以通过以下方法下载：
 
### 1.1网页方式 
 
 从github[网页链接](https://github.com/uenctech/uenc-demo/tree/master/uenc)上直接选择你想要的版本，然后参见[命令行方式](#command)的unzip和chmod操作。

### 1.2命令行方式
 
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
 下载后修改下载的安装文件的执行权限并解压缩。
 

## 节点配置
进入到uenc目录 `#cd uenc_xxx_test`
 * 运行获取的安装文件
 
:::tip tips:
运行之前查看文件是否赋予可读可执行权限
:::

 ```
 ./uenc_1.6.4_testnet
 ```

 * 执行后按`Ctrl + C`退出当前程序，此时会在当前目录中生成如下文件和目录
 
 ## 配置文档修改

 修改配置文件`config.json`，自己是子网节点无需配置，可按默认配置直接运行。
 
 ```
    vim config.json

 ```

编辑完成，进行退出。重新执行`./uenc_xxx_testnet` 命令，执行生效。

 如果自己是公网节点按照如下方式进行修改：
 1. 需要将is_public_node的值由false修改为true。
 2. 将var字段下的local_ip字段设置为自身节点的外网IP地址。

 例如，自身节点外网IP地址为xxx.xxx.xxx.xxx, 所连接公网地址为yyy.yyy.yyy.yyy, 则按如下配置（以测试网公网节点配置为例）
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
 