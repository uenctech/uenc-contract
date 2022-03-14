#include "ca_pledgecache.h"
#include "db/db_api.h"
#include "ca_transaction.h"
#include "ca_global.h"
#include <algorithm>
#include <stdlib.h>
#include "../common/config.h"
#include "../common/time_report.h"

void PledgeCache::Process()
{
    // Get all pledge    
    vector<string> addresses;
    DBReader db_reader;
	auto status = db_reader.GetPledgeAddress(addresses);
    if (status != DBStatus::DB_SUCCESS && status != DBStatus::DB_NOT_FOUND)
    {
        return ;
    }
    set<string> pledge_list;
    for (const auto& addr : addresses)
    {
        uint64_t pledgeAmount = 0;
        SearchPledge(addr, pledgeAmount);
        if (pledgeAmount >= g_TxNeedPledgeAmt)
        {
            pledge_list.insert(addr);
        }
    }

    set<string> public_pledge_list;
    for (const auto& addr : addresses)
    {
        uint64_t pledgeAmount = 0;
        SearchPledge(addr, pledgeAmount, PLEDGE_PUBLIC_NET_LICENCE);
        if (pledgeAmount >= g_TxNeedPublicPledgeAmt)
        {
            public_pledge_list.insert(addr);
        }
    }

    // Partition public node to pledge and unpledge
    set<string> public_pledge_id;
    set<string> public_unpledge_id;
    std::vector<Node> public_nodes = Singleton<PeerNode>::get_instance()->get_public_node();

    Node selfNode = Singleton<PeerNode>::get_instance()->get_self_node();
    if (selfNode.is_public_node)
    {
        public_nodes.push_back(selfNode);
    }

    for (const Node& node : public_nodes)
    {
        auto find = binary_search(public_pledge_list.begin(), public_pledge_list.end(), node.base58address);
        if (find)
        {
            public_pledge_id.insert(node.base58address);
        }
        else
        {
            public_unpledge_id.insert(node.base58address);
        }
    }

    // Partition subnode to pledge and unpledge
    set<string> pledge_id;
    set<string> unpledge_id;
    vector<Node> nodelist = Singleton<PeerNode>::get_instance()->get_nodelist();
    //ofstream stream("process_pledge.txt");
    for (const Node& node : nodelist)
	{
        if (node.is_public_node)
        {
            continue;
        }
        auto find = binary_search(pledge_list.begin(), pledge_list.end(), node.base58address);
        if (find)
        {
            //stream << "id: " << node.id << ", base58: " << node.base58address << std::endl;
            pledge_id.insert(node.base58address);
        }
        else
        {
            unpledge_id.insert(node.base58address);
        }
	}
    //stream.close();
    {
    lock_guard<mutex> lock(cache_mutex_);
    pledge_list_.assign(pledge_list.begin(), pledge_list.end());
    pledge_id_.assign(pledge_id.begin(), pledge_id.end());
    unpledge_id_.assign(unpledge_id.begin(), unpledge_id.end());

    public_pledge_list_.assign(public_pledge_list.begin(), public_pledge_list.end());
    public_pledge_id_.assign(public_pledge_id.begin(), public_pledge_id.end());
    public_unpledge_id_.assign(public_unpledge_id.begin(), public_unpledge_id.end());
    }

    ClearHeightCache();
    ClearHeightUnpledgeCache();
}

bool PledgeCache::IsExist(const string& address)
{
    lock_guard<mutex> lock(cache_mutex_);
    auto iter = find(pledge_list_.begin(), pledge_list_.end(), address);
    return (iter != pledge_list_.end());
}

void PledgeCache::GetPledge(vector<string>& pledge)
{
    lock_guard<mutex> lock(cache_mutex_);
    pledge = pledge_id_;
}

void PledgeCache::GetUnpledge(vector<string>& unpledge)
{
    lock_guard<mutex> lock(cache_mutex_);
    unpledge = unpledge_id_;
}

void PledgeCache::GetPledge(uint32 height, vector<Node>& pledge)
{
    pledge.clear();
    {
        // First find in the height cache
        lock_guard<mutex> lock(height_nodes_mutex_);
        auto iter = map_height_nodes_.find(height);
        if (iter != map_height_nodes_.end())
        {
            pledge = iter->second;
            return;
        }
    }

    lock_guard<mutex> lock(cache_mutex_);
    uint32 height43 = pledge_height43_;
    for (const auto& id : pledge_id_)
    {
        Node node;
        if (Singleton<PeerNode>::get_instance()->find_node(id, node))
        {
            if (IsReasonableHeight(node.height, height, height43))
            {
                pledge.push_back(node);
            }
        }
    }

    AddHeightCache(height, pledge);
    //cout << "GetPledge total=" << pledge_id_.size() << ", filter=" << filter << ", get=" << pledge_id_.size()-filter << endl;
}

