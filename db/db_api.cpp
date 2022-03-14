#include "db/db_api.h"
#include "ca/MagicSingleton.h"
#include "db/cache.h"
#include "include/logging.h"
#include "utils/string_util.h"

//区块相关的接口
const std::string kBlockHash2BlockHeightKey = "blkhs2blkht_";
const std::string kBlockHeight2BlockHashKey = "blkht2blkhs_";
const std::string kBlockHash2BlcokRawKey = "blkhs2blkraw_";
const std::string kBlockTopKey = "blktop_";
const std::string kSyncBlockTopKey = "syncblktop_";
const std::string kBestChainHashKey = "bestchainhash_";
const std::string kBlockHash2BlockHeadRawKey = "blkhs2blkhdraw_";
const std::string kAddress2UtxoKey = "addr2utxo_";
const std::string kTransactionHash2TransactionRawKey = "txhs2txraw_";
const std::string kTransactionHash2BlockHashKey = "txhs2blkhs_";
const std::string kAddress2ContractTxHash = "addr2contracttxhash_";  
const std::string kContractName2Address = "contractname2addr_";
const std::string kAddress2ExecuteContractTxHash = "addr2executecontracttxhash_"; 
const std::string ContractNameAbi = "contractnameabi"; 
//交易查询相关
const std::string kAddress2TransactionRawKey = "addr2txraw_";
const std::string kAddress2BlcokHashKey = "addr2blkhs_";
const std::string kAddress2TransactionTopKey = "addr2txtop_";
const std::string kUsePledeUtxo2TxHashKey = "usepledgeutxo2txhs_";
const std::string kUseUtxo2TxHashKey = "useutxo2txhs_";

//应用层查询
const std::string kAddress2BalanceKey = "addr2bal_";
const std::string kAddress2AllTransactionKey = "addr2atx_";
const std::string kPackageFeeKey = "packagefee_";
const std::string kGasFeeKey = "gasfee_";
const std::string kOnLineTimeKey = "onlinetime_";
const std::string kPledgeKey = "pledge_";
const std::string kTransactionCountKey = "txcount_";
const std::string kGasCountKey = "gascount_";
const std::string kAwardCountKey = "awardcount_";
const std::string kGasTotalKey = "gastotal_";
const std::string kAwardTotalKey = "awardtotal_";
const std::string kAddrToGasTotalKey = "addr2gastotal_";
const std::string kAddrToAwardTotalKey = "addr2awardtotal_";
const std::string kAddrToSignNumKey = "addr2signnum_";

const std::string kInitVersionKey = "initVer_";

bool DBInit(const std::string &db_path)
{
    MagicSingleton<RocksDB>::GetInstance()->SetDBPath(db_path);
    rocksdb::Status ret_status;
    if (!MagicSingleton<RocksDB>::GetInstance()->InitDB(ret_status))
    {
        ERRORLOG("rocksdb init fail {}", ret_status.ToString());
        return false;
    }
    return true;
}
void DBDestory()
{
    MagicSingleton<RocksDB>::DesInstance();
}

DBReader::DBReader() : db_reader_(MagicSingleton<RocksDB>::GetInstance())
{
}

DBStatus DBReader::GetBlockHashesByBlockHeight(uint64_t start_height, uint64_t end_height, std::vector<std::string> &block_hashes)
{
    std::vector<std::string> keys;
    std::vector<std::string> values;
    std::vector<std::string> hashes;
    for (uint64_t index_height = start_height; index_height < end_height; ++index_height)
    {
        keys.push_back(kBlockHeight2BlockHashKey + std::to_string(index_height));
    }
    auto ret = MultiReadData(keys, values);
    if (values.empty() && DBStatus::DB_SUCCESS != ret)
    {
        return ret;
    }
    block_hashes.clear();
    for (auto &value : values)
    {
        hashes.clear();
        StringUtil::SplitString(value, hashes, "_");
        block_hashes.insert(block_hashes.end(), hashes.begin(), hashes.end());
    }
    return ret;
}
DBStatus DBReader::GetBlocksByBlockHash(const std::vector<std::string> &block_hashes, std::vector<std::string> &blocks)
{
    std::vector<std::string> keys;
    for (auto &hash : block_hashes)
    {
        keys.push_back(kBlockHash2BlcokRawKey + hash);
    }
    return MultiReadData(keys, blocks);
}
//通过块哈希获取到数据块的高度
DBStatus DBReader::GetBlockHeightByBlockHash(const std::string &blockHash, unsigned int &blockHeight)
{
    if (blockHash.empty())
    {
        return DBStatus::DB_PARAM_NULL;
    }
    std::string db_key = kBlockHash2BlockHeightKey + blockHash;
    std::string value;
    auto ret = ReadData(db_key, value);
    if (DBStatus::DB_SUCCESS == ret)
    {
        blockHeight = std::stoul(value);
    }
    return ret;
}

