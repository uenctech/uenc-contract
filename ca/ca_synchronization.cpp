#include <iostream>
#include <thread>
#include <unistd.h>
#include <utility>
#include <map>
#include "ca_global.h"
#include "ca_message.h"
#include "ca_coredefs.h"
#include "ca_serialize.h"
#include "ca_transaction.h"
#include "ca_synchronization.h"
#include "ca_test.h"
#include "ca_blockpool.h"
#include "ca_hexcode.h"
#include "../include/net_interface.h"
#include "MagicSingleton.h"
#include "../common/config.h"
#include "../include/logging.h"
#include "../include/ScopeGuard.h"
#include "ca_console.h"
#include "../proto/ca_protomsg.pb.h"
#include <algorithm>
#include <tuple>
#include "../utils/string_util.h"
#include "../net/peer_node.h"
#include "../net/node_cache.h"
#include "../utils/base64.h"
#include "ca/ca_rollback.h"
#include "../utils/time_util.h"
#include "db/db_api.h"

bool SumHash(uint64_t start_height, uint64_t end_height, std::string &hash);
/* 设置获取到的其他节点的最高块信息 */ 
bool Sync::SetPotentialNodes(const std::string &id, 
							const int64_t &height, 
							const std::string &hash, 
							const std::string & forwardHash, 
							const std::string & backwardHash)
{
	std::lock_guard<std::mutex> lck(mu_potential_nodes);
    if(id.size() == 0)
    {
        return false;
    }

    // 遍历现有数据，防止重复数据写入
    uint64_t size = this->potential_nodes.size();
    for(uint64_t i = 0; i < size; i++)
    {
        if(0 == id.compare(potential_nodes[i].id))
        {
            return false;
        }
    }
    struct SyncNode data = {id, height, hash, forwardHash, backwardHash};
    this->potential_nodes.push_back(data);
    return true;
}


void Sync::SetPledgeNodes(const std::vector<std::string> & ids)
{
	std::lock_guard<std::mutex> lck(mu_get_pledge);
	std::vector<std::string> pledgeNodes = this->pledgeNodes;
	this->pledgeNodes.clear();

	std::vector<std::string> v_diff;
	std::set_difference(ids.begin(),ids.end(),pledgeNodes.begin(),pledgeNodes.end(),std::back_inserter(v_diff));

	this->pledgeNodes.assign(v_diff.begin(), v_diff.end());
}


int Sync::GetSyncInfo(std::vector<std::string> & reliables, int64_t & syncHeight)
{
	if (this->potential_nodes.size() == 0)
	{
		return -1;
	}

	std::map<int64_t, std::map<std::string, std::vector<std::string>>> height_hash_id;
	for (auto & node : potential_nodes)
	{
		std::vector<std::string> checkHashs;
		StringUtil::SplitString(node.backwardHash, checkHashs, "_");

		for (auto & hash : checkHashs)
		{
			CheckHash checkHash;
			checkHash.ParseFromString(base64Decode(hash));

			auto idIter = height_hash_id.find(checkHash.end());
			if (height_hash_id.end() != idIter)
			{
				auto hashIter = idIter->second.find(checkHash.hash());
				if (idIter->second.end() != hashIter)
				{
					hashIter->second.push_back(node.id);
				}
				else
				{
					std::map<std::string, std::vector<std::string>> & hash_id = height_hash_id[checkHash.end()];
					std::vector<std::string> ids = {node.id};
					hash_id.insert(make_pair(checkHash.hash(), ids));
				}
			}
			else
			{
				std::vector<std::string> ids = {node.id};
				std::map<std::string, std::vector<std::string>> hash_id;
				hash_id.insert(std::make_pair(checkHash.hash(), ids));
				height_hash_id.insert(make_pair(checkHash.end(), hash_id));
			}
		}
	}

	int most = SYNCNUM * 0.6;
	for (auto & i : height_hash_id)
	{
		auto hash_id = i.second;
		int64_t tmpHeight = i.first;
		if (tmpHeight > syncHeight)
		{
			int mostHashCount = 0;
			std::string mostHash;
			std::vector<std::string> ids;
			for(auto & j : hash_id)
			{
				if (j.second.size() > (size_t)mostHashCount && j.second.size() >= (size_t)most)
				{
					mostHashCount = j.second.size();
					mostHash = j.first;
					ids = j.second;
				}
			}

			if ((size_t)mostHashCount >= reliables.size())
			{
				syncHeight = tmpHeight;
				reliables = ids;
			}
		}
	}
	return 0;
}

int Sync::SyncDataFromPubNode()
{
	std::vector<Node> nodes = net_get_public_node();
	if (nodes.size() == 0)
	{
		return -1;
	}

	int i = rand() % nodes.size();

	int syncNum = Singleton<Config>::get_instance()->GetSyncDataCount();
	DataSynch(nodes[i].base58address, syncNum);

	return 0;
}

