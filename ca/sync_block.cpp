#include "ca/sync_block.h"
#include "ca/MagicSingleton.h"
#include "ca/ca_algorithm.h"
#include "ca/ca_rollback.h"
#include "ca/ca_synchronization.h"
#include "ca/ca_transaction.h"
#include "ca/ca_txhelper.h"
#include "common/global_data.h"
#include "db/db_api.h"
#include "net/node_cache.h"
#include "ca/ca_block_http_callback.h"

extern int VerifyBuildBlock(const CBlock & cblock);
static bool SumHeightHash(std::vector<std::string> &block_hashes, std::string &hash)
{
    std::sort(block_hashes.begin(), block_hashes.end());
    hash = getsha256hash(StringUtil::concat(block_hashes, ""));
    return true;
}

static bool GetHeightBlockHash(uint64_t start_height, uint64_t end_height, std::vector<std::string> &block_hashes)
{
    if (DBStatus::DB_SUCCESS != DBReader().GetBlockHashesByBlockHeight(start_height, end_height, block_hashes))
    {
        return false;
    }
    return true;
}


SyncBlock::SyncBlock()
{
    // broadcast_timer_.AsyncLoop(1000, [this](){
    //     if(syncing_)
    //     {
    //         return;
    //     }
    //     ProcessBroadcast();
    // });
}

void SyncBlock::ThreadStart()
{
    sync_height_time_ = Singleton<Config>::get_instance()->GetSyncDataPollTime();
    sync_height_cnt_ = Singleton<Config>::get_instance()->GetSyncDataCount();
    if(sync_height_cnt_ > 300)
    {
        sync_height_cnt_ = 300;
    }
    fast_sync_height_cnt_ = Singleton<Config>::get_instance()->GetSyncDataCount();
    if(fast_sync_height_cnt_ > 500)
    {
        fast_sync_height_cnt_ = 500;
    }
    sync_thread_runing = true;
    sync_thread_ = std::thread(
        [this]()
        {
            uint32_t sleep_time = sync_height_time_;
            while (sync_thread_runing)
            {
                syncing_ = false;
                std::unique_lock<std::mutex> block_thread_runing_locker(sync_thread_runing_mutex);
                sync_thread_runing_condition.wait_for(block_thread_runing_locker, std::chrono::seconds(sleep_time));
                sync_thread_runing_mutex.unlock();
                if (!sync_thread_runing)
                {
                    break;
                }
                syncing_ = true;
                rollback_hashs_.clear();
                uint64_t chain_height = 0;
                {
                    std::vector<Node> nodes;
                    auto peer_node = Singleton<PeerNode>::get_instance();
                    if (peer_node->get_self_node().is_public_node)
                    {
                        nodes = peer_node->get_nodelist();
                    }
                    else
                    {
                        nodes = Singleton<NodeCache>::get_instance()->get_nodelist();
                    }
                    if (nodes.empty())
                    {
                        continue;
                    }
                    std::vector<uint64_t> node_heights;
                    for (auto &node : nodes)
                    {
                        node_heights.push_back(node.height);
                    }
                    std::sort(node_heights.begin(), node_heights.end());
                    int verify_num = node_heights.size() * 0.75;
                    if (verify_num >= node_heights.size())
                    {
                        ERRORLOG("get chain height error index:{}:{}", verify_num, node_heights.size());
                        continue;
                    }
                    chain_height = node_heights.at(verify_num);
                }
                uint64_t self_node_height = 0;
                std::vector<std::string> pledge_addr;
                {
                    DBReader db_reader;
                    auto status = db_reader.GetBlockTop(self_node_height);
                    if (DBStatus::DB_SUCCESS != status)
                    {
                        continue;
                    }
                    status = db_reader.GetPledgeAddress(pledge_addr);
                    if (DBStatus::DB_SUCCESS != status && DBStatus::DB_NOT_FOUND != status)
                    {
                        continue;
                    }
                }
                uint64_t start_sync_height = 0;
                uint64_t end_sync_height = 0;
                if (chain_height - self_node_height <= 600)
                {
                    if (self_node_height > 500)
                    {
                        start_sync_height = self_node_height - 500;
                    }
                    else
                    {
                        start_sync_height = 1;
                    }
                    end_sync_height = self_node_height + sync_height_cnt_;
                    sleep_time = sync_height_time_;
                    INFOLOG("begin new sync {} {} ", start_sync_height, end_sync_height);
                    RunNewSyncOnce(pledge_addr, chain_height, self_node_height, start_sync_height, end_sync_height);
                }
                else
                {
                    if (self_node_height > 1)
                    {
                        start_sync_height = self_node_height;
                    }
                    else
                    {
                        start_sync_height = 1;
                    }
                    uint64_t end_sync_height = self_node_height + fast_sync_height_cnt_;
                    if (end_sync_height >= (chain_height - 500))
                    {
                        end_sync_height = chain_height - 500;
                    }

                    INFOLOG("begin fast sync {} {} ", start_sync_height, end_sync_height);
                    if(!RunFastSyncOnce(pledge_addr, chain_height, start_sync_height, end_sync_height))
                    {
                        sleep_time = 1;
                    }
                    else
                    {
                        sleep_time = 10;
                    }
                }
            }
        });
    sync_thread_.detach();
}

