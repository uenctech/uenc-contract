#ifndef UENC_CA_BLACK_LIST_CACHE_H_
#define UENC_CA_BLACK_LIST_CACHE_H_

#include "proto/transaction.pb.h"
#include "utils/CTimer.hpp"
#include <mutex>
#include <string>
#include <unordered_map>
#include "proto/ca_protomsg.pb.h"
#include "net/msg_queue.h"

struct BlackCache
{
    CTransaction tx;
    uint32_t time_out_sec;
    std::vector<std::string> base58_addr;
};

class BlackListCache
{
public:
    BlackListCache();
    ~BlackListCache();
    BlackListCache(BlackListCache &&) = delete;
    BlackListCache(const BlackListCache &) = delete;
    BlackListCache &operator=(BlackListCache &&) = delete;
    BlackListCache &operator=(const BlackListCache &) = delete;

    void GetCache(std::unordered_map<std::string, BlackCache> &black_list_cache);
    bool AddBlack(const CTransaction &tx, uint32_t time_out_sec = 600);
    void RemoveBlack(const std::string &tx_hash);

private:
    bool VerifyDoubleSpend(const CTransaction &tx, std::vector<std::string> &addr);

    CTimer clear_timer_;
    CTimer broad_cast_timer_;
    std::mutex cache_mutex_;
    std::unordered_map<std::string, BlackCache> black_list_cache_;
    std::set<std::string> base58_addrs_;
};
int HandleTxDoubleSpendMsg(const std::shared_ptr<TxDoubleSpendMsg> &msg, const MsgData &from);

#endif