// 同步开始
void Sync::Process()
{   
	if(sync_adding)
	{
		DEBUGLOG("sync_adding...");
		return;
	}               
    DEBUGLOG("Sync block begin");
    potential_nodes.clear();
	verifying_node.id.clear();

    //连续3次查找可靠节点失败，直接向公网节点请求同步
    if (reliableCount >= 3 || rollbackCount >= 3)
    {
        SyncDataFromPubNode();
        if ( reliableCount != 0)
        {
            std::lock_guard<std::mutex> lock(reliableLock);
            reliableCount = 0;
        }

        if ( rollbackCount != 0 )
        {
            std::lock_guard<std::mutex> lock(rollbackLock);
            rollbackCount = 0;
        }
        return ;
    }

    // === 1.寻找潜在可靠节点 ===
	std::vector<Node> nodeInfos;
	if (Singleton<PeerNode>::get_instance()->get_self_node().is_public_node)
	{
		nodeInfos = Singleton<PeerNode>::get_instance()->get_nodelist();
		DEBUGLOG("public node size() = {}", nodeInfos.size());
	}
	else
	{
		nodeInfos = Singleton<NodeCache>::get_instance()->get_nodelist();
		DEBUGLOG("normal node size() = {}", nodeInfos.size());
	}
   
	DEBUGLOG("nodeInfos size() = {}", nodeInfos.size());

	std::vector<std::string> nodes;
	for (const auto & nodeInfo : nodeInfos)
	{
		nodes.push_back(nodeInfo.base58address);
	}
    if(nodes.size() == 0)
    {
        ERRORLOG("sync block not have node!!");
		return;
    }
    int nodesum = std::min(SYNCNUM, (int)nodes.size());


	uint64_t top = 0;
	{
		DBReader db_reader;
		if (DBStatus::DB_SUCCESS != db_reader.GetBlockTop(top))
		{
			ERRORLOG("db get top failed!!");
			return ;
		}
	}

    /* 随机选择节点，保证公平*/
    std::vector<std::string> sendid = randomNode(nodesum);
   	DEBUGLOG("sync send is size : {}", sendid.size());

	string proxyid = Singleton<Config>::get_instance()->GetProxyID();
	Type type = Singleton<Config>::get_instance()->GetProxyTypeStatus();
	bool ispublicid = Singleton<Config>::get_instance()->GetIsPublicNode();
	if((type == kMANUAL) && (!proxyid.empty()) &&(!ispublicid))
	{
		SendSyncGetnodeInfoReq(proxyid, top);
	}
	else if((type == kAUTOMUTIC)  && (!ispublicid))
	{
		g_localnode.clear();
		vector<Node> allnode = Singleton<PeerNode>::get_instance()->get_nodelist();//打洞的还需要去除掉
		string self_id =  Singleton<PeerNode>::get_instance()->get_self_id().c_str();
		vector<Node> localnode;
		vector<Node> idsequence;
		
		for(auto&item :allnode)
		{
			if(item.conn_kind != BYHOLE && (!item.is_public_node ))
			{
				localnode.push_back(item);
			}
		}

		Node self = Singleton<PeerNode>::get_instance()->get_self_node();	
		if(!self.is_public_node)
		{
			localnode.push_back(self);
		}

		std::sort(localnode.begin(),localnode.end(),Compare(true));
		localnode.erase(unique(localnode.begin(), localnode.end()), localnode.end());

		uint64_t height = localnode[0].height;
		for(auto &item:localnode)
		{
			if(item.height == height)
			{
				idsequence.push_back(item);
			}
		}
		std::sort(idsequence.begin(),idsequence.end(),Compare(false));

		if(self_id.compare(idsequence[0].base58address))
		{
			g_localnode.push_back(idsequence[0]);
			SendSyncGetnodeInfoReq(g_localnode[0].base58address, top);
			DEBUGLOG("potential g_localnode[0].ip = {} g_localnode[0].height = {}", IpPort::ipsz(g_localnode[0].listen_ip), g_localnode[0].height);
		}	
	}
	
	for(auto& id : sendid)
	{ 
		SendSyncGetnodeInfoReq(id, top); //随机节点节点信息请求
	}
	
    sleep(3);
    if(potential_nodes.size() == 0)
    {
        ERRORLOG("potential_nodes == 0");
		return;
    }

	std::vector<std::string> reliables;
	int64_t syncHeight = 0;
	if ( 0 != GetSyncInfo(reliables, syncHeight) )
	{
		return;
	}
	
	if (reliables.size() <= 1)
	{
		std::lock_guard<std::mutex> lock(reliableLock);
		reliableCount++;
		return ;
	}
	else
	{
		std::lock_guard<std::mutex> lock(reliableLock);
		reliableCount = 0;
	}
	
	int sync_cnt = Singleton<Config>::get_instance()->GetSyncDataCount();
	int syncNum = std::min(syncHeight - (int64_t)top, (int64_t)sync_cnt);
	if( syncNum < 0)
	{
		ERRORLOG("syncNum <= 0:{}", syncNum);
		return;
	}
	
	for(auto const &i :reliables)
	{
		if((type == kMANUAL) && (!proxyid.empty()) && (!ispublicid))
		{
			if(!proxyid.compare(i))
			{
				ca_console infoColor(kConsoleColor_Red, kConsoleColor_Black, true);
				DataSynch(proxyid, syncNum);
				DEBUGLOG("proxyid = {}", proxyid);
				return;
			}
		}
		else if ((type == kAUTOMUTIC)  && (!ispublicid))
		{
			if(g_localnode.size() == 0)
			{
				break;
			}
			string autoid = g_localnode[0].base58address;
			if (!autoid.compare(i))
			{
				ca_console infoColor(kConsoleColor_Red, kConsoleColor_Black, true);
				DEBUGLOG("autoid.ip = {}, autoid.height = {}", IpPort::ipsz(g_localnode[0].listen_ip), g_localnode[0].height);
				DataSynch(autoid, syncNum);
				return;
			}
		}
	}

	int i = rand() % reliables.size();
	DataSynch(reliables[i], syncNum);
}