//通过数据块的高度获取单个块的哈希
DBStatus DBReader::GetBlockHashByBlockHeight(uint64_t blockHeight, std::string &hash)
{
    std::vector<std::string> hashes;
    auto ret = GetBlockHashsByBlockHeight(blockHeight, hashes);
    if (DBStatus::DB_SUCCESS == ret && (!hashes.empty()))
    {
        hash = hashes.at(0);
    }
    return ret;
}

//通过数据块的高度获取到多个块哈希
DBStatus DBReader::GetBlockHashsByBlockHeight(uint64_t blockHeight, std::vector<std::string> &hashes)
{
    std::string db_key = kBlockHeight2BlockHashKey + std::to_string(blockHeight);
    std::string value;
    auto ret = ReadData(db_key, value);
    if (DBStatus::DB_SUCCESS == ret)
    {
        StringUtil::SplitString(value, hashes, "_");
    }
    return ret;
}

//通过块哈希获取到数据块信息
DBStatus DBReader::GetBlockByBlockHash(const std::string &blockHash, std::string &block)
{
    if (blockHash.empty())
    {
        return DBStatus::DB_PARAM_NULL;
    }
    std::string db_key = kBlockHash2BlcokRawKey + blockHash;
    return ReadData(db_key, block);
}

DBStatus DBReader::GetBlockTop(uint64_t &blockHeight)
{
    std::string value;
    auto ret = ReadData(kBlockTopKey, value);
    if (DBStatus::DB_SUCCESS == ret)
    {
        blockHeight = std::stoul(value);
    }
    return ret;
}
//获取已经同步的高度
DBStatus DBReader::GetSyncBlockTop(uint64_t &blockHeight)
{
    std::string value;
    auto ret = ReadData(kSyncBlockTopKey, value);
    if (DBStatus::DB_SUCCESS == ret)
    {
        blockHeight = std::stoul(value);
    }
    return ret;
}

//获取到最佳链
DBStatus DBReader::GetBestChainHash(std::string &blockHash)
{
    return ReadData(kBestChainHashKey, blockHash);
}

//通过块的哈希获取到块头信息
DBStatus DBReader::GetBlockHeaderByBlockHash(const std::string &blockHash, std::string &header)
{
    std::string db_key = kBlockHash2BlockHeadRawKey + blockHash;
    return ReadData(db_key, header);
}

//通过地址获取Utxo哈希(utxohash有多个)
DBStatus DBReader::GetUtxoHashsByAddress(const std::string &address, std::vector<std::string> &utxoHashs)
{
    std::string db_key = kAddress2UtxoKey + address;
    std::string value;
    auto ret = ReadData(db_key, value);
    if (DBStatus::DB_SUCCESS == ret)
    {
        StringUtil::SplitString(value, utxoHashs, "_");
    }
    return ret;
}

//通过交易哈希获取交易原始数据
DBStatus DBReader::GetTransactionByHash(const std::string &txHash, std::string &txRaw)
{
    std::string db_key = kTransactionHash2TransactionRawKey + txHash;
    return ReadData(db_key, txRaw);
}

//通过交易哈希获取块哈希
DBStatus DBReader::GetBlockHashByTransactionHash(const std::string &txHash, std::string &blockHash)
{
    std::string db_key = kTransactionHash2BlockHashKey + txHash;
    return ReadData(db_key, blockHash);
}

//通过交易地址获取块交易
DBStatus DBReader::GetTransactionByAddress(const std::string &address, const uint32_t txNum, std::string &txRaw)
{
    std::string db_key = kAddress2TransactionRawKey + address + "_" + std::to_string(txNum);
    return ReadData(db_key, txRaw);
}

//通过交易地址获取块哈希
DBStatus DBReader::GetBlockHashByAddress(const std::string &address, const uint32_t txNum, std::string &blockHash)
{
    std::string db_key = kAddress2BlcokHashKey + address + "_" + std::to_string(txNum);
    return ReadData(db_key, blockHash);
}

//通过交易地址获取交易最高高度
DBStatus DBReader::GetTransactionTopByAddress(const std::string &address, unsigned int &txIndex)
{
    std::string db_key = kAddress2TransactionTopKey + address;
    std::string value;
    auto ret = ReadData(db_key, value);
    if (DBStatus::DB_SUCCESS == ret)
    {
        txIndex = std::stoul(value);
    }
    return ret;
}

//通过交易地址获取账号余额
DBStatus DBReader::GetBalanceByAddress(const std::string &address, int64_t &balance)
{
    std::string db_key = kAddress2BalanceKey + address;
    std::string value;
    auto ret = ReadData(db_key, value);
    if (DBStatus::DB_SUCCESS == ret)
    {
        balance = std::stol(value);
    }
    return ret;
}


