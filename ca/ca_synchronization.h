#ifndef __CA_SYNCHRONIZATION_H_
#define __CA_SYNCHRONIZATION_H_

#include <vector>
#include <string>
#include <mutex>
#include "../proto/sync_block.pb.h"
#include "net/msg_queue.h"
#include "../include/net_interface.h"
#include "ca_global.h"
#include "../utils/util.h"

const int SYNCNUM = 5;      //同步寻找节点数
const int CHECKNUM = 50;    //漏块检查的段数
const int CHECK_HEIGHT = 10;  //漏块检查的段数每段个数
const int HASH_LEN = 6;       // hash长度
const int SYNC_NUM_LIMIT = 500;   // 最多同步多少块

const int NEWSYNCNUM = 50;      //新的同步方式中同步寻找节点数

struct SyncNode
{
    std::string id = "";            // 节点id
    int64_t height = 0;             // 最高块的高度
    std::string hash = "";          // 块hash
    std::string forwardHash = "";   // 请求高度前n块分组hash
    std::string backwardHash = "";  // 请求高度后n块分组hash
    
    SyncNode(){
    }
    SyncNode(std::string id_, int64_t height_, std::string hash_, std::string forward_, std::string backward_)
            :id(id_), height(height_), hash(hash_), forwardHash(forward_), backwardHash(backward_){

    }
    bool operator <(const SyncNode& other)
    {
        if(height < other.height)
        {
            return true;
        }
        return false;
    }

};

class Sync
{
public:
    Sync() = default;
    ~Sync()  = default;
    enum SYNCRET{
        SYNC_SUCCESS,   //同步成功
        SYNC_FAIL_ADJUST,//同步失败后回调高度
        SYNC_ADJUST,    //同步成功后回调高度
        SYNC_FAIL,      //同步失败
    };
    bool SetPotentialNodes(const std::string &id, 
							const int64_t &height, 
							const std::string &hash, 
							const std::string & forwardHash, 
							const std::string & backwardHash);

    void SetPledgeNodes(const std::vector<std::string> & ids);

    void Process();
    bool DataSynch(std::string id, const int syncNum);
    int GetSyncInfo(std::vector<std::string> & reliables, int64_t & syncHeight);
    void ReliableNode();
    int SyncDataFromPubNode();

    std::vector<std::string> pledgeNodes;     // 已经质押的节点
    SyncNode verifying_node;                  // 正在验证的节点
    std::vector<SyncNode> verifying_result;   // 正在验证的节点返回的结果
    std::vector<SyncNode> potential_nodes;    // 潜在可靠节点
    uint32_t reliableCount = 0;               // 查找可靠节点失败计次
    bool is_sync = false;
    std::mutex mu_potential_nodes;
    std::mutex mu_verifying_result;
    std::mutex mu_get_pledge;
    std::mutex reliableLock;                  // 可靠节点查找失败计次锁
    int conflict_height = -1;
    bool sync_adding = false;

    std::thread sync_thread_;
};

//============区块同步交互协议================

void SendSyncGetnodeInfoReq(std::string id, uint64_t height);
void SendVerifyReliableNodeReq(std::string id, int64_t height);
void SendSyncBlockInfoReq(std::string id, const int syncNum);
void SendSyncGetPledgeNodeReq(std::string id);
int SendVerifyPledgeNodeReq(std::vector<std::string> ids);

int HandleSyncGetnodeInfoReq( const std::shared_ptr<SyncGetnodeInfoReq>& msg, const MsgData& msgdata );
int HandleSyncGetnodeInfoAck( const std::shared_ptr<SyncGetnodeInfoAck>& msg, const MsgData& msgdata );

int HandleVerifyReliableNodeReq( const std::shared_ptr<VerifyReliableNodeReq>& msg, const MsgData& msgdata );
int HandleVerifyReliableNodeAck( const std::shared_ptr<VerifyReliableNodeAck>& msg, const MsgData& msgdata );

int HandleSyncBlockInfoReq( const std::shared_ptr<SyncBlockInfoReq>& msg, const MsgData& msgdata );
int HandleSyncBlockInfoAck( const std::shared_ptr<SyncBlockInfoAck>& msg, const MsgData& msgdata );

int HandleSyncLoseBlockReq( const std::shared_ptr<SyncLoseBlockReq>& msg, const MsgData& msgdata );
int HandleSyncLoseBlockAck( const std::shared_ptr<SyncLoseBlockAck>& msg, const MsgData& msgdata );

int HandleSyncGetPledgeNodeReq( const std::shared_ptr<SyncGetPledgeNodeReq>& msg, const MsgData& msgdata );
int HandleSyncGetPledgeNodeAck( const std::shared_ptr<SyncGetPledgeNodeAck>& msg, const MsgData& msgdata );

int HandleSyncVerifyPledgeNodeReq( const std::shared_ptr<SyncVerifyPledgeNodeReq>& msg, const MsgData& msgdata );
int HandleSyncVerifyPledgeNodeAck( const std::shared_ptr<SyncVerifyPledgeNodeAck>& msg, const MsgData& msgdata );

bool IsOverIt(int64_t height);
std::string get_blkinfo_ser(int64_t begin, int64_t end, int64_t max_num);
std::vector<std::string> get_blkinfo(int64_t begin, int64_t end, int64_t max_num);
int SyncData(std::string &headerstr, bool isSync = false);
template <typename T>
void SetSyncHeaderMsg(T& msg);

template <typename T>
void SetSyncHeaderMsg(T& msg)
{
	SyncHeaderMsg * pSyncHeaderMsg = msg.mutable_syncheadermsg();
    pSyncHeaderMsg->set_version(getVersion());
	pSyncHeaderMsg->set_id(net_get_self_node_id());
}


#endif
