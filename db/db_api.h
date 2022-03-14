#ifndef UENC_DB_DB_API_H_
#define UENC_DB_DB_API_H_

#include "db/rocksdb_read.h"
#include "db/rocksdb_read_write.h"
#include "proto/block.pb.h"
#include <string>
#include <vector>

bool DBInit(const std::string &path);
void DBDestory();

enum DBStatus
{
    DB_SUCCESS = 0,    //成功
    DB_ERROR = 1,      //错误
    DB_PARAM_NULL = 2, //参数不合法
    DB_NOT_FOUND = 3,  //没找到
    DB_IS_EXIST = 4
};
class DBReader
{
public:
    DBReader();
    ~DBReader() = default;
    DBReader(DBReader &&) = delete;
    DBReader(const DBReader &) = delete;
    DBReader &operator=(DBReader &&) = delete;
    DBReader &operator=(const DBReader &) = delete;

    //根据高度区间获取hash
    DBStatus GetBlockHashesByBlockHeight(uint64_t start_height, uint64_t end_height, std::vector<std::string> &block_hashes);
    //根据hash获取区块
    DBStatus GetBlocksByBlockHash(const std::vector<std::string> &block_hashes, std::vector<std::string> &blocks);

    //通过块哈希获取到数据块的高度
    DBStatus GetBlockHeightByBlockHash(const std::string &blockHash, unsigned int &blockHeight);
    //通过数据块的高度获取单个块的哈希
    DBStatus GetBlockHashByBlockHeight(uint64_t blockHeight, std::string &hash);
    //通过数据块的高度获取到多个块哈希
    DBStatus GetBlockHashsByBlockHeight(uint64_t blockHeight, std::vector<std::string> &hashes);
    //通过块哈希获取到数据块信息
    DBStatus GetBlockByBlockHash(const std::string &blockHash, std::string &block);
    //获取最高块
    DBStatus GetBlockTop(uint64_t &blockHeight);
    //获取已经同步的高度
    DBStatus GetSyncBlockTop(uint64_t &blockHeight);
    //获取到最佳链
    DBStatus GetBestChainHash(std::string &blockHash);
    //通过块的哈希获取到块头信息
    DBStatus GetBlockHeaderByBlockHash(const std::string &blockHash, std::string &header);
    //通过地址获取Utxo哈希(utxohash有多个)
    DBStatus GetUtxoHashsByAddress(const std::string &address, std::vector<std::string> &utxoHashs);
    //通过交易哈希获取交易原始数据
    DBStatus GetTransactionByHash(const std::string &txHash, std::string &txRaw);
    //通过交易哈希获取块哈希
    DBStatus GetBlockHashByTransactionHash(const std::string &txHash, std::string &blockHash);
    //通过交易地址获取块交易
    DBStatus GetTransactionByAddress(const std::string &address, const uint32_t txNum, std::string &txRaw);
    //通过交易地址获取块哈希
    DBStatus GetBlockHashByAddress(const std::string &address, const uint32_t txNum, std::string &blockHash);
    //通过交易地址获取交易最高高度
    DBStatus GetTransactionTopByAddress(const std::string &address, unsigned int &txIndex);
    //通过交易地址获取账号余额
    DBStatus GetBalanceByAddress(const std::string &address, int64_t &balance);
    //通过交易地址获取所有交易
    DBStatus GetAllTransactionByAddreess(const std::string &address, std::vector<std::string> &txHashs);
    //获取设备打包费
    DBStatus GetDevicePackageFee(uint64_t &publicNodePackageFee);
    //获取设备签名费
    DBStatus GetDeviceSignatureFee(uint64_t &mineSignatureFee);
    //获取设备在线时长
    DBStatus GetDeviceOnLineTime(double &minerOnLineTime);
    //获取质押地址
    DBStatus GetPledgeAddress(std::vector<std::string> &addresses);
    //获取质押地址的Utxo
    DBStatus GetPledgeAddressUtxo(const std::string &address, std::vector<std::string> &utxos);
    //获取交易总数
    DBStatus GetTxCount(uint64_t &count);
    //获取总燃料费
    DBStatus GetGasCount(uint64_t &count);
    //获取总签名次数
    DBStatus GetAwardCount(uint64_t &count);
    // 新增获得矿费和奖励总值接口，待完善
    DBStatus GetGasTotal(uint64_t &gasTotal); // 未调用
    DBStatus GetAwardTotal(uint64_t &awardTotal);
    //通过base58地址获取已获得的总手续费
    DBStatus GetGasTotalByAddress(const std::string &addr, uint64_t &gasTotal);
    //通过base58地址设置已获得的总手续费
    DBStatus GetAwardTotalByAddress(const std::string &addr, uint64_t &awardTotal);
    //通过base58地址获取已获得的总签名次数
    DBStatus GetSignNumByAddress(const std::string &addr, uint64_t &SignNum);
    // 初始化数据库的程序版本
    DBStatus GetInitVer(std::string &version);