//通过交易地址获取所有交易
DBStatus DBReader::GetAllTransactionByAddreess(const std::string &address, std::vector<std::string> &txHashs)
{
    std::string db_key = kAddress2AllTransactionKey + address;
    std::string value;
    auto ret = ReadData(db_key, value);
    if (DBStatus::DB_SUCCESS == ret)
    {
        StringUtil::SplitString(value, txHashs, "_");
    }
    return ret;
}

//获取设备打包费
DBStatus DBReader::GetDevicePackageFee(uint64_t &publicNodePackageFee)
{
    std::string value;
    auto ret = ReadData(kPackageFeeKey, value);
    if (DBStatus::DB_SUCCESS == ret)
    {
        publicNodePackageFee = std::stoul(value);
    }
    return ret;
}

//获取设备签名费
DBStatus DBReader::GetDeviceSignatureFee(uint64_t &mineSignatureFee)
{
    std::string value;
    auto ret = ReadData(kGasFeeKey, value);
    if (DBStatus::DB_SUCCESS == ret)
    {
        mineSignatureFee = std::stoul(value);
    }
    return ret;
}

//获取设备在线时长
DBStatus DBReader::GetDeviceOnLineTime(double &minerOnLineTime)
{
    std::string value;
    auto ret = ReadData(kOnLineTimeKey, value);
    if (DBStatus::DB_SUCCESS == ret)
    {
        minerOnLineTime = std::stod(value);
    }
    return ret;
}

//获取质押地址
DBStatus DBReader::GetPledgeAddress(std::vector<std::string> &addresses)
{
    std::string value;
    auto ret = ReadData(kPledgeKey, value);
    if (DBStatus::DB_SUCCESS == ret)
    {
        StringUtil::SplitString(value, addresses, "_");
    }
    return ret;
}

//获取质押地址的Utxo
DBStatus DBReader::GetPledgeAddressUtxo(const std::string &address, std::vector<std::string> &utxos)
{
    std::string db_key = kPledgeKey + address;
    std::string value;
    auto ret = ReadData(db_key, value);
    if (DBStatus::DB_SUCCESS == ret)
    {
        StringUtil::SplitString(value, utxos, "_");
    }
    return ret;
}

//获取交易总数
DBStatus DBReader::GetTxCount(uint64_t &count)
{
    std::string value;
    auto ret = ReadData(kTransactionCountKey, value);
    if (DBStatus::DB_SUCCESS == ret)
    {
        count = std::stoull(value);
    }
    return ret;
}

//获取总燃料费
DBStatus DBReader::GetGasCount(uint64_t &count)
{
    std::string value;
    auto ret = ReadData(kGasCountKey, value);
    if (DBStatus::DB_SUCCESS == ret)
    {
        count = std::stoull(value);
    }
    return ret;
}

//获取总签名次数
DBStatus DBReader::GetAwardCount(uint64_t &count)
{
    std::string value;
    auto ret = ReadData(kAwardCountKey, value);
    if (DBStatus::DB_SUCCESS == ret)
    {
        count = std::stoull(value);
    }
    return ret;
}

// 新增获得矿费和奖励总值接口，待完善// 未调用
DBStatus DBReader::GetGasTotal(uint64_t &gasTotal)
{
    std::string value;
    auto ret = ReadData(kGasTotalKey, value);
    if (DBStatus::DB_SUCCESS == ret)
    {
        gasTotal = std::stoull(value);
    }
    return ret;
}

DBStatus DBReader::GetAwardTotal(uint64_t &awardTotal)
{
    std::string value;
    auto ret = ReadData(kAwardTotalKey, value);
    if (DBStatus::DB_SUCCESS == ret)
    {
        awardTotal = std::stoull(value);
    }
    return ret;
}

//通过base58地址获取已获得的总手续费
DBStatus DBReader::GetGasTotalByAddress(const std::string &addr, uint64_t &gasTotal)
{
    std::string db_key = kAddrToGasTotalKey + addr;
    std::string value;
    auto ret = ReadData(db_key, value);
    if (DBStatus::DB_SUCCESS == ret)
    {
        gasTotal = std::stoull(value);
    }
    return ret;
}

//通过base58地址设置已获得的总手续费
DBStatus DBReader::GetAwardTotalByAddress(const std::string &addr, uint64_t &awardTotal)
{
    std::string db_key = kAddrToAwardTotalKey + addr;
    std::string value;
    auto ret = ReadData(db_key, value);
    if (DBStatus::DB_SUCCESS == ret)
    {
        awardTotal = std::stoull(value);
    }
    return ret;
}