/* 发起同步请求*/
bool Sync::DataSynch(std::string id, const int syncNum)
{
    if(0 == id.size())
    {
		ERRORLOG("DataSynch:id is empty!!");
        return false;
    }
    SendSyncBlockInfoReq(id, syncNum);
    return true;
}

//============区块同步交互协议================

void SendSyncGetnodeInfoReq(std::string id, uint64_t height)
{
	if(id.size() == 0)
	{
		ERRORLOG("Parameters invalid!!"); 
		return;
	}

    SyncGetnodeInfoReq getBestchainInfoReq;
    SetSyncHeaderMsg<SyncGetnodeInfoReq>(getBestchainInfoReq);
	getBestchainInfoReq.set_height(height);
	getBestchainInfoReq.set_syncnum( Singleton<Config>::get_instance()->GetSyncDataCount() );
	net_send_message<SyncGetnodeInfoReq>(id, getBestchainInfoReq, net_com::Compress::kCompress_True);
}

int SendVerifyPledgeNodeReq(std::vector<std::string> ids)
{
	if (ids.size() == 0)
	{
		return -1;
	}

	SyncVerifyPledgeNodeReq syncVerifyPledgeNodeReq;
	SetSyncHeaderMsg<SyncVerifyPledgeNodeReq>(syncVerifyPledgeNodeReq);

	for (auto id : ids)
	{
		syncVerifyPledgeNodeReq.add_ids(id);
	}

	for (auto id : ids)
	{
		net_send_message<SyncVerifyPledgeNodeReq>(id, syncVerifyPledgeNodeReq, net_com::Compress::kCompress_True);
	}

	return 0;
}

int HandleSyncVerifyPledgeNodeReq( const std::shared_ptr<SyncVerifyPledgeNodeReq>& msg, const MsgData& msgdata )
{
	// 判断版本是否兼容
	SyncHeaderMsg * HeaderMsg= msg->mutable_syncheadermsg();
	if(0 != Util::IsVersionCompatible(HeaderMsg->version()))
	{
		ERRORLOG("HandleSyncGetnodeInfoReq IsVersionCompatible");
		return 0;
	}
	
	if (msg->ids_size() == 0)
	{
		ERRORLOG("(HandleSyncVerifyPledgeNodeReq) msg->ids_size() == 0");
		return 0;
	}
    DBReader db_reader;

	std::vector<std::string> addrs;
	if ( 0 != db_reader.GetPledgeAddress(addrs) )
	{
		ERRORLOG("(HandleSyncVerifyPledgeNodeReq) GetPledgeAddress failed!");
		return 0;
	}

	SyncVerifyPledgeNodeAck syncVerifyPledgeNodeAck;

	std::map<std::string, std::string> idBase58s = net_get_node_ids_and_base58address();
	for (auto & idBase58 : idBase58s)
	{
		auto iter = find(addrs.begin(), addrs.end(), idBase58.second);
		if (iter != addrs.end())
		{
			uint64_t amount = 0;
			SearchPledge(idBase58.second, amount);

			if (amount >= g_TxNeedPledgeAmt)
			{
				syncVerifyPledgeNodeAck.add_ids(idBase58.first);
			}
		}
	}

	if (syncVerifyPledgeNodeAck.ids_size() == 0)
	{
		ERRORLOG("(HandleSyncVerifyPledgeNodeReq) ids == 0!");
		return 0;
	}

	SyncHeaderMsg * headerMsg = msg->mutable_syncheadermsg();
	net_send_message<SyncVerifyPledgeNodeAck>(headerMsg->id(), syncVerifyPledgeNodeAck, net_com::Compress::kCompress_True);
    return 0;
}