void PledgeCache::GetUnpledge(uint32 height, vector<Node>& unpledge)
{
    unpledge.clear();

    {
        lock_guard<mutex> lock(height_unpledge_nodes_mutex_);
        auto iter = map_height_unpledge_nodes_.find(height);
        if (iter != map_height_unpledge_nodes_.end())
        {
            unpledge = iter->second;
            return ;
        }
    }

    uint32 height43 = unpledge_height43_;
    for (const auto& id : unpledge_id_)
    {
        Node node;
        if (Singleton<PeerNode>::get_instance()->find_node(id, node))
        {
            if (IsReasonableHeight(node.height, height, height43))
            {
                unpledge.push_back(node);
            }
        }
    }

    AddHeightUnpledgeCache(height, unpledge);
}

void PledgeCache::CalcPledgeSubnodeHeight43()
{
    cache_mutex_.lock();
    vector<Node> allPledges;
    for (const auto& id : pledge_id_)
    {
        Node node;
        if (Singleton<PeerNode>::get_instance()->find_node(id, node))
        {
            allPledges.push_back(node);
        }
    }
    cache_mutex_.unlock();

    uint32 height43 = CalcNodesHeight(allPledges);
    pledge_height43_ = height43;
}

void PledgeCache::CalcUnpledgeSubnodeHeight43()
{
    cache_mutex_.lock();
    vector<Node> allUnpledges;
    for (const auto& id : unpledge_id_)
    {
        Node node;
        if (Singleton<PeerNode>::get_instance()->find_node(id, node))
        {
            allUnpledges.push_back(node);
        }
    }
    cache_mutex_.unlock();

    uint32 height43 = CalcNodesHeight(allUnpledges);
    unpledge_height43_ = height43;
}

// Get pledge subnode, 20211115  Liu
void PledgeCache::GetPledge(vector<Node>& pledge)
{
    pledge.clear();

    vector<Node> pledge_nodes;
    cache_mutex_.lock();
    for (const auto& id : pledge_id_)
    {
        Node node;
        if (Singleton<PeerNode>::get_instance()->find_node(id, node))
        {
            pledge_nodes.push_back(node);
        }
    }
    cache_mutex_.unlock();
    
    uint32 height = CalcNodesHeight(pledge_nodes);
    for (const auto& node : pledge_nodes)
    {
        if (node.height == height)
        {
            pledge.push_back(node);
        }
    }
}

void PledgeCache::GetUnpledge(vector<Node>& unpledge)
{
    unpledge.clear();

    vector<Node> unpledge_nodes;
    cache_mutex_.lock();
    for (const auto& id : unpledge_id_)
    {
        Node node;
        if (Singleton<PeerNode>::get_instance()->find_node(id, node))
        {
            unpledge_nodes.push_back(node);
        }
    }
    cache_mutex_.unlock();
    
    uint32 height = CalcNodesHeight(unpledge_nodes);
    for (const auto& node : unpledge_nodes)
    {
        if (node.height == height)
        {
            unpledge.push_back(node);
        }
    }
}

// Get public node, 20211112  Liu
void PledgeCache::GetPublicPledge(vector<Node>& public_pledge)
{
    public_pledge.clear();

    Node selfNode = Singleton<PeerNode>::get_instance()->get_self_node();
    vector<Node> public_nodes;
    cache_mutex_.lock();
    for (const auto& id : public_pledge_id_)
    {
        Node node;
        if (Singleton<PeerNode>::get_instance()->find_public_node(id, node))
        {
            public_nodes.push_back(node);
        }
        if (selfNode.base58address == id && selfNode.is_public_node)
        {
            public_nodes.push_back(selfNode);
        }
    }
    cache_mutex_.unlock();

    uint32 height = public_pledge_height43_;
    for (const auto& node : public_nodes)
    {
        int diff = node.height - height;
        if ((node.height == height) || (abs(diff) == 1))
        {
            public_pledge.push_back(node);
        }
    }
}

void PledgeCache::GetPublicUnpledge(vector<Node>& public_unPledge)
{
    public_unPledge.clear();
    
    Node selfNode = Singleton<PeerNode>::get_instance()->get_self_node();
    std::vector<Node> public_nodes;
    cache_mutex_.lock();
    for (const auto& id : public_unpledge_id_)
    {
        Node node;
        if (Singleton<PeerNode>::get_instance()->find_public_node(id, node))
        {
            public_nodes.push_back(node);
        }
        if (selfNode.base58address == id && selfNode.is_public_node)
        {
            selfNode = Singleton<PeerNode>::get_instance()->get_self_node();
            public_nodes.push_back(selfNode);
        }
    }
    cache_mutex_.unlock();

    uint32 height = public_unpledge_height43_;
    for (const auto& node : public_nodes)
    {
        int diff = node.height - height;
        if ((node.height == height) || (abs(diff) == 1))
        {
            public_unPledge.push_back(node);
        }
    }
}

void PledgeCache::CalcPledgePublicNodeHeight43()
{
    Node selfNode = Singleton<PeerNode>::get_instance()->get_self_node();
    vector<Node> public_nodes;
    cache_mutex_.lock();
    for (const auto& id : public_pledge_id_)
    {
        Node node;
        if (Singleton<PeerNode>::get_instance()->find_public_node(id, node))
        {
            public_nodes.push_back(node);
        }
        if (selfNode.base58address == id && selfNode.is_public_node)
        {
            public_nodes.push_back(selfNode);
        }
    }
    cache_mutex_.unlock();

    uint32 height = CalcNodesHeight(public_nodes);
    public_pledge_height43_ = height;
}

