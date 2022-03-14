// Create: timer for confirmed the transaction, 20210310   LiuMing

#ifndef __CA_TX_CONFIRM_TIMER_H__
#define __CA_TX_CONFIRM_TIMER_H__

#include "../utils/CTimer.hpp"
#include "ca_txvincache.h"
#include "proto/block.pb.h"
#include <vector>
#include <mutex>
#include <string>

using namespace std;

struct TxConfirmation
{
    TxVinCache::Tx tx;
    uint64_t startstamp;
    std::vector<std::string>ids;
    int count = 0; // success count
    int failed_count = 0; // failed count
    int total = 0; // send total
    CBlock block;

    static const int DEFAULT_CONFIRM_TOTAL;
    bool is_confirm_ok();
    bool is_success();
    float get_success_rate();
    int get_success_count();
};

class TransactionConfirmTimer
{
public:
    TransactionConfirmTimer();
    ~TransactionConfirmTimer() = default;

    bool is_confirm_ok(const string& tx_hash);
    bool is_success(const string& tx_hash);
    float get_success_rate(const string& tx_hash);
    
    void add(TxVinCache::Tx& tx, int total = TxConfirmation::DEFAULT_CONFIRM_TOTAL);
    void add(const string& tx_hash, int total = TxConfirmation::DEFAULT_CONFIRM_TOTAL);
    bool remove(const string& tx_hash);
    int get_count(const string& tx_hash);
    void update_count(const string& tx_hash, CBlock& block);
    int get_failed_count(const string& tx_hash);
    void update_failed_count(const string& tx_hash);

    void confirm();
    void timer_start();

    void get_ids(const string& tx_hash,std::vector<std::string>&ids);
    void update_id(const string& tx_hash,std::string&ids );
    bool is_not_exist_id(const string& tx_hash, const string& id);
    static void timer_process(TransactionConfirmTimer* timer);

private:
    std::vector<TxConfirmation> tx_confirmation_;
    std::mutex mutex_;
    
    CTimer timer_;
};

#endif // __CA_TX_CONFIRM_TIMER_H__