int HandleSyncVerifyPledgeNodeAck( const std::shared_ptr<SyncVerifyPledgeNodeAck>& msg, const MsgData& msgdata )
{
	if (msg->ids_size() == 0)
	{
		return 0;
	}

	std::vector<std::string> ids;
	for (int i = 0; i < msg->ids_size(); ++i)
	{
		ids.push_back(msg->ids(i));
	}

	g_synch->SetPledgeNodes(ids);
    return 0;
}

void SendSyncGetPledgeNodeReq(std::string id)
{
	if(id.size() == 0)
	{
		return;
	}

    SyncGetPledgeNodeReq getBestchainInfoReq;
    SetSyncHeaderMsg<SyncGetPledgeNodeReq>(getBestchainInfoReq);
	net_send_message<SyncGetPledgeNodeReq>(id, getBestchainInfoReq, net_com::Compress::kCompress_True);
}

int HandleSyncGetPledgeNodeReq( const std::shared_ptr<SyncGetPledgeNodeReq>& msg, const MsgData& msgdata )
{
	// 判断版本是否兼容
	SyncHeaderMsg * HeaderMsg= msg->mutable_syncheadermsg();
	if( 0 != Util::IsVersionCompatible( HeaderMsg->version() ) )
	{
		ERRORLOG("HandleSyncGetnodeInfoReq IsVersionCompatible");
		return 0;
	}
	
	SyncGetPledgeNodeAck syncGetPledgeNodeAck;

    DBReader db_reader;

	std::vector<string> addresses;
	db_reader.GetPledgeAddress(addresses);

	std::map<std::string, std::string> idBase58s = net_get_node_ids_and_base58address();
	for (auto idBase58 : idBase58s)
	{
		auto iter = find(addresses.begin(), addresses.end(), idBase58.second);
		if (iter != addresses.end())
		{
			uint64_t amount = 0;
			SearchPledge(idBase58.second, amount);

			if (amount >= g_TxNeedPledgeAmt)
			{
				syncGetPledgeNodeAck.add_ids(idBase58.first);
			}
		}
	}
	SyncHeaderMsg * headerMsg = msg->mutable_syncheadermsg();
	net_send_message<SyncGetPledgeNodeAck>(headerMsg->id(), syncGetPledgeNodeAck, net_com::Compress::kCompress_True);
    return 0;
}

int HandleSyncGetPledgeNodeAck( const std::shared_ptr<SyncGetPledgeNodeAck>& msg, const MsgData& msgdata )
{
	std::vector<std::string> ids;
	for (int i = 0; i < msg->ids_size(); ++i)
	{
		ids.push_back(msg->ids(i));
	}

	g_synch->SetPledgeNodes(ids);
    return 0;
}

// 验证潜在可靠节点请求
void SendVerifyReliableNodeReq(std::string id, int64_t height)
{
	if(id.size() == 0)
	{
		return;
	}

    VerifyReliableNodeReq verifyReliableNodeReq;
	SetSyncHeaderMsg<VerifyReliableNodeReq>(verifyReliableNodeReq);

    verifyReliableNodeReq.set_height(height);

	net_send_message<VerifyReliableNodeReq>(id, verifyReliableNodeReq, net_com::Compress::kCompress_True);
}

int get_check_hash(const uint32_t begin, const uint32_t end, const uint32_t end_height, std::vector<CheckHash> & vCheckHash)
{
	if (begin > end)
	{
		return -1;
	}

	std::vector<std::tuple<int, int>> check_range; 
	for(int i = begin; i < (int)end; i++)
	{
		check_range.push_back(std::make_tuple(i*CHECK_HEIGHT, (i+1)*CHECK_HEIGHT));
	}
	check_range.push_back(std::make_tuple(end*CHECK_HEIGHT, end_height));

	DBReader db_reader;
    for(auto i: check_range)
    {
		int begin = std::get<0>(i);
		int end = std::get<1>(i);
		std::string str;

		std::vector<std::string> hashs;
		for(auto j = begin; j <= end; j++)
		{
			std::vector<std::string> vBlockHashs;
			db_reader.GetBlockHashsByBlockHeight(j, vBlockHashs); //j height
			std::sort(vBlockHashs.begin(), vBlockHashs.end());
			hashs.push_back(StringUtil::concat(vBlockHashs, "_"));
		}
		CheckHash checkhash;
		checkhash.set_begin(begin);
		checkhash.set_end(end);
		if(hashs.size() > 0)
		{
			std::string all_hash = getsha256hash(StringUtil::concat(hashs, "_"));
			checkhash.set_hash(all_hash.substr(0,HASH_LEN));
		}
		vCheckHash.push_back(checkhash);
    }
	return 0;
}

