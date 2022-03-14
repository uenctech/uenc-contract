---
sidebar_position: 1
---

本指南描述了如何将 UENC的原生代币添加到某个加密货币到节点应用。

### 节点设置
我们强烈建议在高级计算机/云端设置至少两个节点实例， 立即升级到较新的版本，并随时注意和监测程序的运行情况。

这样设置可以在即使某个节点失败仍然保持您的服务可用性
关于具体要求，请参阅[硬件建议](https://www.uenc.io/devDocs/%E5%BC%80%E5%8F%91%E8%80%85/UENC%E5%BC%80%E5%8F%91%E8%80%85/#%E7%A1%AC%E4%BB%B6%E8%A6%81%E6%B1%82%EF%BC%9A)。

#### 运行一个UENC节点：

1. [安装UENC程序](https://www.uenc.io/devDocs/UENC%E8%8A%82%E7%82%B9/%E8%8A%82%E7%82%B9%E9%83%A8%E7%BD%B2/CentOS%E9%83%A8%E7%BD%B2)

2. 修改节点配置参数：找到程序里的config.json在server目录和node目录添加公网节点的IP。
    
    可选参数： 如果想在交易的时候收到转账成功的通知，只需要在`config.json`文件`http_callback`目录的path路径下加入"/add_block_callback"并加入[建块回调接口](https://www.uenc.io/devDocs/%E5%BC%80%E5%8F%91%E8%80%85/%E5%BC%80%E6%94%BE%E6%8E%A5%E5%8F%A3/jsonrpc%E6%8E%A5%E5%8F%A3/%E6%8E%A5%E5%8F%A3%E4%BB%8B%E7%BB%8D#%E5%8D%81%E4%BA%94%E3%80%81%E5%BB%BA%E5%9D%97%E5%9B%9E%E8%B0%83%E6%8E%A5%E5%8F%A3%EF%BC%88add_block_callback%EF%BC%89)。

3. 启动验证节点： 我们建议将每个节点配置退出时自动重启，以确保尽可能少地丢失数据。 

4. 设置打包费：搭建好节点之后，要想节点尽快加入的全网使用，需要设置打包费。设置完打包费用在此节点上发起的交易才可能被认为合法。打包费的设置在命令行输入 `./uenc_1.x.x_primarynet -s  <package fee>`

5. 设置签名费： 想要作为公网节点，让任何一台节点或者DAPP链接到，还需要设置签名费。签名的费用需要进入程序内部进行设置。

6. 质押UENC： 如果想你的节点参与签名挖矿，就需要质押500UENC。只有质押的节点才能被UENC主网网络承认有签名的资格。质押UENC的操作详情查看使用节点模块。
    
**新软件发布公告**

我们经常发布新软件。 有时较新的版本包含不兼容的协议调整，这时候需要及时更新软件，以避免出块产生的错误。我们发布的所以版本都是提前会在我们的[官网博客](https://www.uenc.io/#/pc/Blog)里通知

### 账本持续性
默认情况下，您的每个节点都通过可信验证节点提供的数据存储快速启动。 这个数据存储在data.DB目录里，此目录包含完整的历史帐本。 如果您的一个节点退出并且通过新的数据启动，那么该节点上的账本中可能会出现一段缺失。 为了防止该问题，需要启动等待账本同步完整在进行操作。

### 正在等待充值
   如果某个用户想UENC存入您的交易所，请指示他们发送一笔金额到相应的存款地址。

### 发送提现请求

要满足用户的提款请求, 您必须生成一笔UENC转账交易，并将其发送到API节点来扩散到全网中。

UENC的提现分为两步：1.创建交易体 2.发送交易。

UENC为了验证提现交易请求是否合法需要先把创建的交易体先发送到全网进行验证，全网根据提交的信息进行hash比对，一旦比对无误发送回hash给用户在待发送交易的时候，加入这个hash全网根据这个hash确认是否是之前提交的交易。

### 交易确认 & 最终性

UENC提供了多个交易列表接口和交易确认接口进行查询交易的完成性，还提供了消息回调，通知用户交易是否上链完成
如：使用 `confirm_transaction` JSON-RPC 接口获取交易的状态。 根据交易返回的hash进行接口调用。 如果success: 1，那么它就是已经确认。
当您使用 `get_tx_by_txid` 或 `get_tx_by_txid_and_nodeid` 请求都是根据交易hash查看当前交易的成功与否。

### 创建帐户

由于提款是不可逆过程，因此最好在提款确认之前对用户提供的帐户地址进行验证，以防止用户资产意外丢失。
UENC的普通账户地址是以1开头的34位十六进制字符串，公私钥为Base64编码字符串。
创建账户如下：
1. UENC节点在初始化启动的时候会生成一个账户。
    这个账户的密钥在程序的对应目录下，帐户不需要任何链上的初始化设置；只要有UENC余额,它们就自动出现。 您可以使用任何我们的钱包工具生成一个UENC密钥，来设置一个交易所存款帐户。

2. 也可以通过代码生成一个账户。

a. 使用protobuf接口调用如下：
在[调用静态库](https://www.uenc.io/devDocs/%E5%BC%80%E5%8F%91%E8%80%85/%E5%BC%80%E6%94%BE%E6%8E%A5%E5%8F%A3/%E8%B0%83%E7%94%A8%E9%9D%99%E6%80%81%E5%BA%93)里linux动态库里有个`String GenWallet(int a)`方法进行调用，可以得到十六进制的公私钥字符串和钱包地址。

b. 使用JsonRPC接口调用`generate_wallet`接口：

请求参数格式
```
{
  "jsonrpc": "2.0",
  "id": "1",
  "method": "generate_wallet"
}
```
示例
```
curl -i -X POST -H "Content-Type: application/json; indent=4" -d '{"jsonrpc": "2.0", "method": "generate_wallet", "id": "1" }' 192.168.1.51:11190
```
c. 如果需要生成公私钥文件则用如下方法：

需要指定创建钱包账号时生成公私钥文件的存储路径,这个路径我们建议设置为应用私有目录。通过调用[应用程序调用静态库]Path方法来设置。

```java
    /**
     * @param path 公私钥存储路径
     */
    public native void Path(String path);
```

通过调用`GenerateKey`方法来创建钱包账号,通过调用`GetMnemonic`方法来判断账号是否创建成功。`GetMnemonic`方法返回"-1"或者"-2"则创建账号失败。

*创建账号后应提示用户及时备份助记词及私钥。*

```java
    String WalletKeyPath = "";//公私钥存储路径
    JniBean jniBean = new JniBean();
    Boolean generateKey = jniBean.GenerateKey();//创建账号
    if(generateKey){
        String base58Address = jniBean.GetBase58Address();//获取钱包地址
        String mnemoiclist = jniBean.GetMnemonic(base58Address,WalletKeyPath);//获取助记词
        if (mnemoiclist.equals("-1")||mnemoiclist.equals("-2")){
            //创建账号失败
        }elese{
            //创建账号成功
        }
    }
```
建议您为每个用户配置一个独特的存款帐户。

### 助记词和私钥

#### 获取助记词

获取助记词通过调用`GetMnemonic`方法，参数为钱包地址及公私钥存储路径

```java
    String mnemoiclist = jniBean.GetMnemonic(base58Address,WalletKeyPath);
    String[] mnemoic = mnemoiclist.split("\\s+");//长度24
```

#### 获取私钥

获取私钥通过调用`GetPrivateKeyHex`方法，参数为钱包地址及公私钥存储路径

```java
	String privatestr=jniBean.GetPrivateKeyHex(base58Address,WalletKeyPath);
```


### 检查账户余额

**a.http的Json-RPC请求：**
请求参数格式
```
{
  "jsonrpc": "2.0",
  "id": "1",
  "method": "get_balance",
  "params": {
    "address": "1BuFpDmH2bJhqxQoyv8cC8YL3mU2TnUDES"
  }
}

```

示例：
```
curl -i -X POST -H "Content-Type: application/json; indent=4" -d '{"jsonrpc": "2.0", "id": "1", "method": "get_balance", "params": { "address":"1BuFpDmH2bJhqxQoyv8cC8YL3mU2TnUDES" } }' 192.168.1.51:11190
```

**b.socket的protobuf请求：**

请求
```java
	//封装请求体 base58Address为钱包地址
	final byte[] bytes = DataReqUtils.getAmount(base58Address);
```

返回
```java
	//result 为响应体byte截取结果
    BaseBean.CommonMsg commonMsg = BaseBean.CommonMsg.parseFrom(result);
    DataBean.GetAmountAck getAmountAck = DataBean.GetAmountAck.parseFrom(commonMsg.getData());
	if (getAmountAck.getCode() == 0) {
        // getAmountAck.getBalance() 获取余额
    }
```

### 获取燃料费
    
 请求
    
    ```java
    byte[] bytes = DataReqUtils.GetNodeServiceFeeReq();
    ```
    
 返回
    
    ```java
     //result 为响应体byte截取结果 
     BaseBean.CommonMsg commonMsg = BaseBean.CommonMsg.parseFrom(result);
     DataBean.GetNodeServiceFeeAck getNodeServiceFeeAck = DataBean.GetNodeServiceFeeAck.parseFrom(commonMsg.getData());
     if (getNodeServiceFeeAck.getCode() == 0) {
         //获取成功               
     }
    ```

### 充值

因为每个用户对需要在链上有一个单独的帐户，所以建议交易所提前创建批量代币帐户，并分配给各个用户。 这些账户都由交易所账号密钥所拥有。
存款交易的监控 记录充值前的余额。根据用户账户查询交易后的新记录，直到出现充值的块记录，然后查询代币账户余额更新。

### 提现

用户提供的提现地址应该是和UENC生成的地址相同。在执行提现之前，交易所应检查地址符合上文所述的规则。
确定提款地址为正确的并将转账发送到该帐户。 请注意关联的代币帐户现在还不存在，因此交易所应该代表用户为该账户提供资金。 在转账的途中要消耗一定量的手续费，这需要交易所合理从用户账户计算扣除。
用来提现的命令模板为：

**a.http的Json-RPC调用：**

#### 第一步：调用get_tx_by_txid
请求参数格式：
```
{
    "jsonrpc": "2.0",
    "id": "1",
    "method": "create_tx_message",
    "params": {
        "from_addr": ["1BuFpDmH2bJhqxQoyv8cC8YL3mU2TnUDES"],
        "to_addr": [{"addr": "1FoQKZdUNeBXV2nTba6e354m5JrQ4rHYgA", "value": "22.222222"}],
        "fee": "0.555555"
    }
}

```
示例：
```
curl -i -X POST -H "Content-Type: application/json; indent=4" -d '{"jsonrpc": "2.0", "id": "1", "method": "create_tx_message", "params": { "from_addr": ["1BuFpDmH2bJhqxQoyv8cC8YL3mU2TnUDES"], "to_addr": [{"addr": "1FoQKZdUNeBXV2nTba6e354m5JrQ4rHYgA", "value": "22.222222"}], "fee": "0.555555"} }' 192.168.1.51:11190
```

#### 第二步：调用send_tx
请求参数格式：
```
{
    "jsonrpc": "2.0",
    "id": "1",
    "method": "send_tx",
    "params": {
        "tx_data": "ELvdqOvRuOwCIiIxQnVGcERtSDJiSmhxeFFveXY4Y0M4WUwzbVUyVG5VREVTMig4ZjU1M2U5ODA4MzM4MjZhMDIxYWQ5MTU4MDA5N2E5OGVkY2EzM2M3QkQKQgpAMjRkMjUxMzMxZGFkYjEyMGMyYmYxMDlhZDI2ODllOWNkMDcwYTAyZWJkZWQxNDA1ZTM5MGFlMmVhMDI0YjEzMEopCI6rzAoSIjFGb1FLWmRVTmVCWFYyblRiYTZlMzU0bTVKclE0ckhZZ0FKKgiwua+GAxIiMUJ1RnBEbUgyYkpocXhRb3l2OGNDOFlMM21VMlRuVURFU1JDeyJHYXNGZWUiOjU1NTU1NSwiTmVlZFZlcmlmeVByZUhhc2hDb3VudCI6MywiVHJhbnNhY3Rpb25UeXBlIjoidHgifQ==",
        "tx_signature": "N1ii0dikr0NJRvi7GXkjXOayD+mVcMfXF+49iOmOneYqYj2HHYzNm3Txj/otW/K7Dh3uBJ2Gb4nlTJW2AY3Dog==",
        "public_key": "ICBszM0aHCpWmDdEC3GMBL6DFN7XdWzijF33uvmWKMa1WbvWBk33+G9E4pSztJWlwDkvEt4dW4oGY8/sY2FJBtPG",
        "tx_encode_hash": "b3b8f15852efddbdfe8aa759a2f026488350b6f56a4cae7494ea3cbba0f8a5c5"
    }
}

```
示例：
```
curl -i -X POST -H "Content-Type: application/json; indent=4" -d '{"jsonrpc": "2.0", "id": "1", "method": "create_tx_message", "params": { "from_addr": ["1BuFpDmH2bJhqxQoyv8cC8YL3mU2TnUDES"], "to_addr": [{"addr": "1FoQKZdUNeBXV2nTba6e354m5JrQ4rHYgA", "value": "22.222222"}], "fee": "0.555555"} }' 192.168.1.51:11190
```

**b.socket的protobuf接口调用：**

转账时需要传递单节点签名费等信息,单节点签名费范围为**获取燃料费**请求返回的`min_fee`、`max_fee`字段值，默认为`service_fee`字段值，节点签名共识数范围为6至15；关于转账交易相关错误Code返回值参考交易相关错误返回值文件

#### 第一步

请求

```java

public class TransListBean implements Serializable {
    String adress; //目标地址
    String price; //转账金额
    public TransListBean() {
        super();
    }
    public String getAdress() {
        return adress;
    }
    public void setAdress(String adress) {
        this.adress = adress;
    }
    public String getPrice() {
        return price;
    }
    public void setPrice(String price) {
        this.price = price;
    }
}
 /**
     * 主网发起交易 第一步
     * @param fromAddress 转账地址
     * @param listBeans 转账目标地址List<TransListBean> ,长度最大为5，也就是一次请求最多可以向五个地址转账
     * @param formatfee 单节点签名 格式化为小数点后六位0.000000
     * @param minfeenum 共识数
  */
 byte[] bytes = DataReqUtils.PhoneCreateMultiTxMsgReq(fromAddress, listBeans, formatfee, minfeenum);

```

返回

```java

//result 为响应体byte截取结果
BaseBean.CommonMsg commonMsg = BaseBean.CommonMsg.parseFrom(result);
DataBean.CreateMultiTxMsgAck phoneCreateTxMsgAck = DataBean.CreateMultiTxMsgAck.parseFrom(commonMsg.getData());
if (phoneCreateTxMsgAck.getCode() == 0) {
    String txData = phoneCreateTxMsgAck.getTxData();
    String sha256 = phoneCreateTxMsgAck.getTxEncodeHash();
}

```

#### 第二步

请求

txData及sha256为第一步返回的值

```java
boolean setKeyState = jniBean.SetKey(fromAddress,WalletKeyPath); //调用动态库进行交易签名
String publicKeybase64 = Base64.encodeToString(sha256.getBytes(), Base64.NO_WRAP);
if (setKeyState) {
    byte[] byteNetMessage = jniBean.NetMessageReqTxRaw(publicKeybase64.getBytes());
    byte[] bytesLength = ArrayUtil.subByte(byteNetMessage, 0, 4);
    int i = ArrayUtil.toInt(bytesLength);
    byte[] bytesSignature = ArrayUtil.subByte(byteNetMessage, 4, i);
    byte[] bytesToStrpub = ArrayUtil.subByte(byteNetMessage, 4 + i, byteNetMessage.length - i - 4);
	byte[] bytes = DataReqUtils.PhoneMultiTxMsgReq(txData,new String(bytesSignature), new String(bytesToStrpub), sha256);
}
```

返回

```java

//result 为响应体byte截取结果  
BaseBean.CommonMsg commonMsg = BaseBean.CommonMsg.parseFrom(result);
DataBean.TxMsgAck phoneTxMsgAck = DataBean.TxMsgAck.parseFrom(commonMsg.getData());
if (phoneTxMsgAck.getCode() == 0) {
    //转账请求发送成功
}

```

### 获取交易列表

#### 交易成功列表

请求

分页请求 count 为每页数量，hash首次请求为空字符串

```java
byte[] bytes = DataReqUtils.GetTxInfoListReq(base58Address, hash, count);
```

返回

```java
//result 为响应体byte截取结果  
BaseBean.CommonMsg commonMsg = BaseBean.CommonMsg.parseFrom(result);
DataBean.GetTxInfoListAck getBlockInfoAck = DataBean.GetTxInfoListAck.parseFrom(commonMsg.getData());
if (getBlockInfoAck.getCode() == 0) {
	getBlockInfoAck.getListList();//返回列表数据
    getBlockInfoAck.getLasthash();//下一次请求hash值，或者取列表数据最后一条hash值
}
```

#### 交易中列表

请求

```java
byte[] bytes = DataReqUtils.GetTxPendingListReq(base58Address);
```

返回

```java
 BaseBean.CommonMsg commonMsg = BaseBean.CommonMsg.parseFrom(result);
 DataBean.GetTxPendingListAck getBlockInfoAck = DataBean.GetTxPendingListAck.parseFrom(commonMsg.getData());
 if (getBlockInfoAck.getCode() == 0) {
 	getBlockInfoAck.getListList();//返回列表数据
 }
```

#### 交易失败列表

请求

分页请求 count 为每页数量，hash首次请求为空字符串

```java
byte[] bytes = DataReqUtils.GetTxFailureListReq(base58Address, hash, count);
```

返回

```java
BaseBean.CommonMsg commonMsg = BaseBean.CommonMsg.parseFrom(data);
DataBean.GetTxFailureListAck getBlockInfoAck = DataBean.GetTxFailureListAck.parseFrom(commonMsg.getData());
if (getBlockInfoAck.getCode() == 0) {
    getBlockInfoAck.getListList();//返回列表数据
    getBlockInfoAck.getLasthash();//下一次请求hash值，或者取列表数据最后一条hash值
}
```

### 测试集成
请务必下载测试包进行测试，或者正式包更改配置文件,在自己节点上测试完整的工作流，在进行稳定运行。
UENC测试网有一个水龙头，您可以通过[注册申请](http://www.uenc.net.cn:9000/#/index)获取一些用来开发和测试的UENC代币。参与我们测试网的测试还会获得实质的UENC代币奖励，具体奖励规则详见水龙头测试页面