//通过base58地址获取已获得的总签名次数
DBStatus DBReader::GetSignNumByAddress(const std::string &addr, uint64_t &SignNum)
{
    std::string db_key = kAddrToSignNumKey + addr;
    std::string value;
    auto ret = ReadData(db_key, value);
    if (DBStatus::DB_SUCCESS == ret)
    {
        SignNum = std::stoull(value);
    }
    return ret;
}

// 初始化数据库的程序版本
DBStatus DBReader::GetInitVer(std::string &version)
{
    return ReadData(kInitVersionKey, version);
}

DBStatus DBReader::MultiReadData(const std::vector<std::string> &keys, std::vector<std::string> &values)
{
    if (keys.empty())
    {
        return DBStatus::DB_PARAM_NULL;
    }
    auto cache = MagicSingleton<DBCache>::GetInstance();
    std::vector<std::string> cache_values;
    std::vector<rocksdb::Slice> db_keys;
    std::string value;
    for (auto key : keys)
    {
        if (cache->GetData(key, value))
        {
            cache_values.push_back(value);
        }
        else
        {
            db_keys.push_back(key);
        }
    }
    if (db_keys.empty())
    {
        values.swap(cache_values);
        return DBStatus::DB_SUCCESS;
    }
    std::vector<rocksdb::Status> ret_status;
    if (db_reader_.MultiReadData(db_keys, values, ret_status))
    {
        if (db_keys.size() != values.size())
        {
            return DBStatus::DB_ERROR;
        }
        for (size_t i = 0; i < db_keys.size(); ++i)
        {
            cache->SetData(db_keys.at(i).data(), values.at(i));
        }
        values.insert(values.end(), cache_values.begin(), cache_values.end());
        return DBStatus::DB_SUCCESS;
    }
    else
    {
        for (auto status : ret_status)
        {
            if (status.IsNotFound())
            {
                return DBStatus::DB_NOT_FOUND;
            }
        }
    }
    return DBStatus::DB_ERROR;
}

DBStatus DBReader::ReadData(const std::string &key, std::string &value)
{
    if (key.empty())
    {
        return DBStatus::DB_PARAM_NULL;
    }

    auto cache = MagicSingleton<DBCache>::GetInstance();
    if (cache->GetData(key, value))
    {
        return DBStatus::DB_SUCCESS;
    }
    rocksdb::Status ret_status;
    if (db_reader_.ReadData(key, value, ret_status))
    {
        cache->SetData(key, value);
        return DBStatus::DB_SUCCESS;
    }
    else if (ret_status.IsNotFound())
    {
        value.clear();
        return DBStatus::DB_NOT_FOUND;
    }
    return DBStatus::DB_ERROR;
}

DBReadWriter::DBReadWriter(const std::string &txn_name) : db_read_writer_(MagicSingleton<RocksDB>::GetInstance(), txn_name)
{
    auto_oper_trans = false;
    ReTransactionInit();
}

DBReadWriter::~DBReadWriter()
{
    TransactionRollBack();
}
DBStatus DBReadWriter::ReTransactionInit()
{
    auto ret = TransactionRollBack();
    if (DBStatus::DB_SUCCESS != ret)
    {
        return ret;
    }
    auto_oper_trans = true;
    if (!db_read_writer_.TransactionInit())
    {
        ERRORLOG("transction init error");
        return DBStatus::DB_ERROR;
    }
    return DBStatus::DB_SUCCESS;
}

DBStatus DBReadWriter::TransactionCommit()
{
    rocksdb::Status ret_status;
    if (db_read_writer_.TransactionCommit(ret_status))
    {
        auto_oper_trans = false;
        std::lock_guard<std::mutex> lock(key_mutex_);
        MagicSingleton<DBCache>::GetInstance()->DeleteData(delete_keys_);
        delete_keys_.clear();
        return DBStatus::DB_SUCCESS;
    }
    ERRORLOG("TransactionCommit faild:{}:{}", ret_status.code(), ret_status.ToString());
    return DBStatus::DB_ERROR;
}

DBStatus DBReadWriter::SetTxHashByUsePledeUtxoHash(const std::string &utxo_hash, const std::string &tx_hash)
{
    std::string db_key = kUsePledeUtxo2TxHashKey + utxo_hash;
    return WriteData(db_key, tx_hash);
}
//设置数据当中的交易hash
DBStatus DBReadWriter::DeleteTxHashByUsePledeUtxoHash(const std::string &utxo_hash)
{
    std::string db_key = kUsePledeUtxo2TxHashKey + utxo_hash;
    return DeleteData(db_key);
}

DBStatus DBReadWriter::SetTxHashByUseUtxoHash(const std::string &utxo_hash, const std::string &tx_hash)
{
    std::string db_key = kUseUtxo2TxHashKey + utxo_hash;
    return WriteData(db_key, tx_hash);
}
//移除数据当中的交易hash
DBStatus DBReadWriter::DeleteTxHashByUseUtxoHash(const std::string &utxo_hash)
{
    std::string db_key = kUseUtxo2TxHashKey + utxo_hash;
    return DeleteData(db_key);
}

