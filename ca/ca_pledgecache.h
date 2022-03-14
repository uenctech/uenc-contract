// ca_pledgelist.h
// Create: the pledge list, 20211019  Liu
#ifndef __CA_PLEDGE_LIST_H__
#define __CA_PLEDGE_LIST_H__

#include <vector>
#include <string>
#include <mutex>
#include "../net/peer_node.h"
#include "../utils/CTimer.hpp"

using namespace std;

class PledgeCache
{
public:
    PledgeCache() = default;
    ~PledgeCache() = default;
    PledgeCache(const PledgeCache&) = delete;
    PledgeCache(PledgeCache&&) = delete;
    PledgeCache& operator=(const PledgeCache&) = delete;
    PledgeCache& operator=(PledgeCache&&) = delete;

public:
    void Process();
    bool IsExist(const string& address);
    void GetPledge(vector<string>& pledge);
    void GetUnpledge(vector<string>& unpledge);
    void GetPledge(uint32 height, vector<Node>& pledge);
    void GetUnpledge(uint32 height, vector<Node>& unpledge);

    void GetPledge(vector<Node>& pledge);
    void GetUnpledge(vector<Node>& unpledge);
    void GetPublicPledge(vector<Node>& public_pledge);
    void GetPublicUnpledge(vector<Node>& public_unPledge);
    bool IsInit();
    void Init();

    void Start();
    void Stop();
    static int HandleTimer(PledgeCache* cache);

    void AddPledgeId(const string& id) { pledge_id_.push_back(id); }
    void AddPublicPledgeId(const string& id) { public_pledge_id_.push_back(id); }

    void ClearHeightCache();
    void AddHeightCache(uint32 height, const vector<Node>& nodes);

    void ClearHeightUnpledgeCache();
    void AddHeightUnpledgeCache(uint32 height, const vector<Node>& nodes);

    void CalcPledgeSubnodeHeight43();
    void CalcUnpledgeSubnodeHeight43();

    void CalcPledgePublicNodeHeight43();
    void CalcUnpledgePublicNodeHeight43();

private:
    uint32 CalcNodesHeight(const vector<Node>& nodes);
    uint32 CalcNodesHeight1(const vector<Node>& nodes);
    uint32 CalcNodesHeight2(const vector<Node>& nodes);
    bool IsReasonableHeight(uint32 height1, uint32 height2);
    bool IsReasonableHeight(uint32 nodeHeight, uint32 subNodeHeight, uint32 height43);

private:
    vector<string> pledge_list_;
    vector<string> public_pledge_list_;
    vector<string> pledge_id_;
    vector<string> unpledge_id_;
    vector<string> public_pledge_id_;
    vector<string> public_unpledge_id_;
    mutex cache_mutex_;

    mutex height_nodes_mutex_;
    std::map<uint32, std::vector<Node>> map_height_nodes_;
    mutex height_unpledge_nodes_mutex_;
    std::map<uint32, std::vector<Node>> map_height_unpledge_nodes_;

    uint32 pledge_height43_ = 0;
    uint32 unpledge_height43_ = 0;

    uint32 public_pledge_height43_ = 0;
    uint32 public_unpledge_height43_ = 0;

    CTimer init_timer_;
    CTimer timer_;
    CTimer height_cache_timer_;
};

#endif