void PledgeCache::CalcUnpledgePublicNodeHeight43()
{
    Node selfNode = Singleton<PeerNode>::get_instance()->get_self_node();
    std::vector<Node> public_nodes;
    cache_mutex_.lock();
    for (const auto& id : public_unpledge_id_)
    {
        Node node;
        if (Singleton<PeerNode>::get_instance()->find_public_node(id, node))
        {
            public_nodes.push_back(node);
        }
        if (selfNode.base58address == id && selfNode.is_public_node)
        {
            selfNode = Singleton<PeerNode>::get_instance()->get_self_node();
            public_nodes.push_back(selfNode);
        }
    }
    cache_mutex_.unlock();

    uint32 height = CalcNodesHeight(public_nodes);
    public_unpledge_height43_ = height;
}

uint32 PledgeCache::CalcNodesHeight(const vector<Node>& nodes)
{
    if (nodes.empty())
    {
        return 0;
    }
    std::vector<std::uint32_t> heightlist;
    for (const auto& node : nodes)
    {
        heightlist.push_back(node.height);
    }
    std::sort(heightlist.begin(), heightlist.end());
    return heightlist[3 * heightlist.size() / 4]; //返回排序后四分之三位点的高度
}

// Get height with Mode, 20211112  Liu
uint32 PledgeCache::CalcNodesHeight1(const vector<Node>& nodes)
{
    if (nodes.empty())
    {
        return 0;
    }

    // Mode algorithm
    std::unordered_map<uint32, int> height_count;
    for (const auto& node : nodes)
    {
        auto iter = height_count.find(node.height);
        if (iter != height_count.end())
        {
            (iter->second)++;
        }
        else
        {
            height_count[node.height] = 1;
        }
    }

    std::pair<uint32, int> max_count = std::make_pair(0, 0);
    for (auto iter = height_count.begin(); iter != height_count.end(); ++iter)
    {
        if (iter->first > max_count.first && (iter->second >= max_count.second || abs(iter->second - max_count.second) < 6))
        {
            max_count = *iter;
        }
    }

    return max_count.first;
}

uint32 PledgeCache::CalcNodesHeight2(const vector<Node>& nodes)
{
    return 0;
}

void PledgeCache::ClearHeightCache()
{
    lock_guard<mutex> lock(height_nodes_mutex_);
    map_height_nodes_.clear();
}

void PledgeCache::AddHeightCache(uint32 height, const vector<Node>& nodes)
{
    lock_guard<mutex> lock(height_nodes_mutex_);
    map_height_nodes_.insert(std::make_pair(height, nodes));
}

void PledgeCache::ClearHeightUnpledgeCache()
{
    lock_guard<mutex> lock(height_unpledge_nodes_mutex_);
    map_height_unpledge_nodes_.clear();
}

void PledgeCache::AddHeightUnpledgeCache(uint32 height, const vector<Node>& nodes)
{
    lock_guard<mutex> lock(height_unpledge_nodes_mutex_);
    map_height_unpledge_nodes_.insert(std::make_pair(height, nodes));
}

bool PledgeCache::IsReasonableHeight(uint32 height1, uint32 height2)
{
    return (height1 >= height2);
}

bool PledgeCache::IsReasonableHeight(uint32 nodeHeight, uint32 subNodeHeight, uint32 height43)
{
    uint32 low43 = (height43 >= 10 ? height43 - 10 : 0);
    uint32 hight43 = height43 + 10;
    if (nodeHeight >= low43 && nodeHeight <= hight43)
    {
        if (nodeHeight >= subNodeHeight)
        {
            return true;
        }
    }
    return false;
}

bool PledgeCache::IsInit()
{
    lock_guard<mutex> lock(cache_mutex_);
    return (this->pledge_list_.size() > 0);
}

void PledgeCache::Init()
{
    this->Process();
}

void PledgeCache::Start()
{
    if (!Singleton<Config>::get_instance()->GetIsPublicNode())
    {
        return ;
    }

    this->init_timer_.AsyncOnce(1000 * 60, PledgeCache::HandleTimer, this);

    const int LOOP_TIME = 1000 * 60 * 5;
    this->timer_.AsyncLoop(LOOP_TIME, PledgeCache::HandleTimer, this);

    this->height_cache_timer_.AsyncLoop(1000 * 10, [this](){
        ClearHeightCache();
        ClearHeightUnpledgeCache();

        CalcPledgeSubnodeHeight43();
        CalcUnpledgeSubnodeHeight43();

        CalcPledgePublicNodeHeight43();
        CalcUnpledgePublicNodeHeight43();
    });    
}

void PledgeCache::Stop()
{
    if (!Singleton<Config>::get_instance()->GetIsPublicNode())
    {
        return ;
    }

    this->init_timer_.Cancel();
    this->timer_.Cancel();
}

int PledgeCache::HandleTimer(PledgeCache* cache)
{
    if (cache == nullptr)
    {
        return -1;
    }

    cache->Process();
    return 0;
}