bool SyncBlock::RunFastSyncOnce(const std::vector<std::string> &pledge_addr, uint64_t chain_height, uint64_t start_sync_height, uint64_t end_sync_height)
{
    if (start_sync_height > end_sync_height)
    {
        return false;
    }
    std::vector<std::string> send_node_ids;
    if (!GetSyncNode(10, chain_height, pledge_addr, send_node_ids))
    {
        ERRORLOG("get sync node fail");
        return false;
    }
    std::vector<std::string> ret_node_ids;
    std::string sum_hash;

    if( start_sync_height > 1)
    {
        uint64_t start_height = 1;
        if( start_sync_height > 500)
        {
            start_height = start_sync_height - 500;
        }
        INFOLOG("begin GetFastSyncSumHashNode1 {} {} ", start_height, start_sync_height);
        if (!GetFastSyncSumHashNode(send_node_ids, start_height, start_sync_height, sum_hash, ret_node_ids))
        {
            ERRORLOG("get sync sum hash fail");
            return false;
        }
        std::string hash;
        std::vector<std::string> block_hashes;
        if (GetHeightBlockHash(start_height, start_sync_height, block_hashes) && SumHeightHash(block_hashes, hash))
        {
            if(sum_hash != hash)
            {
                start_sync_height = start_height;
            }
        }
        else
        {
            start_sync_height = start_height;
        }
        ret_node_ids.clear();
        sum_hash.clear();
    }

    INFOLOG("begin GetFastSyncSumHashNode {} {} ", start_sync_height, end_sync_height);
    if (!GetFastSyncSumHashNode(send_node_ids, start_sync_height, end_sync_height, sum_hash, ret_node_ids))
    {
        ERRORLOG("get sync sum hash fail");
        return false;
    }
    bool flag = false;
    for (auto &node_id : ret_node_ids)
    {
        DEBUGLOG("fast sync block from {}", node_id);
        if (GetFastSyncBlockData(node_id, sum_hash, start_sync_height, end_sync_height))
        {
            flag = true;
            break;
        }
    }
    return flag;
}