    DBStatus getKey(const std::string &key,std::string &GetValue);

    DBStatus GetDeployContractTXHashByAddress( const std::string & addr, const std::string & contract_name,std::string& txhash);
    DBStatus GetExecuteContractTXHashByAddress( const std::string & addr, const std::string & contract_name,std::string& txhash);
    DBStatus GetAddressByContractName(const std::string & contract_name, std::string & addr);
    DBStatus GetContractNameAbiName(  std::string & value);

    virtual DBStatus MultiReadData(const std::vector<std::string> &keys, std::vector<std::string> &values);
    virtual DBStatus ReadData(const std::string &key, std::string &value);

private:
    RocksDBReader db_reader_;
};

class DBReadWriter : public DBReader
{
public:
    DBReadWriter(const std::string &txn_name = std::string());
    virtual ~DBReadWriter();
    DBReadWriter(DBReadWriter &&) = delete;
    DBReadWriter(const DBReadWriter &) = delete;
    DBReadWriter &operator=(DBReadWriter &&) = delete;
    DBReadWriter &operator=(const DBReadWriter &) = delete;

    DBStatus ReTransactionInit();
    DBStatus TransactionCommit();

    //根据交易hash设置使用的质押交易utxo
    DBStatus SetTxHashByUsePledeUtxoHash(const std::string &utxo_hash, const std::string &tx_hash);
    //根据交易hash删除使用的质押交易utxo
    DBStatus DeleteTxHashByUsePledeUtxoHash(const std::string &utxo_hash);
    //根据交易hash设置使用的交易utxo
    DBStatus SetTxHashByUseUtxoHash(const std::string &utxo_hash, const std::string &tx_hash);
    //根据交易hash删除使用的交易utxo
    DBStatus DeleteTxHashByUseUtxoHash(const std::string &utxo_hash);

