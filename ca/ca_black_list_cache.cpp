#include "ca/ca_black_list_cache.h"
#include "include/logging.h"
#include "utils/json.hpp"
#include "ca/ca_global.h"
#include "db/db_api.h"
#include "ca/ca_transaction.h"
#include "utils/time_util.h"
#include "ca/MagicSingleton.h"

extern int StrictCheckTransaction(const CTransaction& tx);

BlackListCache::BlackListCache()
{
    // clear
    clear_timer_.AsyncLoop(1 * 1000, [this](){
        std::vector<std::string> hashes;
        {
            std::lock_guard<std::mutex> lock(cache_mutex_);
            uint64_t now_time = Singleton<TimeUtil>::get_instance()->getlocalTimestamp();
            for(auto it = black_list_cache_.begin(); it != black_list_cache_.end(); ++it)
            {
                if(now_time > it->second.tx.time() && now_time - it->second.tx.time() >= it->second.time_out_sec)
                {
                    hashes.push_back(it->first);
                }
            }
        }
        for(auto hash : hashes)
        {
            RemoveBlack(hash);
        }
    });

    broad_cast_timer_.AsyncLoop(1 * 60 * 1000, [this](){
        TxDoubleSpendMsg msg;
        msg.set_version(getVersion());
        {
            std::lock_guard<std::mutex> lock(cache_mutex_);
            for(auto it = black_list_cache_.begin(); it != black_list_cache_.end(); ++it)
            {
                auto tx = msg.add_txs();
                *tx = it->second.tx;
            }
        }
        if(msg.txs_size() <= 0)
        {
            return;
        }
        std::vector<Node> nodeInfos = Singleton<PeerNode>::get_instance()->get_public_node();
        for(auto &node : nodeInfos)
        {
            net_send_message<TxDoubleSpendMsg>(node.base58address, msg, net_com::Compress::kCompress_False, net_com::Encrypt::kEncrypt_False, net_com::Priority::kPriority_High_1);
        }
    });
}

BlackListCache::~BlackListCache()
{
    clear_timer_.Cancel();
    broad_cast_timer_.Cancel();
}
void BlackListCache::GetCache(std::unordered_map<std::string, BlackCache> &black_cache)
{
    std::lock_guard<std::mutex> lock(cache_mutex_);
    black_cache = black_list_cache_;
}
bool BlackListCache::AddBlack(const CTransaction &tx, uint32_t time_out_sec)
{
    BlackCache black;
    black.tx = tx;
    black.time_out_sec = time_out_sec * 1000000;

    uint64_t now_time = Singleton<TimeUtil>::get_instance()->getlocalTimestamp();
    if(now_time > tx.time() && now_time - tx.time() >= black.time_out_sec)
    {
        return false;
    }

    if(!VerifyDoubleSpend(black.tx, black.base58_addr))
    {
        return false;
    }

    RemoveBlack(tx.hash());
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        black_list_cache_.insert(std::make_pair(tx.hash(), black));
        std::string last_addr;
        for (auto &vin : tx.vin())
        {
            std::string address = GetBase58Addr(vin.scriptsig().pub());
            if(last_addr != address)
            {
                base58_addrs_.insert(address);
                last_addr = address;
            }
        }
    }
    
    return true;
}

void BlackListCache::RemoveBlack(const std::string &tx_hash)
{
    std::lock_guard<std::mutex> lock(cache_mutex_);
    if(black_list_cache_.cend() != black_list_cache_.find(tx_hash))
    {
        std::string last_addr;
        for (auto &vin : black_list_cache_[tx_hash].tx.vin())
        {
            std::string address = GetBase58Addr(vin.scriptsig().pub());
            if(last_addr != address)
            {
                base58_addrs_.erase(address);
                last_addr = address;
            }
        }
        black_list_cache_.erase(tx_hash);
    }
}

bool BlackListCache::VerifyDoubleSpend(const CTransaction &tx, std::vector<std::string> &addr)
{
    addr.clear();
    if(kTransactionType_Tx != CheckTransactionType(tx))
    {
        return false;
    }
    if(0 != StrictCheckTransaction(tx))
    {
        ERRORLOG("VerifyDoubleSpend check transaction failed");
        return false;
    }
    bool is_redeem = false;
    nlohmann::json extra;
    try {
        extra = nlohmann::json::parse(tx.extra());
        std::string tx_type = extra["TransactionType"];
        if (TXTYPE_REDEEM == tx_type)
        {
            is_redeem = true;
        }
    } catch (...) {
        ERRORLOG("transaction info error");
        return false;
    }

    std::vector<std::string> addr_utxo_hashes;
    std::vector<std::string> addr_pledge_utxo_hashes;
    std::string redeem_utxo_hash;
    DBReader db_reader;
    std::string last_addr;
    for (auto &vin : tx.vin())
    {
        std::string pubKey = vin.scriptsig().pub();
        std::string address = GetBase58Addr(pubKey);
        std::string utxo_hash = vin.prevout().hash();
        // GET UTXO
        if(last_addr != address)
        {
            {
                std::lock_guard<std::mutex> lock(cache_mutex_);
                if(base58_addrs_.cend() != std::find(base58_addrs_.cbegin(), base58_addrs_.cend(), address))
                {
                    addr.push_back(address);
                    continue;
                }
            }
            addr_utxo_hashes.clear();
            auto ret = db_reader.GetUtxoHashsByAddress(address, addr_utxo_hashes);
            if(DBStatus::DB_SUCCESS != ret && DBStatus::DB_NOT_FOUND != ret)
            {
                ERRORLOG("db get addr utxo hash failed");
                return false;
            }
            if (is_redeem)
            {
                addr_pledge_utxo_hashes.clear();
                ret = db_reader.GetPledgeAddressUtxo(address, addr_pledge_utxo_hashes);
                if(DBStatus::DB_SUCCESS != ret && DBStatus::DB_NOT_FOUND != ret)
                {
                    ERRORLOG("db get addr utxo hash failed");
                    return false;
                }
                nlohmann::json txInfo = extra["TransactionInfo"].get<nlohmann::json>();
                redeem_utxo_hash = txInfo["RedeemptionUTXO"].get<std::string>();
            }
            last_addr = address;
        }
        // 1.如果是解质押交易则需要验证接质押hash是否在vin中存在
        // 2.如果不存在并且是解质押交易则在质押的utxo中查找，如果不存在则是双花
        if(is_redeem && redeem_utxo_hash == utxo_hash)
        {
            if (addr_pledge_utxo_hashes.end() == std::find(addr_pledge_utxo_hashes.begin(),
                                                           addr_pledge_utxo_hashes.end(),utxo_hash))
            {
                ERRORLOG("plede vin not found !");
                addr.push_back(address);
            }
        }
        // 在地址的普通utxo中查找，校验输入的是否不存在
        else if (addr_utxo_hashes.end() == std::find(addr_utxo_hashes.begin(), addr_utxo_hashes.end(), utxo_hash))
        {
            ERRORLOG("tx vin not found !");
            addr.push_back(address);
        }
    }
    return !addr.empty();
}
int HandleTxDoubleSpendMsg(const std::shared_ptr<TxDoubleSpendMsg> &msg, const MsgData &from)
{
    for(auto &tx : msg->txs())
    {
        MagicSingleton<BlackListCache>::GetInstance()->AddBlack(tx);
    }
    return 0;
}