//通过块哈希设置数据块的高度
DBStatus DBReadWriter::SetBlockHeightByBlockHash(const std::string &blockHash, const unsigned int blockHeight)
{
    std::string db_key = kBlockHash2BlockHeightKey + blockHash;
    return WriteData(db_key, std::to_string(blockHeight));
}

//通过块哈希移除数据库当中的块高度
DBStatus DBReadWriter::DeleteBlockHeightByBlockHash(const std::string &blockHash)
{
    std::string db_key = kBlockHash2BlockHeightKey + blockHash;
    return DeleteData(db_key);
}

//通过块高度设置数据块的哈希(在并发的时候同一高度可能有多个块哈希)
DBStatus DBReadWriter::SetBlockHashByBlockHeight(const unsigned int blockHeight, const std::string &blockHash, bool is_mainblock)
{
    std::string db_key = kBlockHeight2BlockHashKey + std::to_string(blockHeight);
    return MergeValue(db_key, blockHash, is_mainblock);
}

//通过块高度移除数据库里边块的哈希
DBStatus DBReadWriter::RemoveBlockHashByBlockHeight(const unsigned int blockHeight, const std::string &blockHash)
{
    std::string db_key = kBlockHeight2BlockHashKey + std::to_string(blockHeight);
    return RemoveMergeValue(db_key, blockHash);
}

//通过块哈希设置块
DBStatus DBReadWriter::SetBlockByBlockHash(const std::string &blockHash, const std::string &block)
{
    std::string db_key = kBlockHash2BlcokRawKey + blockHash;
    return WriteData(db_key, block);
}

//通过块哈希移除数据块里边的块
DBStatus DBReadWriter::DeleteBlockByBlockHash(const std::string &blockHash)
{
    std::string db_key = kBlockHash2BlcokRawKey + blockHash;
    return DeleteData(db_key);
}

//设置最高块
DBStatus DBReadWriter::SetBlockTop(const unsigned int blockHeight)
{
    return WriteData(kBlockTopKey, std::to_string(blockHeight));
}

//设置已经同步的高度
DBStatus DBReadWriter::SetSyncBlockTop(const unsigned int blockHeight)
{
    return WriteData(kSyncBlockTopKey, std::to_string(blockHeight));
}

//设置最佳链
DBStatus DBReadWriter::SetBestChainHash(const std::string &blockHash)
{
    return WriteData(kBestChainHashKey, blockHash);
}

//通过块的哈希设置块头信息
DBStatus DBReadWriter::SetBlockHeaderByBlockHash(const std::string &blockHash, const std::string &header)
{
    std::string db_key = kBlockHash2BlockHeadRawKey + blockHash;
    return WriteData(db_key, header);
}

//根据块哈希移除数据库里边的块头信息
DBStatus DBReadWriter::DeleteBlockHeaderByBlockHash(const std::string &blockHash)
{
    std::string db_key = kBlockHash2BlockHeadRawKey + blockHash;
    return DeleteData(db_key);
}

//通过地址设置Utxo哈希
DBStatus DBReadWriter::SetUtxoHashsByAddress(const std::string &address, const std::string &utxoHash)
{
    std::string db_key = kAddress2UtxoKey + address;
    return MergeValue(db_key, utxoHash);
}

//通过地址移除Utxo哈希
DBStatus DBReadWriter::RemoveUtxoHashsByAddress(const std::string &address, const std::string &utxoHash)
{
    std::string db_key = kAddress2UtxoKey + address;
    return RemoveMergeValue(db_key, utxoHash);
}

//通过交易哈希设置交易原始数据
DBStatus DBReadWriter::SetTransactionByHash(const std::string &txHash, const std::string &txRaw)
{
    std::string db_key = kTransactionHash2TransactionRawKey + txHash;
    return WriteData(db_key, txRaw);
}

//通过交易哈希移除数据库里边的交易原始数据
DBStatus DBReadWriter::DeleteTransactionByHash(const std::string &txHash)
{
    std::string db_key = kTransactionHash2TransactionRawKey + txHash;
    return DeleteData(db_key);
}

//通过交易哈希设置块哈希
DBStatus DBReadWriter::SetBlockHashByTransactionHash(const std::string &txHash, const std::string &blockHash)
{
    std::string db_key = kTransactionHash2BlockHashKey + txHash;
    return WriteData(db_key, blockHash);
}

//通过交易哈希移除数据库里边的块哈希
DBStatus DBReadWriter::DeleteBlockHashByTransactionHash(const std::string &txHash)
{
    std::string db_key = kTransactionHash2BlockHashKey + txHash;
    return DeleteData(db_key);
}