std::vector<CheckHash> get_check_hash_backward(const int height, const int num)
{
    DBReader db_read;
	std::vector<CheckHash> v_checkhash;

	uint64_t top = 0;
	if (db_read.GetBlockTop(top))
	{
		return v_checkhash;
	}

	int check_begin = height / CHECK_HEIGHT;
	int end_height = std::min((height + num), (int)top);
	int check_end = end_height / CHECK_HEIGHT;

	get_check_hash(check_begin, check_end, end_height, v_checkhash);
	return v_checkhash;
}

std::vector<CheckHash> get_check_hash_forward(int height)
{
	std::vector<CheckHash> v_checkhash;
	if(height < 1) 
	{
		return v_checkhash;
	}
	int check_end = (height / CHECK_HEIGHT);        // 5
	int check_num = std::min(check_end, (CHECKNUM-1) ); //5
	int check_begin = check_end - check_num; //0

	get_check_hash(check_begin, check_end, height, v_checkhash);
	return v_checkhash;
}

void  SendSyncBlockInfoReq(std::string id, const int syncNum)
{
	uint64_t blockHeight = 0;
	{
		DBReader db_reader;
		db_reader.GetBlockTop(blockHeight);
	}	
	SyncBlockInfoReq syncBlockInfoReq;
	SetSyncHeaderMsg<SyncBlockInfoReq>(syncBlockInfoReq);

	syncBlockInfoReq.set_height(blockHeight);
	int sync_cnt = Singleton<Config>::get_instance()->GetSyncDataCount();
	syncBlockInfoReq.set_max_num(sync_cnt);
	syncBlockInfoReq.set_max_height(blockHeight + syncNum);
	
	std::vector<CheckHash> v_checkhash = get_check_hash_forward(blockHeight);
	for(auto i:v_checkhash)
	{
		CheckHash *checkhash = syncBlockInfoReq.add_checkhash();
		checkhash->set_begin(i.begin());
		checkhash->set_end(i.end());
		checkhash->set_hash(i.hash());
	}
	net_send_message<SyncBlockInfoReq>(id, syncBlockInfoReq, net_com::Compress::kCompress_True);
}


std::vector<std::string> CheckHashToString(const std::vector<CheckHash> & v)
{
	std::vector<std::string> vRet;
	for (const auto & i : v)
	{
		vRet.push_back(base64Encode(i.SerializeAsString()));
	}

	return vRet;
}


int HandleSyncGetnodeInfoReq( const std::shared_ptr<SyncGetnodeInfoReq>& msg, const MsgData& msgdata )
{
	// 判断版本是否兼容
	SyncHeaderMsg * HeaderMsg= msg->mutable_syncheadermsg();
	if( 0 != Util::IsVersionCompatible( HeaderMsg->version() ) )
	{
		ERRORLOG("HandleSyncGetnodeInfoReq IsVersionCompatible");
		return 0;
	}

	DBReader db_reader;
	uint64_t blockHeight;
	std::string blockHash;
	if (DBStatus::DB_SUCCESS != db_reader.GetBlockTop(blockHeight))
	{
		ERRORLOG("HandleSyncGetnodeInfoReq GetBlockTop");
		return 0;
	}

	if (DBStatus::DB_SUCCESS != db_reader.GetBestChainHash(blockHash))
	{
		ERRORLOG("HandleSyncGetnodeInfoReq GetBestChainHash");
		return 0;
	}

	SyncGetnodeInfoAck syncGetnodeInfoAck;
	SetSyncHeaderMsg<SyncGetnodeInfoAck>(syncGetnodeInfoAck);

	std::vector<CheckHash> vCheckHashForward = get_check_hash_forward(msg->height());
	std::vector<CheckHash> vCheckHashbackward = get_check_hash_backward(msg->height(), msg->syncnum());
	std::vector<std::string> vStrForward = CheckHashToString(vCheckHashForward);
	std::vector<std::string> vStrBackward = CheckHashToString(vCheckHashbackward);

	syncGetnodeInfoAck.set_height(blockHeight);
	syncGetnodeInfoAck.set_hash(blockHash.substr(0,HASH_LEN));
	syncGetnodeInfoAck.set_checkhashforward( StringUtil::concat(vStrForward, "_") );
	syncGetnodeInfoAck.set_checkhashbackward( StringUtil::concat(vStrBackward, "_") );

	SyncHeaderMsg * headerMsg = msg->mutable_syncheadermsg();
	net_send_message<SyncGetnodeInfoAck>(headerMsg->id(), syncGetnodeInfoAck, net_com::Compress::kCompress_True);
    return 0;
}