bool SyncBlock::RunNewSyncOnce(const std::vector<std::string> &pledge_addr, uint64_t chain_height, uint64_t self_node_height, uint64_t start_sync_height, uint64_t end_sync_height)
{
    if (start_sync_height > end_sync_height)
    {
        return false;
    }
    std::vector<std::string> send_node_ids;
    if (!GetSyncNode(10, chain_height, pledge_addr, send_node_ids))
    {
        ERRORLOG("get sync node fail");
        return false;
    }
    std::map<uint64_t, uint64_t> need_sync_heights;
    std::vector<std::string> ret_node_ids;
    chain_height = 0;
    DEBUGLOG("GetSyncSumHashNode begin:{}:{}", start_sync_height, end_sync_height);
    if (!GetSyncSumHashNode(send_node_ids, start_sync_height, end_sync_height, need_sync_heights, ret_node_ids, chain_height))
    {
        ERRORLOG("get sync sum hash fail");
        return false;
    }
    DEBUGLOG("GetSyncSumHashNode success:{}:{}", start_sync_height, end_sync_height);
    std::vector<std::string> sec_ret_node_ids;
    std::vector<std::string> req_hashes;
    for (auto sync_height : need_sync_heights)
    {
        sec_ret_node_ids.clear();
        req_hashes.clear();
        DEBUGLOG("GetSyncBlockHashNode begin:{}:{}", sync_height.first, sync_height.second);
        if (!GetSyncBlockHashNode(ret_node_ids, sync_height.first, sync_height.second, self_node_height, chain_height, sec_ret_node_ids, req_hashes))
        {
            return false;
        }
        DEBUGLOG("GetSyncBlockHashNode success:{}:{}", sync_height.first, sync_height.second);
        DEBUGLOG("GetSyncBlockData begin:{}", req_hashes.size());
        if (!GetSyncBlockData(sec_ret_node_ids, req_hashes))
        {
            return false;
        }
        DEBUGLOG("GetSyncBlockData success:{}", req_hashes.size());
    }

    return true;
}
void SyncBlock::ProcessBroadcast()
{
    uint64_t height = 0;
    {
        std::lock_guard<std::mutex> lock(broadcast_mutex_);
        auto now = time(NULL);
        for (auto it = blocks_.begin(); it != blocks_.end(); )
        {
            if(now - it->second >= PROCESS_TIME)
            {
                if(height < it->first.height())
                {
                    height = it->first.height();
                }
            }
            else
            {
                ++it;
            }
        }
    }
    uint64_t self_node_height = 0;
    auto status = DBReader().GetBlockTop(self_node_height);
    if (DBStatus::DB_SUCCESS != status)
    {
        return;
    }
    if(height >= (self_node_height+1))
    {
        return;
    }
    std::map<uint64_t, std::set<CBlock, CBlockCompare>> broadcast_block_data;
    {
        std::lock_guard<std::mutex> lock(broadcast_mutex_);
        auto now = time(NULL);
        for (auto it = blocks_.begin(); it != blocks_.end(); )
        {
            if(now - it->second >= PROCESS_TIME)
            {
                AddBlockToMap(it->first, broadcast_block_data);
                it = blocks_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
    FlushBlock(broadcast_block_data, false);
}

void SyncBlock::AddBroadcastData(const CBlock &block)
{
    uint64_t self_node_height = 0;
    auto status = DBReader().GetBlockTop(self_node_height);
    if (DBStatus::DB_SUCCESS != status)
    {
        return;
    }
    if(block.height() >= (self_node_height+10))
    {
        return;
    }
    std::lock_guard<std::mutex> lock(broadcast_mutex_);
    //检查utxo是否有冲突
    for (auto it = blocks_.begin(); it != blocks_.end(); ++it)
    {
        bool ret = MagicSingleton<BlockPoll>::GetInstance()->CheckConflict(it->first, block);
        if(ret)   //有冲突
        {
            ERRORLOG("BlockPoll::Add====has conflict");
            if(it->first.time() > block.time())
            {
                it = blocks_.erase(it);
                blocks_.push_back(std::make_pair(block, time(NULL)));
                break;
            }
        }
    }
}

bool SyncBlock::GetSyncNode(uint32_t num, uint64_t self_node_height, const std::vector<std::string> &pledge_addr,
                            std::vector<std::string> &send_node_ids)
{
    std::vector<Node> nodes;
    auto peer_node = Singleton<PeerNode>::get_instance();
    if (peer_node->get_self_node().is_public_node)
    {
        nodes = peer_node->get_nodelist();
    }
    else
    {
        nodes = Singleton<NodeCache>::get_instance()->get_nodelist();
    }
    if (nodes.empty())
    {
        ERRORLOG("node is empty");
        return false;
    }
    if (nodes.size() <= num)
    {
        for (auto &node : nodes)
        {
            if (node.height < self_node_height)
            {
                continue;
            }
            send_node_ids.push_back(node.base58address);
        }
    }
    else
    {
        std::vector<Node> node_info = nodes;
        if(pledge_addr.size() < 10)
        {
            for (; send_node_ids.size() < num && (!node_info.empty());)
            {
                int index = rand() % node_info.size();
                auto &node = node_info.at(index);
                if (node.height < self_node_height)
                {
                    continue;
                }
                send_node_ids.push_back(node_info[index].base58address);
                node_info.erase(node_info.cbegin() + index);
            }
        }
        else
        {
            std::vector<std::string> pledge = pledge_addr;
            for (; !pledge.empty() && send_node_ids.size() < num && (!node_info.empty());)
            {
                int index = rand() % node_info.size();
                auto &node = node_info.at(index);
                if (node.height < self_node_height)
                {
                    node_info.erase(node_info.cbegin() + index);
                    continue;
                }
                auto it = std::find(pledge.cbegin(), pledge.cend(), node.base58address);
                if (pledge.cend() == it)
                {
                    node_info.erase(node_info.cbegin() + index);
                    continue;
                }
                pledge.erase(it);
                send_node_ids.push_back(node_info[index].base58address);
                node_info.erase(node_info.cbegin() + index);
            }
            for (; send_node_ids.size() < num && (!nodes.empty());)
            {
                int index = rand() % nodes.size();
                auto &node = nodes.at(index);
                if (node.height < self_node_height)
                {
                    nodes.erase(nodes.cbegin() + index);
                    continue;
                }
                auto it = std::find(send_node_ids.cbegin(), send_node_ids.cend(), node.base58address);
                if (send_node_ids.cend() != it)
                {
                    nodes.erase(nodes.cbegin() + index);
                    continue;
                }
                send_node_ids.push_back(nodes[index].base58address);
                nodes.erase(nodes.cbegin() + index);
            }
        }
    }
    if (send_node_ids.size() < 5)
    {
        send_node_ids.clear();
        return false;
    }
    return true;
}

bool SyncBlock::GetFastSyncSumHashNode(const std::vector<std::string> &send_node_ids, uint64_t start_sync_height, uint64_t end_sync_height,
                                       std::string &sum_hash, std::vector<std::string> &ret_node_ids)
{
    sum_hash.clear();
    ret_node_ids.clear();
    std::string msg_id;
    size_t send_num = send_node_ids.size();
    if (!GLOBALDATAMGRPTR.CreateWait(180, send_num * 0.8, msg_id))
    {
        return false;
    }
    for (auto &node_id : send_node_ids)
    {
        SendFastSyncGetHashReq(node_id, msg_id, start_sync_height, end_sync_height);
    }
    std::vector<std::string> ret_datas;
    if (!GLOBALDATAMGRPTR.WaitData(msg_id, ret_datas))
    {
        if (ret_datas.size() < send_num * 0.5)
        {
            ERRORLOG("wait sync height time out send:{} recv:{}", send_num, ret_datas.size());
            return false;
        }
    }
    FastSyncGetHashAck ack;
    std::map<std::string, std::pair<uint32_t, std::set<std::string>>> sync_hashs;
    uint32_t ret_num = 0;
    for (auto &ret_data : ret_datas)
    {
        ack.Clear();
        if (!ack.ParseFromString(ret_data))
        {
            continue;
        }
        auto it = sync_hashs.find(ack.hash());
        if (sync_hashs.end() == it)
        {
            sync_hashs.insert(std::make_pair(ack.hash(), std::make_pair(1, std::set<std::string>())));
        }
        auto &value = sync_hashs.at(ack.hash());
        value.first = value.first + 1;
        value.second.insert(ack.self_node_id());
        ++ret_num;
    }
    int verify_num = ret_num * 0.75;
    for (auto &sync_hash : sync_hashs)
    {
        if (sync_hash.second.first >= verify_num)
        {
            ret_node_ids.assign(sync_hash.second.second.cbegin(), sync_hash.second.second.cend());
            sum_hash = sync_hash.first;
            break;
        }
    }
    return !ret_node_ids.empty();
}

bool SyncBlock::GetFastSyncBlockData(const std::string &send_node_id, const std::string &sum_hash, uint64_t start_sync_height, uint64_t end_sync_height)
{
    std::string msg_id;
    if (!GLOBALDATAMGRPTR.CreateWait(180, 1, msg_id))
    {
        return false;
    }
    SendFastSyncGetBlockReq(send_node_id, msg_id, start_sync_height, end_sync_height);
    std::vector<std::string> ret_datas;
    if (!GLOBALDATAMGRPTR.WaitData(msg_id, ret_datas))
    {
        return false;
    }
    if (ret_datas.empty())
    {
        return false;
    }
    FastSyncGetBlockAck ack;
    if (!ack.ParseFromString(ret_datas.at(0)))
    {
        return false;
    }
    CBlock block;
    CBlock hash_block;
    std::vector<std::string> block_hashes;
    std::map<uint64_t, std::set<CBlock, CBlockCompare>> fast_sync_block_data;
    for (auto &block_raw : ack.blocks())
    {
        if (block.ParseFromString(block_raw))
        {
            hash_block = block;
            hash_block.clear_hash();
            if (block.hash() != getsha256hash(hash_block.SerializeAsString()))
            {
                continue;
            }
            AddBlockToMap(block, fast_sync_block_data);
            block_hashes.push_back(block.hash());
        }
    }
    std::string hash;
    SumHeightHash(block_hashes, hash);
    if (sum_hash != hash)
    {
        return false;
    }
    FlushBlock(fast_sync_block_data, true);
    return true;
}

bool SyncBlock::GetSyncSumHashNode(const std::vector<std::string> &send_node_ids, uint64_t start_sync_height, uint64_t end_sync_height,
                                   std::map<uint64_t, uint64_t> &need_sync_heights, std::vector<std::string> &ret_node_ids, uint64_t &chain_height)
{
    need_sync_heights.clear();
    ret_node_ids.clear();
    std::string msg_id;
    size_t send_num = send_node_ids.size();
    if (!GLOBALDATAMGRPTR.CreateWait(90, send_num * 0.8, msg_id))
    {
        return false;
    }
    for (auto &node_id : send_node_ids)
    {
        SendSyncGetSumHashReq(node_id, msg_id, start_sync_height, end_sync_height);
    }
    std::vector<std::string> ret_datas;
    if (!GLOBALDATAMGRPTR.WaitData(msg_id, ret_datas))
    {
        if (ret_datas.empty() || ret_datas.size() < send_num / 2)
        {
            ERRORLOG("wait sync height time out send:{} recv:{}", send_num, ret_datas.size());
            return false;
        }
    }
    std::vector<uint64_t> ret_node_tops;
    SyncGetSumHashAck ack;
    std::map<std::string, uint32_t> sync_hash_datas;
    for (auto &ret_data : ret_datas)
    {
        ack.Clear();
        if (!ack.ParseFromString(ret_data))
        {
            continue;
        }
        ret_node_ids.push_back(ack.self_node_id());
        ret_node_tops.push_back(ack.node_block_height());
        for (auto &sync_sum_hash : ack.sync_sum_hashes())
        {
            std::string key = std::to_string(sync_sum_hash.start_height()) + "_" + std::to_string(sync_sum_hash.end_height()) + "_" + sync_sum_hash.hash();
            auto it = sync_hash_datas.find(key);
            if (sync_hash_datas.end() == it)
            {
                sync_hash_datas.insert(std::make_pair(key, 1));
            }
            else
            {
                auto &value = sync_hash_datas.at(key);
                value = value + 1;
            }
        }
    }
    std::sort(ret_node_tops.begin(), ret_node_tops.end());
    int verify_num = ret_node_tops.size() / 4 * 3;
    if (verify_num >= ret_node_tops.size())
    {
        ERRORLOG("get chain height error index:{}:{}", verify_num, ret_node_tops.size());
        return false;
    }
    chain_height = ret_node_tops.at(verify_num);
    std::set<uint64_t> heights;
    std::string hash;
    uint64_t start_height = 0;
    uint64_t end_height = 0;
    std::vector<std::string> block_hashes;
    std::vector<std::string> data_key;
    for (auto &sync_hash_data : sync_hash_datas)
    {
        if (sync_hash_data.second < verify_num)
        {
            continue;
        }
        data_key.clear();
        StringUtil::SplitString(sync_hash_data.first, data_key, "_");
        if (data_key.size() < 3)
        {
            continue;
        }
        start_height = std::stoull(data_key.at(0));
        end_height = std::stoull(data_key.at(1));
        hash.clear();
        block_hashes.clear();
        GetHeightBlockHash(start_height, end_height, block_hashes);
        SumHeightHash(block_hashes, hash);
        if (data_key.at(2) == hash)
        {
            continue;
        }
        for (uint64_t i = start_height; i <= end_height; i++)
        {
            heights.insert(i);
        }
    }
    int no_verify_height = 10;
    int64_t start = -1;
    int64_t end = -1;
    for (auto value : heights)
    {
        if (-1 == start && -1 == end)
        {
            start = value;
            end = start;
        }
        else
        {
            if (value != (end + 1))
            {
                need_sync_heights.insert(std::make_pair(start, end));
                start = -1;
                end = -1;
            }
            else
            {
                end = value;
            }
        }
    }
    if (-1 != start && -1 != end)
    {
        need_sync_heights.insert(std::make_pair(start, end));
    }
    //同步最新 no_verify_height 个高度
    if (end_sync_height >= (chain_height - no_verify_height))
    {
        if (chain_height > no_verify_height)
        {
            need_sync_heights.insert(std::make_pair(chain_height - no_verify_height, chain_height));
            need_sync_heights.insert(std::make_pair(chain_height, chain_height + no_verify_height));
        }
        else
        {
            need_sync_heights.insert(std::make_pair(1, chain_height + no_verify_height));
        }
    }
    return true;
}

bool SyncBlock::GetSyncBlockHashNode(const std::vector<std::string> &send_node_ids, uint64_t start_sync_height,
                                     uint64_t end_sync_height, uint64_t self_node_height, uint64_t chain_height,
                                     std::vector<std::string> &ret_node_ids, std::vector<std::string> &req_hashes)
{
    std::string msg_id;
    if (!GLOBALDATAMGRPTR.CreateWait(90, send_node_ids.size() * 0.8, msg_id))
    {
        return false;
    }
    for (auto &node_id : send_node_ids)
    {
        SendSyncGetHeightHashReq(node_id, msg_id, start_sync_height, end_sync_height);
    }
    std::vector<std::string> ret_datas;
    if (!GLOBALDATAMGRPTR.WaitData(msg_id, ret_datas))
    {
        if(ret_datas.size() < send_node_ids.size() * 0.5)
        {
            ERRORLOG("wait sync block hash time out send:{} recv:{}", send_node_ids.size(), ret_datas.size());
            return false;
        }
    }
    SyncGetHeightHashAck ack;
    std::map<std::string, std::set<std::string>> sync_block_hashes;
    for (auto &ret_data : ret_datas)
    {
        ack.Clear();
        if (!ack.ParseFromString(ret_data))
        {
            continue;
        }
        for (auto &key : ack.block_hashes())
        {
            auto it = sync_block_hashes.find(key);
            if (sync_block_hashes.end() == it)
            {
                sync_block_hashes.insert(std::make_pair(key, std::set<std::string>()));
            }
            auto &value = sync_block_hashes.at(key);
            value.insert(ack.self_node_id());
        }
    }
    std::set<std::string> nodes;
    std::vector<std::string> intersection_nodes;
    std::set<std::string> verify_hashes;
    req_hashes.clear();
    size_t verify_num = ret_datas.size() / 5 * 4;
    //将大于60%的区块hash放到数组中
    std::string strblock;
    std::vector<std::string> exits_hashes;
    std::vector<uint64_t> rollback_heights;
    std::vector<CBlock> rollback_blocks;
    CBlock block;
    DBReader db_reader;
    for (auto &sync_block_hash : sync_block_hashes)
    {
        strblock.clear();
        auto ret = db_reader.GetBlockByBlockHash(sync_block_hash.first, strblock);
        if (DBStatus::DB_SUCCESS == ret)
        {
            exits_hashes.push_back(sync_block_hash.first);
        }
        else if(DBStatus::DB_NOT_FOUND != ret)
        {
            return false;
        }
        if (sync_block_hash.second.size() >= verify_num)
        {
            if (DBStatus::DB_NOT_FOUND == ret)
            {
                verify_hashes.insert(sync_block_hash.first);
                if (nodes.empty())
                {
                    nodes = sync_block_hash.second;
                }
                else
                {
                    std::set_intersection(nodes.cbegin(), nodes.cend(), sync_block_hash.second.cbegin(), sync_block_hash.second.cend(), std::back_inserter(intersection_nodes));
                    nodes.insert(intersection_nodes.cbegin(), intersection_nodes.cend());
                    intersection_nodes.clear();
                }
            }
        }
        //当区块所在节点数量小于20%，并且本地是存在该区块的时候，回滚区块
        else
        {
            if (DBStatus::DB_SUCCESS == ret && block.ParseFromString(strblock))
            {
                if (block.height() < chain_height - 10)
                {
                    rollback_blocks.push_back(block);
                    rollback_heights.push_back(block.height());
                }
            }
        }
    }


    std::vector<std::string> v_diff;
    uint64_t end_height = end_sync_height > self_node_height ? self_node_height+1 : end_sync_height;
    if(end_height > start_sync_height)
    {
        //获取本地高度区间所有区块hash，判断是否在可信列表，当不再可信列表中的时候回滚掉
        std::vector<std::string> block_hashes;
        if(DBStatus::DB_SUCCESS != db_reader.GetBlockHashesByBlockHeight(start_sync_height, end_height, block_hashes))
        {
            return false;
        }
        std::sort(block_hashes.begin(), block_hashes.end());
        std::sort(exits_hashes.begin(), exits_hashes.end());
        std::set_difference(block_hashes.begin(), block_hashes.end(), exits_hashes.begin(), exits_hashes.end(), std::back_inserter(v_diff));
    }

    for (auto it = v_diff.cbegin(); it != v_diff.cend(); it++)
    {
        //处理回滚
        block.Clear();
        std::string().swap(strblock);
        auto ret = db_reader.GetBlockByBlockHash(*it, strblock);
        if (DBStatus::DB_SUCCESS != ret)
        {
            continue;
        }
        block.ParseFromString(strblock);
        //当要添加的区块所在高度小于链上最高高度-10 的情况下才会回滚
        uint64_t tmp_height = block.height();
        if ((tmp_height < chain_height) && chain_height - tmp_height > 10)
        {
            DEBUGLOG("rollback:{}", block.hash());
            rollback_heights.push_back(tmp_height);
            rollback_blocks.push_back(block);
        }
    }
    rollback_hashs_.clear();
    if (rollback_heights.size() > 0)
    {
        for (auto rollback_block : rollback_blocks)
        {
            rollback_hashs_.push_back(rollback_block.hash());
        }
    }
    if (verify_hashes.empty())
    {
        return true;
    }
    req_hashes.assign(verify_hashes.cbegin(), verify_hashes.cend());
    ret_node_ids.assign(nodes.cbegin(), nodes.cend());
    return !ret_node_ids.empty();
}

bool SyncBlock::GetSyncBlockData(const std::vector<std::string> &send_node_ids, const std::vector<std::string> &req_hashes)
{
    if (req_hashes.empty() || send_node_ids.empty())
    {
        MagicSingleton<BlockPoll>::GetInstance()->AddSyncBlock(std::vector<CBlock>(), rollback_hashs_);
        rollback_hashs_.clear();
        return true;
    }
    std::string msg_id;
    if (!GLOBALDATAMGRPTR.CreateWait(90, send_node_ids.size() * 0.8, msg_id))
    {
        return false;
    }
    for (auto &node_id : send_node_ids)
    {
        SendSyncGetBlockReq(node_id, msg_id, req_hashes);
    }
    std::vector<std::string> ret_datas;
    if (!GLOBALDATAMGRPTR.WaitData(msg_id, ret_datas))
    {
        if(ret_datas.empty())
        {
            ERRORLOG("wait sync block data time out send:{} recv:{}", send_node_ids.size(), ret_datas.size());
            return false;
        }
    }

    CBlock block;
    CBlock hash_block;
    SyncGetBlockAck ack;
    std::map<uint64_t, std::set<CBlock, CBlockCompare>> sync_block_data;
    for (auto &ret_data : ret_datas)
    {
        ack.Clear();
        if (!ack.ParseFromString(ret_data))
        {
            continue;
        }
        for (auto &block_raw : ack.blocks())
        {
            if (block.ParseFromString(block_raw))
            {
                if (req_hashes.cend() == std::find(req_hashes.cbegin(), req_hashes.cend(), block.hash()))
                {
                    continue;
                }
                hash_block = block;
                hash_block.clear_hash();
                if (block.hash() != getsha256hash(hash_block.SerializeAsString()))
                {
                    continue;
                }
                AddBlockToMap(block, sync_block_data);
            }
        }
    }
    std::vector<CBlock> blocks;
    for (auto it = sync_block_data.begin(); it != sync_block_data.end(); ++it)
    {
        for (auto sit = it->second.begin(); sit != it->second.end(); ++sit)
        {
            blocks.push_back(*sit);
        }
    }
    MagicSingleton<BlockPoll>::GetInstance()->AddSyncBlock(blocks, rollback_hashs_);
    return true;
    // bool flag = FlushBlock(sync_block_data, false);
    // ProcessBroadcast();
    // return flag;
}

void SyncBlock::AddBlockToMap(const CBlock &block, std::map<uint64_t, std::set<CBlock, CBlockCompare>> &sync_block_data)
{
    if (sync_block_data.end() == sync_block_data.find(block.height()))
    {
        sync_block_data.insert(std::make_pair(block.height(), std::set<CBlock, CBlockCompare>()));
    }
    auto &value = sync_block_data.at(block.height());
    value.insert(block);
}

bool SyncBlock::FlushBlock(const std::map<uint64_t, std::set<CBlock, CBlockCompare>> &sync_block_data, bool no_verify)
{
    std::vector<std::string> rollback_hashs;
    if (no_verify)
    {
        DBReader db_reader;
        std::vector<std::string> block_hashs;
        std::vector<std::string> tmp_hashs;
        for (auto it = sync_block_data.rbegin(); sync_block_data.rend() != it; ++it)
        {
            block_hashs.clear();
            auto status = db_reader.GetBlockHashsByBlockHeight(it->first, block_hashs);
            if(status == DB_NOT_FOUND)
            {
                continue;
            }
            tmp_hashs.clear();
            for (auto sit = it->second.begin(); sit != it->second.end(); ++sit)
            {
                tmp_hashs.push_back(sit->hash());
            }
            for(auto &hash : block_hashs)
            {
                if(tmp_hashs.cend() == std::find(tmp_hashs.cbegin(), tmp_hashs.cend(), hash))
                {
                    rollback_hashs.push_back(hash);
                }
            }
        }
    }

    for(auto &hash : rollback_hashs)
    {
        ca_algorithm::RollBackByHash(hash);
    }

    uint32_t save_num = 0;
    DBReadWriter db_writer;
    for (auto it = sync_block_data.begin(); it != sync_block_data.end(); ++it)
    {
        for (auto sit = it->second.begin(); sit != it->second.end(); ++sit)
        {
            if (no_verify)
            {
                auto ret = ca_algorithm::SaveBlock(db_writer, *sit);
                INFOLOG("save block ret:{}:{}:{}", ret, sit->height(), sit->hash());
                if (0 != ret)
                {
                    return false;
                }
            }
            else
            {
                if(1 == sit->version())
                {
                    auto ret = ca_algorithm::VerifyBlock(*sit, false, &db_writer);
                    if (0 != ret)
                    {
                        ERRORLOG("verify block fail ret:{}:{}:{}", ret, sit->height(), sit->hash());
                        return false;
                    }
                }
                else if(0 == sit->version())
                {
                    if (DBStatus::DB_SUCCESS != db_writer.TransactionCommit())
                    {
                        return false;
                    }
                    if (DBStatus::DB_SUCCESS != db_writer.ReTransactionInit())
                    {
                        return false;
                    }
                    save_num = 0;
                    int veret = VerifyBuildBlock(*sit);
                    if(veret <0)
                    {
                        continue;
                    }
                    bool flag = VerifyBlockHeader(*sit);
                    if(!flag)
                    {
                        continue;
                    }
                }
                auto ret = ca_algorithm::SaveBlock(db_writer, *sit);
                INFOLOG("save block ret:{}:{}:{}", ret, sit->height(), sit->hash());
                if (0 != ret)
                {
                    return false;
                }
                for (int i = 0; i < sit->txs_size(); i++)
                {
                    CTransaction tx = sit->txs(i);
                    if (CheckTransactionType(tx) == kTransactionType_Tx)
                    {
                        std::vector<std::string> txOwnerVec;
                        StringUtil::SplitString(tx.owner(), txOwnerVec, "_");
                        int result = MagicSingleton<TxVinCache>::GetInstance()->Remove(tx.hash(), txOwnerVec);
                        if (result == 0)
                        {
                            std::cout << "Remove pending transaction in Cache " << tx.hash() << " from ";
                            for_each(txOwnerVec.begin(), txOwnerVec.end(), [](const string& owner){ cout << owner << " "; });
                            std::cout << std::endl;
                        }
                    }
                }
                if (Singleton<Config>::get_instance()->HasHttpCallback())
                {
                    if (MagicSingleton<CBlockHttpCallback>::GetInstance()->IsRunning())
                    {
                        MagicSingleton<CBlockHttpCallback>::GetInstance()->AddBlock(*sit);
                    }
                }
            }
            if (save_num >= 20)
            {
                if (DBStatus::DB_SUCCESS != db_writer.TransactionCommit())
                {
                    return false;
                }
                if (DBStatus::DB_SUCCESS != db_writer.ReTransactionInit())
                {
                    return false;
                }
                save_num = 0;
            }
            else
            {
                ++save_num;
            }
        }
    }
    if (DBStatus::DB_SUCCESS != db_writer.TransactionCommit())
    {
        return false;
    }
    Singleton<PeerNode>::get_instance()->set_self_height();

    return true;
}

void SendFastSyncGetHashReq(const std::string &node_id, const std::string &msg_id, uint64_t start_height, uint64_t end_height)
{
    FastSyncGetHashReq req;
    req.set_self_node_id(net_get_self_node_id());
    req.set_msg_id(msg_id);
    req.set_start_height(start_height);
    req.set_end_height(end_height);
    net_send_message<FastSyncGetHashReq>(node_id, req, net_com::Compress::kCompress_False, net_com::Encrypt::kEncrypt_False, net_com::Priority::kPriority_High_1);
}

void SendFastSyncGetHashAck(const std::string &node_id, const std::string &msg_id, uint64_t start_height, uint64_t end_height)
{
    if(start_height > end_height)
    {
        return;
    }
    if ((end_height - start_height) > 100000)
    {
        return;
    }
    FastSyncGetHashAck ack;
    ack.set_self_node_id(net_get_self_node_id());
    ack.set_msg_id(msg_id);
    uint64_t node_block_height = 0;
    if (DBStatus::DB_SUCCESS != DBReader().GetBlockTop(node_block_height))
    {
        ERRORLOG("GetBlockTop error");
        return;
    }
    ack.set_node_block_height(node_block_height);

    std::string hash;
    std::vector<std::string> block_hashes;
    if (GetHeightBlockHash(start_height, end_height, block_hashes) && SumHeightHash(block_hashes, hash))
    {
        ack.set_hash(hash);
        net_send_message<FastSyncGetHashAck>(node_id, ack, net_com::Compress::kCompress_False, net_com::Encrypt::kEncrypt_False, net_com::Priority::kPriority_High_1);
    }
}

void SendFastSyncGetBlockReq(const std::string &node_id, const std::string &msg_id, uint64_t start_height, uint64_t end_height)
{
    FastSyncGetBlockReq req;
    req.set_self_node_id(net_get_self_node_id());
    req.set_msg_id(msg_id);
    req.set_start_height(start_height);
    req.set_end_height(end_height);
    net_send_message<FastSyncGetBlockReq>(node_id, req, net_com::Compress::kCompress_False, net_com::Encrypt::kEncrypt_False, net_com::Priority::kPriority_High_1);
}

void SendFastSyncGetBlockAck(const std::string &node_id, const std::string &msg_id, uint64_t start_height, uint64_t end_height)
{
    if(start_height > end_height)
    {
        return;
    }
    if ((end_height - start_height) > 1500)
    {
        return;
    }
    FastSyncGetBlockAck ack;
    ack.set_msg_id(msg_id);
    DBReader db_reader;
    std::vector<std::string> block_hashes;
    if (DBStatus::DB_SUCCESS != db_reader.GetBlockHashesByBlockHeight(start_height, end_height, block_hashes))
    {
        return;
    }
    std::vector<std::string> blocks;
    if (DBStatus::DB_SUCCESS != db_reader.GetBlocksByBlockHash(block_hashes, blocks))
    {
        return;
    }
    for (auto &block : blocks)
    {
        ack.add_blocks(block);
    }
    net_send_message<FastSyncGetBlockAck>(node_id, ack, net_com::Compress::kCompress_True, net_com::Encrypt::kEncrypt_False, net_com::Priority::kPriority_High_1);
}

int HandleFastSyncGetHashReq(const std::shared_ptr<FastSyncGetHashReq> &msg, const MsgData &msgdata)
{
    SendFastSyncGetHashAck(msg->self_node_id(), msg->msg_id(), msg->start_height(), msg->end_height());
    return 0;
}

int HandleFastSyncGetHashAck(const std::shared_ptr<FastSyncGetHashAck> &msg, const MsgData &msgdata)
{
    GLOBALDATAMGRPTR.AddWaitData(msg->msg_id(), msg->SerializeAsString());
    return 0;
}

int HandleFastSyncGetBlockReq(const std::shared_ptr<FastSyncGetBlockReq> &msg, const MsgData &msgdata)
{
    SendFastSyncGetBlockAck(msg->self_node_id(), msg->msg_id(), msg->start_height(), msg->end_height());
    return 0;
}

int HandleFastSyncGetBlockAck(const std::shared_ptr<FastSyncGetBlockAck> &msg, const MsgData &msgdata)
{
    GLOBALDATAMGRPTR.AddWaitData(msg->msg_id(), msg->SerializeAsString());
    return 0;
}

void SendSyncGetSumHashReq(const std::string &node_id, const std::string &msg_id, uint64_t start_height, uint64_t end_height)
{
    SyncGetSumHashReq req;
    req.set_self_node_id(net_get_self_node_id());
    req.set_msg_id(msg_id);
    req.set_start_height(start_height);
    req.set_end_height(end_height);
    net_send_message<SyncGetSumHashReq>(node_id, req, net_com::Compress::kCompress_False, net_com::Encrypt::kEncrypt_False, net_com::Priority::kPriority_High_1);
}

void SendSyncGetSumHashAck(const std::string &node_id, const std::string &msg_id, uint64_t start_height, uint64_t end_height)
{
    //当请求高度大于1000时不处理

    if(start_height > end_height)
    {
        return;
    }
    if (end_height - start_height > 1000)
    {
        return;
    }
    SyncGetSumHashAck ack;
    ack.set_self_node_id(net_get_self_node_id());
    DBReader db_reader;
    uint64_t self_node_height = 0;
    if (0 != db_reader.GetBlockTop(self_node_height))
    {
        ERRORLOG("GetBlockTop(txn, top)");
        return;
    }
    ack.set_node_block_height(self_node_height);
    ack.set_msg_id(msg_id);

    uint64_t end = end_height > self_node_height ? self_node_height+1 : end_height;
    std::string hash;
    uint64_t j = 0;
    std::vector<std::string> block_hashes;
    for (uint64_t i = start_height; j < end;)
    {
        j = i + 10; //每10个高度算一个hash
        j = j > end ? end : j;
        block_hashes.clear();
        hash.clear();
        if (GetHeightBlockHash(i, j, block_hashes) && SumHeightHash(block_hashes, hash))
        {
            auto sync_sum_hash = ack.add_sync_sum_hashes();
            sync_sum_hash->set_start_height(i);
            sync_sum_hash->set_end_height(j);
            sync_sum_hash->set_hash(hash);
        }
        else
        {
            return;
        }
        i = j - 1;
    }
    net_send_message<SyncGetSumHashAck>(node_id, ack, net_com::Compress::kCompress_True, net_com::Encrypt::kEncrypt_False, net_com::Priority::kPriority_High_1);
}

void SendSyncGetHeightHashReq(const std::string &node_id, const std::string &msg_id, uint64_t start_height, uint64_t end_height)
{
    SyncGetHeightHashReq req;
    req.set_self_node_id(net_get_self_node_id());
    req.set_msg_id(msg_id);
    req.set_start_height(start_height);
    req.set_end_height(end_height);
    net_send_message<SyncGetHeightHashReq>(node_id, req, net_com::Compress::kCompress_False, net_com::Encrypt::kEncrypt_False, net_com::Priority::kPriority_High_1);
}

void SendSyncGetHeightHashAck(const std::string &node_id, const std::string &msg_id, uint64_t start_height, uint64_t end_height)
{
    if(start_height > end_height)
    {
        return;
    }
    if (end_height - start_height > 500)
    {
        return;
    }
    SyncGetHeightHashAck ack;
    ack.set_self_node_id(net_get_self_node_id());
    DBReader db_reader;
    uint64_t self_node_height = 0;
    if (0 != db_reader.GetBlockTop(self_node_height))
    {
        ERRORLOG("GetBlockTop(txn, top)");
        return;
    }
    ack.set_msg_id(msg_id);
    std::vector<std::string> block_hashes;
    if(end_height > self_node_height)
    {
        end_height = self_node_height + 1;
    }
    if(start_height > end_height)
    {
        return;
    }
    if (DBStatus::DB_SUCCESS != db_reader.GetBlockHashesByBlockHeight(start_height, end_height, block_hashes))
    {
        return;
    }
    for (auto hash : block_hashes)
    {
        ack.add_block_hashes(hash);
    }
    net_send_message<SyncGetHeightHashAck>(node_id, ack, net_com::Compress::kCompress_True, net_com::Encrypt::kEncrypt_False, net_com::Priority::kPriority_High_1);
}

void SendSyncGetBlockReq(const std::string &node_id, const std::string &msg_id, const std::vector<std::string> &req_hashes)
{
    SyncGetBlockReq req;
    req.set_self_node_id(net_get_self_node_id());
    req.set_msg_id(msg_id);
    for (auto hash : req_hashes)
    {
        req.add_block_hashes(hash);
    }
    net_send_message<SyncGetBlockReq>(node_id, req, net_com::Compress::kCompress_False, net_com::Encrypt::kEncrypt_False, net_com::Priority::kPriority_High_1);
}

void SendSyncGetBlockAck(const std::string &node_id, const std::string &msg_id, const std::vector<std::string> &req_hashes)
{
    //当请求数据超过5000时不处理
    if (req_hashes.size() > 5000)
    {
        return;
    }
    SyncGetBlockAck ack;
    ack.set_msg_id(msg_id);
    DBReader db_reader;
    std::vector<std::string> blocks;
    if (DBStatus::DB_SUCCESS != db_reader.GetBlocksByBlockHash(req_hashes, blocks))
    {
        return;
    }
    for (auto &block : blocks)
    {
        ack.add_blocks(block);
    }
    net_send_message<SyncGetBlockAck>(node_id, ack, net_com::Compress::kCompress_True, net_com::Encrypt::kEncrypt_False, net_com::Priority::kPriority_High_1);
}

int HandleSyncGetSumHashReq(const std::shared_ptr<SyncGetSumHashReq> &msg, const MsgData &msgdata)
{
    SendSyncGetSumHashAck(msg->self_node_id(), msg->msg_id(), msg->start_height(), msg->end_height());
    return 0;
}

int HandleSyncGetSumHashAck(const std::shared_ptr<SyncGetSumHashAck> &msg, const MsgData &msgdata)
{
    GLOBALDATAMGRPTR.AddWaitData(msg->msg_id(), msg->SerializeAsString());
    return 0;
}

int HandleSyncGetHeightHashReq(const std::shared_ptr<SyncGetHeightHashReq> &msg, const MsgData &msgdata)
{
    SendSyncGetHeightHashAck(msg->self_node_id(), msg->msg_id(), msg->start_height(), msg->end_height());
    return 0;
}

int HandleSyncGetHeightHashAck(const std::shared_ptr<SyncGetHeightHashAck> &msg, const MsgData &msgdata)
{
    GLOBALDATAMGRPTR.AddWaitData(msg->msg_id(), msg->SerializeAsString());
    return 0;
}

int HandleSyncGetBlockReq(const std::shared_ptr<SyncGetBlockReq> &msg, const MsgData &msgdata)
{
    std::vector<std::string> req_hashes;
    for (auto hash : msg->block_hashes())
    {
        req_hashes.push_back(hash);
    }
    SendSyncGetBlockAck(msg->self_node_id(), msg->msg_id(), req_hashes);
    return 0;
}

int HandleSyncGetBlockAck(const std::shared_ptr<SyncGetBlockAck> &msg, const MsgData &msgdata)
{
    GLOBALDATAMGRPTR.AddWaitData(msg->msg_id(), msg->SerializeAsString());
    return 0;
}
