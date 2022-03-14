#ifndef UENC_CA_SYNC_BLOCK_H_
#define UENC_CA_SYNC_BLOCK_H_

#include "db/db_api.h"
#include "net/msg_queue.h"
#include "proto/sync_block.pb.h"
#include <map>
#include "ca_blockcompare.h"
#include "utils/CTimer.hpp"

// struct CBlockCompare
// {
//     bool operator()(const CBlock &a, const CBlock &b) const;
// };

class SyncBlock
{
public:
    SyncBlock();
    ~SyncBlock() = default;
    SyncBlock(SyncBlock &&) = delete;
    SyncBlock(const SyncBlock &) = delete;
    SyncBlock &operator=(SyncBlock &&) = delete;
    SyncBlock &operator=(const SyncBlock &) = delete;

    void ThreadStart();
    bool RunFastSyncOnce(const std::vector<std::string> &pledge_addr, uint64_t chain_height, uint64_t start_sync_height, uint64_t end_sync_height);
    bool RunNewSyncOnce(const std::vector<std::string> &pledge_addr, uint64_t chain_height, uint64_t self_node_height, uint64_t start_sync_height, uint64_t end_sync_height);
    void ProcessBroadcast();
    void AddBroadcastData(const CBlock &block);
private:
    bool GetSyncNode(uint32_t num, uint64_t self_node_height, const std::vector<std::string> &pledge_addr,
                     std::vector<std::string> &send_node_ids);
    /**********************************************************************************************************************************/
    bool GetFastSyncSumHashNode(const std::vector<std::string> &send_node_ids, uint64_t start_sync_height, uint64_t end_sync_height,
                                std::string &sum_hash, std::vector<std::string> &ret_node_ids);
    bool GetFastSyncBlockData(const std::string &send_node_id, const std::string &sum_hash, uint64_t start_sync_height, uint64_t end_sync_height);

    /**********************************************************************************************************************************/
    bool GetSyncSumHashNode(const std::vector<std::string> &send_node_ids, uint64_t start_sync_height, uint64_t end_sync_height,
                            std::map<uint64_t, uint64_t> &need_sync_heights, std::vector<std::string> &ret_node_ids, uint64_t &chain_height);
    bool GetSyncBlockHashNode(const std::vector<std::string> &send_node_ids, uint64_t start_sync_height, uint64_t end_sync_height, uint64_t self_node_height, uint64_t chain_height,
                              std::vector<std::string> &ret_node_ids, std::vector<std::string> &req_hashes);
    bool GetSyncBlockData(const std::vector<std::string> &send_node_ids, const std::vector<std::string> &req_hashes);
    /**********************************************************************************************************************************/

    void AddBlockToMap(const CBlock &block, std::map<uint64_t, std::set<CBlock, CBlockCompare>> &sync_block_data);
    bool FlushBlock(const std::map<uint64_t, std::set<CBlock, CBlockCompare>> &sync_block_data, bool no_verify);

    std::thread sync_thread_;
    bool sync_thread_runing;
    std::mutex sync_thread_runing_mutex;
    std::condition_variable sync_thread_runing_condition;

    uint32_t sync_height_cnt_;
    uint32_t sync_height_time_;
    uint32_t fast_sync_height_cnt_;
    bool syncing_ ;

    CTimer broadcast_timer_;
    std::mutex broadcast_mutex_;
    std::vector<std::pair<CBlock, uint64_t>> blocks_;
    std::vector<std::string> rollback_hashs_;

};

void SendFastSyncGetHashReq(const std::string &node_id, const std::string &msg_id, uint64_t start_height, uint64_t end_height);
void SendFastSyncGetHashAck(const std::string &node_id, const std::string &msg_id, uint64_t start_height, uint64_t end_height);
void SendFastSyncGetBlockReq(const std::string &node_id, const std::string &msg_id, uint64_t start_height, uint64_t end_height);
void SendFastSyncGetBlockAck(const std::string &node_id, const std::string &msg_id, uint64_t start_height, uint64_t end_height);

int HandleFastSyncGetHashReq(const std::shared_ptr<FastSyncGetHashReq> &msg, const MsgData &msgdata);
int HandleFastSyncGetHashAck(const std::shared_ptr<FastSyncGetHashAck> &msg, const MsgData &msgdata);
int HandleFastSyncGetBlockReq(const std::shared_ptr<FastSyncGetBlockReq> &msg, const MsgData &msgdata);
int HandleFastSyncGetBlockAck(const std::shared_ptr<FastSyncGetBlockAck> &msg, const MsgData &msgdata);

void SendSyncGetSumHashReq(const std::string &node_id, const std::string &msg_id, uint64_t start_height, uint64_t end_height);
void SendSyncGetSumHashAck(const std::string &node_id, const std::string &msg_id, uint64_t start_height, uint64_t end_height);
void SendSyncGetHeightHashReq(const std::string &node_id, const std::string &msg_id, uint64_t start_height, uint64_t end_height);
void SendSyncGetHeightHashAck(const std::string &node_id, const std::string &msg_id, uint64_t start_height, uint64_t end_height);
void SendSyncGetBlockReq(const std::string &node_id, const std::string &msg_id, const std::vector<std::string> &req_hashes);
void SendSyncGetBlockAck(const std::string &node_id, const std::string &msg_id, uint64_t start_height, uint64_t end_height);

int HandleSyncGetSumHashReq(const std::shared_ptr<SyncGetSumHashReq> &msg, const MsgData &msgdata);
int HandleSyncGetSumHashAck(const std::shared_ptr<SyncGetSumHashAck> &msg, const MsgData &msgdata);
int HandleSyncGetHeightHashReq(const std::shared_ptr<SyncGetHeightHashReq> &msg, const MsgData &msgdata);
int HandleSyncGetHeightHashAck(const std::shared_ptr<SyncGetHeightHashAck> &msg, const MsgData &msgdata);
int HandleSyncGetBlockReq(const std::shared_ptr<SyncGetBlockReq> &msg, const MsgData &msgdata);
int HandleSyncGetBlockAck(const std::shared_ptr<SyncGetBlockAck> &msg, const MsgData &msgdata);
#endif