//通过交易地址设置块交易
DBStatus DBReadWriter::SetTransactionByAddress(const std::string &address, const uint32_t txNum, const std::string &txRaw)
{
    std::string db_key = kAddress2TransactionRawKey + address + "_" + std::to_string(txNum);
    return WriteData(db_key, txRaw);
}

//通过交易地址移除数据库里边的交易数据
DBStatus DBReadWriter::DeleteTransactionByAddress(const std::string &address, const uint32_t txNum)
{
    std::string db_key = kAddress2TransactionRawKey + address + "_" + std::to_string(txNum);
    return DeleteData(db_key);
}

//通过交易地址设置块哈希
DBStatus DBReadWriter::SetBlockHashByAddress(const std::string &address, const uint32_t txNum, const std::string &blockHash)
{
    std::string db_key = kAddress2BlcokHashKey + address + "_" + std::to_string(txNum);
    return WriteData(db_key, blockHash);
}

//通过交易地址移除数据库里边的块哈希
DBStatus DBReadWriter::DeleteBlockHashByAddress(const std::string &address, const uint32_t txNum)
{
    std::string db_key = kAddress2BlcokHashKey + address + "_" + std::to_string(txNum);
    return DeleteData(db_key);
}

//通过交易地址设置交易最高高度
DBStatus DBReadWriter::SetTransactionTopByAddress(const std::string &address, const unsigned int txIndex)
{
    std::string db_key = kAddress2TransactionTopKey + address;
    return WriteData(db_key, std::to_string(txIndex));
}

//通过交易地址设置账号余额
DBStatus DBReadWriter::SetBalanceByAddress(const std::string &address, int64_t balance)
{
    std::string db_key = kAddress2BalanceKey + address;
    return WriteData(db_key, std::to_string(balance));
}

DBStatus DBReadWriter::DeleteBalanceByAddress(const std::string &address)
{
    std::string db_key = kAddress2BalanceKey + address;
    return DeleteData(db_key);
}

//通过交易地址设置所有交易
DBStatus DBReadWriter::SetAllTransactionByAddress(const std::string &address, const std::string &txHash)
{
    std::string db_key = kAddress2AllTransactionKey + address;
    return MergeValue(db_key, txHash);
}

//通过交易地址移数据库里边的所有交易
DBStatus DBReadWriter::RemoveAllTransactionByAddress(const std::string &address, const std::string &txHash)
{
    std::string db_key = kAddress2AllTransactionKey + address;
    return RemoveMergeValue(db_key, txHash);
}

//设置设备打包费
DBStatus DBReadWriter::SetDevicePackageFee(const uint64_t publicNodePackageFee)
{
    return WriteData(kPackageFeeKey, std::to_string(publicNodePackageFee));
}

//设置设备签名费
DBStatus DBReadWriter::SetDeviceSignatureFee(const uint64_t mineSignatureFee)
{
    return WriteData(kGasFeeKey, std::to_string(mineSignatureFee));
}

// 矿机在线时长
DBStatus DBReadWriter::SetDeviceOnlineTime(const double minerOnLineTime)
{
    return WriteData(kOnLineTimeKey, std::to_string(minerOnLineTime));
}

//设置质押地址
DBStatus DBReadWriter::SetPledgeAddresses(const std::string &address)
{
    return MergeValue(kPledgeKey, address);
}

//移除数据库当中的质押地址
DBStatus DBReadWriter::RemovePledgeAddresses(const std::string &address)
{
    return RemoveMergeValue(kPledgeKey, address);
}

//设置滞押资产账户的utxo
DBStatus DBReadWriter::SetPledgeAddressUtxo(const std::string &address, const std::string &utxo)
{
    std::string db_key = kPledgeKey + address;
    return MergeValue(db_key, utxo);
}

// 移除数据当中的Utxo
DBStatus DBReadWriter::RemovePledgeAddressUtxo(const std::string &address, const std::string &utxos)
{
    std::string db_key = kPledgeKey + address;
    return RemoveMergeValue(db_key, utxos);
}

// 设置总交易数
DBStatus DBReadWriter::SetTxCount(uint64_t &count)
{
    return WriteData(kTransactionCountKey, std::to_string(count));
}

// 设置总燃料费
DBStatus DBReadWriter::SetGasCount(uint64_t &count)
{
    return WriteData(kGasCountKey, std::to_string(count));
}

// 设置总额外奖励费
DBStatus DBReadWriter::SetAwardCount(uint64_t &count)
{
    return WriteData(kAwardCountKey, std::to_string(count));
}

// 新增获得矿费和奖励总值接口，待完善// 未调用
DBStatus DBReadWriter::SetGasTotal(const uint64_t &gasTotal)
{
    return WriteData(kGasTotalKey, std::to_string(gasTotal));
}