    //通过块哈希设置数据块的高度
    DBStatus SetBlockHeightByBlockHash(const std::string &blockHash, const unsigned int blockHeight);
    //通过块哈希移除数据库当中的块高度
    DBStatus DeleteBlockHeightByBlockHash(const std::string &blockHash);
    //通过块高度设置数据块的哈希(在并发的时候同一高度可能有多个块哈希)
    DBStatus SetBlockHashByBlockHeight(const unsigned int blockHeight, const std::string &blockHash, bool is_mainblock = false);
    //通过块高度移除数据库里边块的哈希
    DBStatus RemoveBlockHashByBlockHeight(const unsigned int blockHeight, const std::string &blockHash);
    //通过块哈希设置块
    DBStatus SetBlockByBlockHash(const std::string &blockHash, const std::string &block);
    //通过块哈希移除数据块里边的块
    DBStatus DeleteBlockByBlockHash(const std::string &blockHash);
    //设置最高块
    DBStatus SetBlockTop(const unsigned int blockHeight);
    //设置已经同步的高度
    DBStatus SetSyncBlockTop(const unsigned int blockHeight);
    //设置最佳链
    DBStatus SetBestChainHash(const std::string &blockHash);
    //通过块的哈希设置块头信息
    DBStatus SetBlockHeaderByBlockHash(const std::string &blockHash, const std::string &header);
    //根据块哈希移除数据库里边的块头信息
    DBStatus DeleteBlockHeaderByBlockHash(const std::string &blockHash);
    //通过地址设置Utxo哈希
    DBStatus SetUtxoHashsByAddress(const std::string &address, const std::string &utxoHash);
    //通过地址移除Utxo哈希
    DBStatus RemoveUtxoHashsByAddress(const std::string &address, const std::string &utxoHash);
    //通过交易哈希设置交易原始数据
    DBStatus SetTransactionByHash(const std::string &txHash, const std::string &txRaw);
    //通过交易哈希移除数据库里边的交易原始数据
    DBStatus DeleteTransactionByHash(const std::string &txHash);
    //通过交易哈希设置块哈希
    DBStatus SetBlockHashByTransactionHash(const std::string &txHash, const std::string &blockHash);
    //通过交易哈希移除数据库里边的块哈希
    DBStatus DeleteBlockHashByTransactionHash(const std::string &txHash);
    //通过交易地址设置块交易
    DBStatus SetTransactionByAddress(const std::string &address, const uint32_t txNum, const std::string &txRaw);
    //通过交易地址移除数据库里边的交易数据
    DBStatus DeleteTransactionByAddress(const std::string &address, const uint32_t txNum);
    //通过交易地址设置块哈希
    DBStatus SetBlockHashByAddress(const std::string &address, const uint32_t txNum, const std::string &blockHash);
    //通过交易地址移除数据库里边的块哈希
    DBStatus DeleteBlockHashByAddress(const std::string &address, const uint32_t txNum);
    //通过交易地址设置交易最高高度
    DBStatus SetTransactionTopByAddress(const std::string &address, const unsigned int txIndex);
    //通过交易地址设置账号余额
    DBStatus SetBalanceByAddress(const std::string &address, int64_t balance);
    //通过交易地址设置账号余额
    DBStatus DeleteBalanceByAddress(const std::string &address);
    //通过交易地址设置所有交易
    DBStatus SetAllTransactionByAddress(const std::string &address, const std::string &txHash);
    //通过交易地址移数据库里边的所有交易
    DBStatus RemoveAllTransactionByAddress(const std::string &address, const std::string &txHash);
    //设置设备打包费
    DBStatus SetDevicePackageFee(const uint64_t publicNodePackageFee);
    //设置设备签名费
    DBStatus SetDeviceSignatureFee(const uint64_t mineSignatureFee);
    // 矿机在线时长
    DBStatus SetDeviceOnlineTime(const double minerOnLineTime);
    //设置质押地址
    DBStatus SetPledgeAddresses(const std::string &address);
    //移除数据库当中的质押地址
    DBStatus RemovePledgeAddresses(const std::string &address);
    //设置滞押资产账户的utxo
    DBStatus SetPledgeAddressUtxo(const std::string &address, const std::string &utxo);
    // 移除数据当中的Utxo
    DBStatus RemovePledgeAddressUtxo(const std::string &address, const std::string &utxos);
    // 设置总交易数
    DBStatus SetTxCount(uint64_t &count);
    // 设置总燃料费
    DBStatus SetGasCount(uint64_t &count);
    // 设置总额外奖励费
    DBStatus SetAwardCount(uint64_t &count);
    // 新增获得矿费和奖励总值接口，待完善// 未调用
    DBStatus SetGasTotal(const uint64_t &gasTotal);
    DBStatus SetAwardTotal(const uint64_t &awardTotal);
    //通过base58地址设置已获得的总手续费
    DBStatus SetGasTotalByAddress(const std::string &addr, const uint64_t &gasTotal);
    //通过base58地址设置已获得的总奖励
    DBStatus SetAwardTotalByAddress(const std::string &addr, const uint64_t &awardTotal);
    //通过base58地址删除已获得的总奖励
    DBStatus DeleteAwardTotalByAddress(const std::string &addr);
    //通过base58地址设置已获得的总签名次数
    DBStatus SetSignNumByAddress(const std::string &addr, const uint64_t &SignNum);
    //通过base58地址删除已获得的总签名次数
    DBStatus DeleteSignNumByAddress(const std::string &addr);

    // 记录初始化数据库的程序版本
    DBStatus SetInitVer(const std::string &version);

    
    DBStatus setKey(const std::string &key, const std::string &value);

    std::map<std::string, std::string> prefix_key(const std::string& key);

    // 设置和获取合约
    DBStatus SetDeployContractTxHashByAddress(const std::string & addr, const std::string & contract_name, const std::string txhash);
    DBStatus DeleteDeployContractTxHashByAddress(const std::string & addr, const std::string & contract_name);

    DBStatus SetExecuteContractTxHashByAddress(const std::string & addr, const std::string & contract_name, const std::string txhash);
    DBStatus DeleteExecuteContractTxHashByAddress(const std::string & addr, const std::string & contract_name);

    DBStatus SetAddressByContractName(const std::string & addr, const std::string & contract_name);
    DBStatus DeleteAddressByContractName( const std::string & contract_name);

    
    DBStatus SetContractNameAbiName( const std::string & contract_name, const std::string & abi_name);
    DBStatus DeleteContractNameAbiName();
private:
    DBStatus TransactionRollBack();
    virtual DBStatus MultiReadData(const std::vector<std::string> &keys, std::vector<std::string> &values);
    virtual DBStatus ReadData(const std::string &key, std::string &value);
    DBStatus MergeValue(const std::string &key, const std::string &value, bool first_or_last = false);
    DBStatus RemoveMergeValue(const std::string &key, const std::string &value);
    DBStatus WriteData(const std::string &key, const std::string &value);
    DBStatus DeleteData(const std::string &key);

    std::mutex key_mutex_;
    std::set<std::string> delete_keys_;

    RocksDBReadWriter db_read_writer_;
    bool auto_oper_trans;
};

#endif