int HandleSyncGetnodeInfoAck( const std::shared_ptr<SyncGetnodeInfoAck>& msg, const MsgData& msgdata )
{
	// 判断版本是否兼容
	SyncHeaderMsg * pSyncHeaderMsg= msg->mutable_syncheadermsg();
	if( 0 != Util::IsVersionCompatible( pSyncHeaderMsg->version() ) )
	{
		ERRORLOG("HandleSyncGetnodeInfoAck IsVersionCompatible");
		return 0;
	}

	g_synch->SetPotentialNodes( pSyncHeaderMsg->id(), msg->height(), msg->hash(), msg->checkhashforward(), msg->checkhashbackward() );
    return 0;
}

int HandleVerifyReliableNodeReq( const std::shared_ptr<VerifyReliableNodeReq>& msg, const MsgData& msgdata )
{
    DBReader db_reader;

	std::string ownid = net_get_self_node_id();

	VerifyReliableNodeAck verifyReliableNodeAck;

	verifyReliableNodeAck.set_id(ownid);
	verifyReliableNodeAck.set_height(msg->height());

    std::string hash;
    db_reader.GetBlockHashByBlockHeight(msg->height(), hash);
    if(hash.size() == 0)
    {
        uint64_t top = 0;
        db_reader.GetBlockTop(top);
        verifyReliableNodeAck.set_height(top);
    }
	verifyReliableNodeAck.set_hash(hash.substr(0,HASH_LEN));
	SyncHeaderMsg * headerMsg = msg->mutable_syncheadermsg();
	net_send_message<VerifyReliableNodeAck>(headerMsg->id(), verifyReliableNodeAck, net_com::Compress::kCompress_True);
    return 0;
}


int HandleSyncBlockInfoReq( const std::shared_ptr<SyncBlockInfoReq>& msg, const MsgData& msgdata )
{
	SyncHeaderMsg * pSyncHeaderMsg = msg->mutable_syncheadermsg();
	
	// 判断版本是否兼容
	if( 0 != Util::IsVersionCompatible( pSyncHeaderMsg->version() ) )
	{
		ERRORLOG("HandleSyncBlockInfoReq:IsVersionCompatible");
		return 0;
	}

	uint64_t ownblockHeight = 0;
	int64_t other_height = msg->height();
	{
		DBReader db_reader;
		db_reader.GetBlockTop(ownblockHeight);
	}

	uint64_t max_height = msg->max_height();           // 请求节点要同步的最大高度
	uint64_t end = std::min(ownblockHeight, max_height);    // 同步结束高度
	
	if(ownblockHeight < msg->height())
	{
		DEBUGLOG("request height is over me,not sync.");
		return 0;
	}

	SyncBlockInfoAck syncBlockInfoAck;
	SetSyncHeaderMsg<SyncBlockInfoAck>(syncBlockInfoAck);

	std::vector<CheckHash> v_checkhash = get_check_hash_forward(other_height);
	if((int)v_checkhash.size() == (int)msg->checkhash_size())
	{
		std::vector<CheckHash> v_invalid_checkhash;
		for (int i = 0; i < msg->checkhash_size(); i++) 
		{
			const CheckHash& checkhash = msg->checkhash(i);
			if(checkhash.begin() == v_checkhash[i].begin() 
				&& checkhash.end() == v_checkhash[i].end() 
				&& checkhash.hash() != v_checkhash[i].hash()
			)
			{
				v_invalid_checkhash.push_back(checkhash);
				CheckHash *invalid_checkhash = syncBlockInfoAck.add_invalid_checkhash();
				invalid_checkhash->set_begin(checkhash.begin());
				invalid_checkhash->set_end(checkhash.end());
			}
		}
	}
	else
	{
		ERRORLOG("checkhash.size not equal!! form:{} me:{}",(int)msg->checkhash_size(), (int)v_checkhash.size());
	}

	int beginHeight = 0;
	if (syncBlockInfoAck.invalid_checkhash_size() != 0)
	{
		beginHeight = syncBlockInfoAck.invalid_checkhash(0).begin();
	}
	else
	{
		beginHeight = msg->height();
	}
	
	if(ownblockHeight > msg->height() )
	{
		std::string blocks = get_blkinfo_ser(beginHeight, end, msg->max_num());
		syncBlockInfoAck.set_blocks(blocks);
		syncBlockInfoAck.set_poolblocks(MagicSingleton<BlockPoll>::GetInstance()->GetBlock());
	}

	SyncHeaderMsg * headerMsg = msg->mutable_syncheadermsg();
	net_send_message<SyncBlockInfoAck>(headerMsg->id(), syncBlockInfoAck, net_com::Compress::kCompress_True);	
    return 0;
}