DBStatus DBReadWriter::SetAwardTotal(const uint64_t &awardTotal)
{
    return WriteData(kAwardTotalKey, std::to_string(awardTotal));
}

//通过base58地址设置已获得的总手续费
DBStatus DBReadWriter::SetGasTotalByAddress(const std::string &addr, const uint64_t &gasTotal)
{
    std::string db_key = kAddrToGasTotalKey + addr;
    return WriteData(db_key, std::to_string(gasTotal));
}

//通过base58地址设置已获得的总奖励
DBStatus DBReadWriter::SetAwardTotalByAddress(const std::string &addr, const uint64_t &awardTotal)
{
    std::string db_key = kAddrToAwardTotalKey + addr;
    return WriteData(db_key, std::to_string(awardTotal));
}

//通过base58地址删除已获得的总奖励
DBStatus DBReadWriter::DeleteAwardTotalByAddress(const std::string &addr)
{
    std::string db_key = kAddrToAwardTotalKey + addr;
    return DeleteData(db_key);
}

//通过base58地址设置已获得的总签名次数
DBStatus DBReadWriter::SetSignNumByAddress(const std::string &addr, const uint64_t &SignNum)
{
    std::string db_key = kAddrToSignNumKey + addr;
    return WriteData(db_key, std::to_string(SignNum));
}

//通过base58地址删除已获得的总签名次数
DBStatus DBReadWriter::DeleteSignNumByAddress(const std::string &addr)
{
    std::string db_key = kAddrToSignNumKey + addr;
    return DeleteData(db_key);
}

// 记录初始化数据库的程序版本
DBStatus DBReadWriter::SetInitVer(const std::string &version)
{
    return WriteData(kInitVersionKey, version);
}

DBStatus DBReadWriter::TransactionRollBack()
{
    if (auto_oper_trans)
    {
        rocksdb::Status ret_status;
        if (!db_read_writer_.TransactionRollBack(ret_status))
        {
            ERRORLOG("transction rollback code:{} info:{}", ret_status.code(), ret_status.ToString());
            return DBStatus::DB_ERROR;
        }
    }
    return DBStatus::DB_SUCCESS;
}

DBStatus DBReadWriter::MultiReadData(const std::vector<std::string> &keys, std::vector<std::string> &values)
{
    if (keys.empty())
    {
        return DBStatus::DB_PARAM_NULL;
    }
    std::vector<rocksdb::Slice> db_keys;
    for (auto key : keys)
    {
        db_keys.push_back(key);
    }
    std::vector<rocksdb::Status> ret_status;
    if (db_read_writer_.MultiReadData(db_keys, values, ret_status))
    {
        if (db_keys.size() != values.size())
        {
            return DBStatus::DB_ERROR;
        }
        return DBStatus::DB_SUCCESS;
    }
    else
    {
        for (auto status : ret_status)
        {
            if (status.IsNotFound())
            {
                return DBStatus::DB_NOT_FOUND;
            }
        }
    }
    return DBStatus::DB_ERROR;
}

DBStatus DBReadWriter::ReadData(const std::string &key, std::string &value)
{
    if (key.empty())
    {
        return DBStatus::DB_PARAM_NULL;
    }
    rocksdb::Status ret_status;
    if (db_read_writer_.ReadData(key, value, ret_status))
    {
        return DBStatus::DB_SUCCESS;
    }
    else if (ret_status.IsNotFound())
    {
        value.clear();
        return DBStatus::DB_NOT_FOUND;
    }
    return DBStatus::DB_ERROR;
}
DBStatus DBReadWriter::MergeValue(const std::string &key, const std::string &value, bool first_or_last)
{
    rocksdb::Status ret_status;
    if (db_read_writer_.MergeValue(key, value, ret_status, first_or_last))
    {
        std::lock_guard<std::mutex> lock(key_mutex_);
        delete_keys_.insert(key);
        return DBStatus::DB_SUCCESS;
    }
    return DBStatus::DB_ERROR;
}
DBStatus DBReadWriter::RemoveMergeValue(const std::string &key, const std::string &value)
{
    rocksdb::Status ret_status;
    if (db_read_writer_.RemoveMergeValue(key, value, ret_status))
    {
        std::lock_guard<std::mutex> lock(key_mutex_);
        delete_keys_.insert(key);
        return DBStatus::DB_SUCCESS;
    }
    return DBStatus::DB_ERROR;
}
DBStatus DBReadWriter::WriteData(const std::string &key, const std::string &value)
{
    rocksdb::Status ret_status;
    if (db_read_writer_.WriteData(key, value, ret_status))
    {
        std::lock_guard<std::mutex> lock(key_mutex_);
        delete_keys_.insert(key);
        return DBStatus::DB_SUCCESS;
    }
    return DBStatus::DB_ERROR;
}
DBStatus DBReadWriter::DeleteData(const std::string &key)
{
    rocksdb::Status ret_status;
    if (db_read_writer_.DeleteData(key, ret_status))
    {
        std::lock_guard<std::mutex> lock(key_mutex_);
        delete_keys_.insert(key);
        return DBStatus::DB_SUCCESS;
    }
    return DBStatus::DB_ERROR;
}


 DBStatus DBReader::getKey(const std::string &key,std::string &GetValue)
 {
    std::string value;
    auto ret = ReadData(key, value);
    if (DBStatus::DB_SUCCESS == ret)
    {
        GetValue = value;
    }
   return ret;
 }