bool isRunningSyncBlockInfoAck = false;
std::mutex mutexRunningSyncBlockInfoAck;
int HandleSyncBlockInfoAck( const std::shared_ptr<SyncBlockInfoAck>& msg, const MsgData& msgdata )
{
	if (isRunningSyncBlockInfoAck == true)
	{
		ERRORLOG("isRunningSyncBlockInfoAck is true");
		return 0;
	}

	{
		std::lock_guard<std::mutex> lck(mutexRunningSyncBlockInfoAck);

		if (isRunningSyncBlockInfoAck == true)
		{
			ERRORLOG("isRunningSyncBlockInfoAck is true");
			return 0;
		}

		isRunningSyncBlockInfoAck = true;
	}

	ON_SCOPE_EXIT{
		isRunningSyncBlockInfoAck = false;
	};

	SyncHeaderMsg * pSyncHeaderMsg = msg->mutable_syncheadermsg();
	string id = pSyncHeaderMsg->id();
	Node node;
	Singleton<PeerNode>::get_instance()->find_node(id, node);
	DEBUGLOG("HandleSyncBlockInfoAck id = {}. ip = {}, syncBlockInfoReq height ={}", id, IpPort::ipsz(node.listen_ip), node.height);
	// 判断版本是否兼容
	if( 0 != Util::IsVersionCompatible( pSyncHeaderMsg->version() ) )
	{
		ERRORLOG("HandleSyncBlockInfoAck:IsOverIt");
		return 0;
	}

	if(MagicSingleton<BlockPoll>::GetInstance()->GetSyncBlocks().size() > 0)
	{
		ERRORLOG("HandleSyncBlockInfoAck:SyncBlocks not empty!"); 
		return 0;
	}

	DBReader db_reader;

	SyncHeaderMsg * headerMsg = msg->mutable_syncheadermsg();
	for (int i = 0; i < msg->invalid_checkhash_size(); i++) 
	{
		const CheckHash& checkhash = msg->invalid_checkhash(i);
		DEBUGLOG("begin:{}, end:{}", checkhash.begin(), checkhash.end());
 
		SyncLoseBlockReq syncLoseBlockReq;
		SetSyncHeaderMsg<SyncLoseBlockReq>(syncLoseBlockReq);
		syncLoseBlockReq.set_begin(checkhash.begin());
		syncLoseBlockReq.set_end(checkhash.end());

		std::vector<std::string> hashs;
		for(auto i = checkhash.begin(); i <= checkhash.end(); i++)
		{
			std::vector<std::string> vBlockHashs;
			db_reader.GetBlockHashsByBlockHeight(i, vBlockHashs); //i height
			std::for_each(vBlockHashs.begin(), vBlockHashs.end(),
				 [](std::string &s){ 
					s = s.substr(0,HASH_LEN);
					}
			);
			
			hashs.push_back(StringUtil::concat(vBlockHashs, "_"));
		}
		syncLoseBlockReq.set_all_hash(StringUtil::concat(hashs, "_"));
		net_send_message<SyncLoseBlockReq>(headerMsg->id(), syncLoseBlockReq, net_com::Compress::kCompress_True);	
		break;
	}

	//加块
	std::string blocks = msg->blocks();
	std::string poolblocks = msg->poolblocks();
	std::vector<std::string> v_blocks;
	std::vector<std::string> v_poolblocks;
	SplitString(blocks, v_blocks, "_");
	SplitString(poolblocks, v_poolblocks, "_");

	if(msg->invalid_checkhash_size() > 0 )
	{
		int begin = msg->invalid_checkhash(0).begin() == 0 ? 1 : msg->invalid_checkhash(0).begin();
		g_synch->conflict_height = begin;
		// sleep(5);
	}

	for(size_t i = 0; i < v_blocks.size(); i++)
	{
		if(0 != SyncData(v_blocks[i],true))
		{
			return 0;
		}
	}	
	for(size_t i = 0; i < v_poolblocks.size(); i++)
	{
		if(0 != SyncData(v_poolblocks[i]))
		{
			return 0;
		}
	}	
	sleep(1);
	g_synch->conflict_height = -1;
    return 0;
}


int HandleSyncLoseBlockReq( const std::shared_ptr<SyncLoseBlockReq>& msg, const MsgData& msgdata )
{
	uint64_t begin = msg->begin();
	uint64_t end = msg->end();
	std::string all_hash = msg->all_hash();

	std::vector<std::string> v_hashs;
	SplitString(all_hash, v_hashs, "_");
	
	DBReader db_reader;

	std::vector<std::string> ser_block;
	for(auto i = begin; i <= end; i++)
	{
		std::vector<std::string> vBlockHashs;
		if (DBStatus::DB_SUCCESS != db_reader.GetBlockHashsByBlockHeight(i, vBlockHashs))
		{
			return 0;
		}
		
		for(auto hash : vBlockHashs)
		{
			string strBlock;
			if (DBStatus::DB_SUCCESS != db_reader.GetBlockByBlockHash(hash, strBlock) ) 
			{
				return 0;
			}
			ser_block.push_back(Str2Hex(strBlock));
		}
	}
	SyncLoseBlockAck syncLoseBlockAck;
	syncLoseBlockAck.set_blocks(StringUtil::concat(ser_block, "_"));
	SyncHeaderMsg * headerMsg = msg->mutable_syncheadermsg();
	net_send_message<SyncLoseBlockAck>(headerMsg->id(), syncLoseBlockAck, net_com::Compress::kCompress_True);	
    return 0;
}