DBStatus DBReadWriter::setKey(const std::string &key, const std::string &value)
{
    return WriteData(key, value);
}

DBStatus DBReadWriter::SetDeployContractTxHashByAddress(const std::string & addr, const std::string & contract_name, const std::string txhash)
{
    if ( addr.empty() || contract_name.empty())
    {
        return DBStatus::DB_PARAM_NULL;
    }
    std::string txhash_key = kAddress2ContractTxHash  + addr  + "_" + contract_name;
    return WriteData(txhash_key,txhash);
}

DBStatus DBReader::GetDeployContractTXHashByAddress( const std::string & addr, const std::string & contract_name,std::string& txhash)
{
    if ( contract_name.empty() || addr.empty() )
    {
        return DBStatus::DB_PARAM_NULL;
    }
    std::string txhash_key = kAddress2ContractTxHash  + addr  + "_" + contract_name;
    return ReadData(txhash_key, txhash);
}

DBStatus DBReadWriter::DeleteDeployContractTxHashByAddress(const std::string & addr, const std::string & contract_name)
{
    std::string txhash_key = kAddress2ContractTxHash  + addr  + "_" + contract_name;
    return DeleteData(txhash_key);
}

DBStatus DBReadWriter::SetExecuteContractTxHashByAddress(const std::string & addr, const std::string & contract_name, const std::string txhash)
{
    if ( addr.empty() || contract_name.empty())
    {
        return DBStatus::DB_PARAM_NULL;
    }
    std::string txhash_key = kAddress2ExecuteContractTxHash  + addr  + "_" + contract_name;
    return WriteData(txhash_key,txhash);
}

DBStatus DBReader::GetExecuteContractTXHashByAddress( const std::string & addr, const std::string & contract_name,std::string& txhash)
{
    if ( contract_name.empty() || addr.empty() )
    {
        return DBStatus::DB_PARAM_NULL;
    }
    std::string txhash_key = kAddress2ContractTxHash  + addr  + "_" + contract_name;
    return ReadData(txhash_key, txhash);
}

DBStatus DBReadWriter::DeleteExecuteContractTxHashByAddress(const std::string & addr, const std::string & contract_name)
{
    if ( addr.empty() || contract_name.empty())
    {
        return DBStatus::DB_PARAM_NULL;
    }
    std::string txhash_key = kAddress2ExecuteContractTxHash  + addr  + "_" + contract_name;
    return DeleteData(txhash_key);
}

DBStatus DBReadWriter::SetAddressByContractName(const std::string & addr, const std::string & contract_name)
{
    if ( addr.empty() || contract_name.empty() )
    {
        return DBStatus::DB_PARAM_NULL;
    }
    std::string addr2contract_key = kContractName2Address  + contract_name ;
    return WriteData(addr2contract_key,addr); 
}

DBStatus DBReader::GetAddressByContractName( const std::string & contract_name, std::string & addr)
{
    if ( contract_name.empty() )
    {
        return DBStatus::DB_PARAM_NULL;
    }
    std::string addr2contract_key = kContractName2Address  + contract_name;
    return ReadData(addr2contract_key, addr);
}
DBStatus DBReadWriter::DeleteAddressByContractName( const std::string & contract_name)
{
    if (contract_name.empty())
    {
        return DBStatus::DB_PARAM_NULL;
    }
    std::string txhash_key = kAddress2ExecuteContractTxHash + contract_name;
    return DeleteData(txhash_key);
}
 DBStatus DBReadWriter::SetContractNameAbiName( const std::string & contract_name, const std::string & abi_name)
 {
    if ( contract_name.empty() || abi_name.empty() )
    {
        return DBStatus::DB_PARAM_NULL;
    }
    std::string value = contract_name + "_" + abi_name;
    return WriteData(ContractNameAbi,value); 
 }

 DBStatus DBReader::GetContractNameAbiName( std::string & value)
 {
    return ReadData(ContractNameAbi,value); 
 }

 DBStatus DBReadWriter::DeleteContractNameAbiName()
 {
    return DeleteData(ContractNameAbi);
 }