bool isRunningSyncLoseBlockAck = false;
std::mutex mutexSyncLoseBlockAck;
int HandleSyncLoseBlockAck( const std::shared_ptr<SyncLoseBlockAck>& msg, const MsgData& msgdata )
{
	if (isRunningSyncLoseBlockAck == true)
	{
		ERRORLOG("isRunningSyncLoseBlockAck is true");
		return 0;
	}

	{
		std::lock_guard<std::mutex> lck(mutexSyncLoseBlockAck);

		if (isRunningSyncLoseBlockAck == true)
		{
			ERRORLOG("isRunningSyncLoseBlockAck is true");
			return 0;
		}

		isRunningSyncLoseBlockAck = true;
	}

	ON_SCOPE_EXIT{
		isRunningSyncLoseBlockAck = false;
	};
	
	if(MagicSingleton<BlockPoll>::GetInstance()->GetSyncBlocks().size() > 0)
	{
		return 0;
	}

	std::string blocks = msg->blocks();
	std::vector<std::string> v_blocks;
	SplitString(blocks, v_blocks, "_");

	for(size_t i = 0; i < v_blocks.size(); i++)
	{
		if(0 != SyncData(v_blocks[i],true))
		{
			return 0;
		}
	}
    return 0;
}

bool IsOverIt(int64_t height)
{
    DBReader db_read;
	uint64_t ownblockHeight;
    if (DBStatus::DB_SUCCESS != db_read.GetBlockTop(ownblockHeight))
	{
        return false;
    }
	if(ownblockHeight > height)
	{
		return true;
	}
	return false;
}

std::string get_blkinfo_ser(int64_t begin, int64_t end, int64_t max_num)
{
	max_num = max_num > SYNC_NUM_LIMIT ? SYNC_NUM_LIMIT : max_num;
    std::string bestblockHash;
	DBReader db_reader;

	begin = begin < 0 ? 0 : begin;
	end = end < 0 ? 0 : end;
	uint64_t top = 0;
	if(DBStatus::DB_SUCCESS != db_reader.GetBlockTop(top))
	{
		return std::string();
	}
	begin = begin > top ? top : begin;
	end = end > top ? top : end;

	std::vector<std::string> ser_block;
	int block_num = 0;
	for (auto i = begin; i <= end; i++) 
	{
		if(block_num >= max_num)
		{
			break;
		}
        std::vector<std::string> vBlockHashs;
        db_reader.GetBlockHashsByBlockHeight(i, vBlockHashs);
		block_num += vBlockHashs.size();
        for (auto hash : vBlockHashs) 
		{
            string strHeader;
            db_reader.GetBlockByBlockHash(hash, strHeader);
            ser_block.push_back(Str2Hex(strHeader));
		}
	}
    
    return StringUtil::concat(ser_block, "_");
}

std::vector<std::string> get_blkinfo(int64_t begin, int64_t end, int64_t max_num)
{
	max_num = max_num > SYNC_NUM_LIMIT ? SYNC_NUM_LIMIT : max_num;
    std::string bestblockHash;
    DBReader db_reader;
	begin = begin < 0 ? 0 : begin;
	end = end < 0 ? 0 : end;
	uint64_t top;
    db_reader.GetBlockTop(top);
	begin = begin > top ? top : begin;
	end = end > top ? top : end;

	std::vector<std::string> v_blocks;
	int block_num = 0;
	for (auto i = begin; i <= end; i++) 
	{
		if(block_num >= max_num)
		{
			break;
		}
        std::vector<std::string> vBlockHashs;
        db_reader.GetBlockHashsByBlockHeight(i, vBlockHashs);
		block_num += vBlockHashs.size();
        for (auto hash : vBlockHashs) 
		{
            string strHeader;
            db_reader.GetBlockByBlockHash(hash, strHeader);
            v_blocks.push_back(strHeader);
		}
	}
    
    return v_blocks;
}

int SyncData(std::string &headerstr, bool isSync)
{
	headerstr = Hex2Str(headerstr);
	CBlock cblock;
	if(cblock.ParseFromString(headerstr))
    {
        MagicSingleton<BlockPoll>::GetInstance()->Add(Block(cblock,isSync));
    }
	return 0;
}





