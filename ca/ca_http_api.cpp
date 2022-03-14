#include "ca_http_api.h"
#include "ca_global.h"
#include "ca_transaction.h"
#include "Crypto_ECDSA.h"
#include "../net/peer_node.h"
#include "../utils/singleton.h"
#include "../utils/json.hpp"
#include "ca_synchronization.h"
#include "../net/httplib.h"
#include "../utils/string_util.h"
#include "../utils/base64.h"
#include "block.pb.h"
#include "MagicSingleton.h"
#include <algorithm>
#include <string>
#include "../include/ScopeGuard.h"
#include "ca_txhelper.h"
#include "../include/net_interface.h"
#include "../net/net_api.h"
#include "ca_base64.h"
#include "ca_message.h"
#include <sstream>
#include "ca_MultipleApi.h"
#include "ca_jcAPI.h"
#include "ca_txvincache.h"
#include "ca_txfailurecache.h"
#include "ca_global.h"
#include "../net/global.h"
#include "../utils/string_util.h"
#include "ca_trans_utils.h"
#include "../net/node_cache.h"
#include <cctype>//toupper/tolower
#include <algorithm>//transform
#include "db/db_api.h"
#include "db/cache.h"
#include "ca/ca_black_list_cache.h"


#include "wasm/types/name.hpp"
#include "wasm/datastream.hpp"
#include "wasm/wasm_constants.hpp"
#include "wasm/contract_tools.hpp"
#include "wasm/abi_serializer.hpp"
#include "wasm/wasm_context.hpp"

#include <google/protobuf/util/json_util.h>
using namespace google::protobuf;

void ca_register_http_callbacks()
{
    HttpServer::registerCallback("/", api_jsonrpc);
    HttpServer::registerCallback("/block", api_print_block);
    HttpServer::registerCallback("/info", api_info);
    HttpServer::registerCallback("/node_cache", api_node_cache);
    HttpServer::registerCallback("/info_queue", api_info_queue);
    HttpServer::registerCallback("/get_block", api_get_block);
    HttpServer::registerCallback("/get_block_hash", api_get_block_hash);
    HttpServer::registerCallback("/get_block_by_hash", api_get_block_by_hash);
    HttpServer::registerCallback("/get_tx_owner", api_get_tx_owner);
    HttpServer::registerCallback("/test_create_multi_tx", test_create_multi_tx);
    HttpServer::registerCallback("/get_db_key", api_get_db_key);
    HttpServer::registerCallback("/get_gas", api_get_gas);
    HttpServer::registerCallback("/add_block_callback", add_block_callback_test);
    HttpServer::registerCallback("/cache_info", api_cache_info);
    HttpServer::registerCallback("/black_list", api_black_list);

	HttpServer::registerCallback("/pub", api_pub);
	
    //json rpc=========
    HttpServer::registerJsonRpcCallback("jsonrpc_test", jsonrpc_test);
    HttpServer::registerJsonRpcCallback("get_height", jsonrpc_get_height);
    HttpServer::registerJsonRpcCallback("get_basic_info", jsonrpc_get_basic_info);
    HttpServer::registerJsonRpcCallback("get_balance", jsonrpc_get_balance);
    HttpServer::registerJsonRpcCallback("get_txids_by_height", jsonrpc_get_txids_by_height);
    HttpServer::registerJsonRpcCallback("get_tx_by_txid", jsonrpc_get_tx_by_txid);
    HttpServer::registerJsonRpcCallback("get_tx_by_txid_and_nodeid", jsonrpc_get_tx_by_txid_and_nodeid);
    HttpServer::registerJsonRpcCallback("create_tx_message", jsonrpc_create_tx_message);
    HttpServer::registerJsonRpcCallback("send_tx", jsonrpc_send_tx);
    HttpServer::registerJsonRpcCallback("get_avg_fee", jsonrpc_get_avg_fee);
    HttpServer::registerJsonRpcCallback("generate_wallet", jsonrpc_generate_wallet);
    HttpServer::registerJsonRpcCallback("generate_sign", jsonrpc_generate_sign);
    HttpServer::registerJsonRpcCallback("send_multi_tx", jsonrpc_send_multi_tx);
    HttpServer::registerJsonRpcCallback("get_pending_transaction", jsonrpc_get_pending_transaction);
    HttpServer::registerJsonRpcCallback("get_failure_transaction", jsonrpc_get_failure_transaction);
    HttpServer::registerJsonRpcCallback("get_block_info_list", jsonrpc_get_block_info_list);
    HttpServer::registerJsonRpcCallback("confirm_transaction", jsonrpc_confirm_transaction);
    HttpServer::registerJsonRpcCallback("deploy_contract",jsonrpc_deploy_contract);
    HttpServer::registerJsonRpcCallback("execute_contract",jsonrpc_execute_contract);
    HttpServer::registerJsonRpcCallback("execute_contract_rpc",jsonrpc_execute_contract_rpc);
    HttpServer::registerJsonRpcCallback("get_db_prefix_key",jsonrpc_get_db_prefix_key);
    HttpServer::registerJsonRpcCallback("get_contract_data",jsonrpc_get_contract_data);
    HttpServer::registerJsonRpcCallback("get_pledge_redeem_tx_by_addr", jsonrpc_get_pledge_redeem_tx_by_addr);
    HttpServer::registerJsonRpcCallback("get_sign_tx_by_addr_and_height", jsonrpc_get_sign_tx_by_addr_and_height);
    HttpServer::registerJsonRpcCallback("get_tx_by_addr_and_height", jsonrpc_get_tx_by_addr_and_height);
    HttpServer::registerJsonRpcCallback("get_utxo", jsonrpc_get_utxo);

    //启动http服务
    HttpServer::start();
}

void add_block_callback_test(const Request &req, Response &res)
{
    DEBUGLOG("Receive callback request from Client: {}", req.body);
    res.set_content(req.body, "text/plain"); // "application/json"
}

nlohmann::json jsonrpc_test(const nlohmann::json &param)
{
    std::string param1 = param["param1"].get<std::string>();
    nlohmann::json ret;
    ret["result"]["echo param"] = param1;
    return ret;
}

//-------
void api_pub(const Request & req, Response & res)
{
	std::ostringstream oss;
    oss << "queue_read:" << global::queue_read.msg_queue_.size() << std::endl;
    oss << "queue_work:" << global::queue_work.msg_queue_.size() << std::endl;
    oss << "queue_write:" << global::queue_write.msg_queue_.size() << std::endl;
    oss << "\n"
        << std::endl;

    double total = .0f;
    oss << "------------------------------------------" << std::endl;
    for (auto &item : global::reqCntMap)
    {
        total += (double)item.second.second;//数据大小
        oss.precision(3);//保留3位小数
		// 数据的类型					调用次数								 转换MB
        oss << item.first << ": " << item.second.first << " size: " << (double)item.second.second / 1024 / 1024 << " MB" << std::endl;
    }
    oss << "------------------------------------------" << std::endl;
    oss << "Total: " << total / 1024 / 1024 << " MB" << std::endl;//总共的大小

	oss << std::endl;
	oss << std::endl;
	
	oss << "amount----------:" << std::endl;
	for (auto &i : g_AccountInfo.AccountList)
	{
		uint64_t amount = 0;
		GetBalanceByUtxo(i.first,amount);
		oss << i.first + ":" + std::to_string(amount) << std::endl;
		// std::string strPub;
		// g_AccountInfo.GetPubKeyStr(i.first.c_str(), strPub);
		// oss << base64Encode(strPub) << std::endl;
	}
	oss << "\n"
		<< std::endl;

	std::vector<Node> pubNodeList = Singleton<PeerNode>::get_instance()->get_public_node();
	oss << "Public PeerNode size is: " << pubNodeList.size() << std::endl;
	oss << Singleton<PeerNode>::get_instance()->nodelist_info(pubNodeList);//把所有的公网节点数据转换为string保存
	res.set_content(oss.str(), "text/plain");
}


void api_jsonrpc(const Request &req, Response &res)
{
    nlohmann::json ret;
    ret["jsonrpc"] = "2.0";
    try
    {
        auto json = nlohmann::json::parse(req.body);

        std::string method = json["method"];

        auto p = HttpServer::rpc_cbs.find(method);
        if (p == HttpServer::rpc_cbs.end())
        {
            ret["error"]["code"] = -32601;
            ret["error"]["message"] = "Method not found";
            ret["id"] = "";
        }
        else
        {
            auto params = json["params"];
            ret = HttpServer::rpc_cbs[method](params);
            try
            {
                ret["id"] = json["id"].get<int>();
            }
            catch (const std::exception &e)
            {
                ret["id"] = json["id"].get<std::string>();
            }
            ret["jsonrpc"] = "2.0";
        }
    }
    catch (const std::exception &e)
    {
        ret["error"]["code"] = -32700;
        ret["error"]["message"] = "Internal error";
        ret["id"] = "";
    }
    res.set_content(ret.dump(4), "application/json");
}

void api_get_db_key(const Request &req, Response &res)
{
    std::string key;
    if (req.has_param("key"))
    {
        key = req.get_param_value("key");
    }

    std::string value;
    DBReader().ReadData(key, value);
    res.set_content(value, "text/plain");
}

void api_print_block(const Request &req, Response &res)
{
    int num = 100;
    if (req.has_param("num"))
    {
        num = atol(req.get_param_value("num").c_str());
    }
    std::string str = printBlocks(num, req.has_param("pre_hash_flag"));
    res.set_content(str, "text/plain");
}

void api_info_queue(const Request &req, Response &res)
{
    std::ostringstream oss;
    oss << "queue_read:" << global::queue_read.msg_queue_.size() << std::endl;
    oss << "queue_work:" << global::queue_work.msg_queue_.size() << std::endl;
    oss << "queue_write:" << global::queue_write.msg_queue_.size() << std::endl;
    oss << "\n"
        << std::endl;

    double total = .0f;
    oss << "------------------------------------------" << std::endl;
    for (auto &item : global::reqCntMap)
    {
        total += (double)item.second.second;
        oss.precision(3);
        oss << item.first << ": " << item.second.first << " size: " << (double)item.second.second / 1024 / 1024 << " MB" << std::endl;
    }
    oss << "------------------------------------------" << std::endl;
    oss << "Total: " << total / 1024 / 1024 << " MB" << std::endl;

    res.set_content(oss.str(), "text/plain");
}

void api_info(const Request &req, Response &res)
{

    std::ostringstream oss;

    oss << "queue----------:" << std::endl;
    oss << "queue_read:" << global::queue_read.msg_queue_.size() << std::endl;
    oss << "queue_work:" << global::queue_work.msg_queue_.size() << std::endl;
    oss << "queue_write:" << global::queue_write.msg_queue_.size() << std::endl;
    oss << "\n"
        << std::endl;

    oss << "amount----------:" << std::endl;
    for (auto &i : g_AccountInfo.AccountList)
    {
        uint64_t amount = 0;
        GetBalanceByUtxo(i.first,amount);
        oss << i.first + ":" + std::to_string(amount) << std::endl;
        // std::string strPub;
        // g_AccountInfo.GetPubKeyStr(i.first.c_str(), strPub);
        // oss << base64Encode(strPub) << std::endl;
    }
    oss << "\n"
        << std::endl;

    std::vector<Node> pubNodeList = Singleton<PeerNode>::get_instance()->get_public_node();
    oss << "Public PeerNode size is: " << pubNodeList.size() << std::endl;
    oss << Singleton<PeerNode>::get_instance()->nodelist_info(pubNodeList);

    oss << std::endl
        << std::endl;

    std::vector<Node> nodeList = Singleton<PeerNode>::get_instance()->get_nodelist();
    oss << "PeerNode size is: " << nodeList.size() << std::endl;
    oss << Singleton<PeerNode>::get_instance()->nodelist_info(nodeList);

    res.set_content(oss.str(), "text/plain");
}


void api_node_cache(const Request &req, Response &res)
{
    std::ostringstream oss;
    std::vector<Node> node_cache_list = Singleton<NodeCache>::get_instance()->get_nodelist();
    oss << "node cache size is: " << node_cache_list.size() << std::endl;
    oss << "------------------------------------------" << std::endl;
    for(auto node : node_cache_list)
    {
        oss << node.info_str();
    }
    oss << "------------------------------------------" << std::endl;
    oss << std::endl;
    res.set_content(oss.str(), "text/plain");
}

void api_get_block(const Request &req, Response &res)
{
    nlohmann::json blocks;

    int top = 0;
    if (req.has_param("top"))
    {
        top = atol(req.get_param_value("top").c_str());
    }
    int num = 0;
    if (req.has_param("num"))
    {
        num = atol(req.get_param_value("num").c_str());
    }
    std::string hashs;
    if (req.has_param("hashs"))
    {
        hashs = req.get_param_value("hashs");
    }
    std::vector<std::string> tmp;
    StringUtil::SplitString(hashs, tmp, "_");
    std::unordered_set<std::string> exist_hash(tmp.begin(), tmp.end());

    num = num > SYNC_NUM_LIMIT ? SYNC_NUM_LIMIT : num;



    if (top < 0 || num <= 0)
    {
        ERRORLOG("api_get_block top < 0||num <= 0");
        return;
    }

    DBReader db_reader;
    uint64_t mytop;
    db_reader.GetBlockTop(mytop);
    if (top > (int)mytop)
    {
        ERRORLOG("api_get_block begin > mytop");
        return;
    }
    int k = 0;
    int block_num = 0;
    for (auto i = top; i <= top + num; i++)
    {
        if (block_num >= num)
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
            CBlock cblock;
            cblock.ParseFromString(strHeader);
            CTransaction *tx = cblock.mutable_txs(0);
            for (int i = 0; i < tx->vin_size(); i++)
            {
                CTxin *vin = tx->mutable_vin(i);
                std::string utxo_hash = vin->mutable_prevout()->hash();
                std::string pub = vin->mutable_scriptsig()->pub();
                std::string addr = GetBase58Addr(pub);

                uint64_t amount = TxHelper::GetUtxoAmount(utxo_hash, addr);
                vin->mutable_prevout()->set_hash(addr + "_" + std::to_string(amount) + "_" + utxo_hash);
            }
            strHeader = cblock.SerializeAsString();

            if (i == top)
            {
                auto result = exist_hash.find(cblock.hash());
                if (result == exist_hash.end())
                {
                    blocks[k++] = httplib::detail::base64_encode(strHeader);
                }
            }
            else
            {
                blocks[k++] = httplib::detail::base64_encode(strHeader);
            }
        }
    }

    res.set_content(blocks.dump(4), "application/json");
}

void api_get_block_hash(const Request &req, Response &res)
{
    nlohmann::json blocks;

    int top = 0;
    if (req.has_param("top"))
    {
        top = atol(req.get_param_value("top").c_str());
    }
    int num = 0;
    if (req.has_param("num"))
    {
        num = atol(req.get_param_value("num").c_str());
    }
    DBReader db_reader;
    uint64_t my_top = 0;
    db_reader.GetBlockTop(my_top);

    nlohmann::json block_hashs;
    int end = std::min(top, (int)my_top);
    int begin = (end - num > 0) ? (end - num) : 0;

    std::vector<std::string> hashs;
    for (auto i = begin; i <= end; i++)
    {
        std::vector<std::string> vBlockHashs;
        db_reader.GetBlockHashsByBlockHeight(i, vBlockHashs); //i height
        // std::for_each(vBlockHashs.begin(), vBlockHashs.end(),
        //         [](std::string &s){ s = s.substr(0,HASH_LEN);}
        // );
        hashs.insert(hashs.end(), vBlockHashs.begin(), vBlockHashs.end());
    }

    for (size_t i = 0; i < hashs.size(); i++)
    {
        block_hashs[i] = hashs[i];
    }

    res.set_content(block_hashs.dump(4), "application/json");
}

void api_get_block_by_hash(const Request &req, Response &res)
{
    nlohmann::json blocks;

    std::string hashs;
    if (req.has_param("hashs"))
    {
        hashs = req.get_param_value("hashs");
    }
    std::vector<std::string> v_blocks;
    SplitString(hashs, v_blocks, "_");

    DBReader db_reader;
    int i = 0;
    for (auto hash : v_blocks)
    {
        string strHeader;
        db_reader.GetBlockByBlockHash(hash, strHeader);
        CBlock cblock;
        cblock.ParseFromString(strHeader);
        CTransaction *tx = cblock.mutable_txs(0);
        for (int i = 0; i < tx->vin_size(); i++)
        {
            CTxin *vin = tx->mutable_vin(i);
            std::string utxo_hash = vin->mutable_prevout()->hash();
            std::string pub = vin->mutable_scriptsig()->pub();
            std::string addr = GetBase58Addr(pub);

            uint64_t amount = TxHelper::GetUtxoAmount(utxo_hash, addr);
            vin->mutable_prevout()->set_hash(addr + "_" + std::to_string(amount) + "_" + utxo_hash);
        }
        strHeader = cblock.SerializeAsString();

        blocks[i++] = httplib::detail::base64_encode(strHeader);
    }
    res.set_content(blocks.dump(4), "application/json");
}

void api_get_tx_owner(const Request &req, Response &res)
{
    std::string tx_hash;
    if (req.has_param("tx_hash"))
    {
        tx_hash = req.get_param_value("tx_hash");
    }
    std::string str = TxHelper::GetTxOwnerStr(tx_hash);
    res.set_content(str, "text/plain");
}

void api_get_gas(const Request &req, Response &res)
{
    nlohmann::json data;
    int i = 0;
    std::map<std::string, uint64_t> fees = net_get_node_ids_and_fees();
    for (auto item : fees)
    {
        data[i++] = item.second;
    }

    res.set_content(data.dump(4), "application/json");
}
void api_black_list(const Request & req, Response & res)
{
    std::unordered_map<std::string, BlackCache> black_list_cache;
    MagicSingleton<BlackListCache>::GetInstance()->GetCache(black_list_cache);

    std::ostringstream oss;
    oss << "addr black list( " << black_list_cache.size() << " ) " << std::endl << std::endl;

    auto getTimeStringFunc = [](uint64_t time, std::string & timeString)
    {
        time_t s = (time_t)time;
        struct tm * gm_date;
        gm_date = localtime(&s);
        char ps[1024] = {0};
        sprintf(ps, "%d-%d-%d %d:%d:%d (%zu)", gm_date->tm_year + 1900, gm_date->tm_mon + 1, gm_date->tm_mday, gm_date->tm_hour, gm_date->tm_min, gm_date->tm_sec, time);
        timeString = ps;
    };

    for(auto item : black_list_cache)
    {
        oss << "tx hash: " << item.first << std::endl;

        std::string timeString;
        getTimeStringFunc(item.second.tx.time() / 1000000, timeString);
        oss << "time: " << timeString << std::endl;
        std::string base58;
        if (item.second.base58_addr.size() != 0)
        {
            base58 = item.second.base58_addr[0];
        }
        oss << "addr: " << base58 << std::endl;
        oss << "vin size: "<< item.second.base58_addr.size() << std::endl;
        oss << "node id: " << item.second.tx.identity() << std::endl;
        oss << std::endl << std::endl;
    }
    oss << std::endl << std::endl;
    res.set_content(oss.str(), "text/plain");
}

void api_cache_info(const Request & req, Response & res)
{
    nlohmann::json data;
    auto cache = MagicSingleton<DBCache>::GetInstance();
    data["key_num"] = cache->GetKeyNum();
    data["data_size"] = cache->GetDataSize();
    res.set_content(data.dump(4), "application/json");
}

void test_create_multi_tx(const Request &req, Response &res)
{
    std::string from_addr;
    if (req.has_param("from_addr"))
    {
        from_addr = req.get_param_value("from_addr");
    }

    std::string to_addr;
    if (req.has_param("to_addr"))
    {
        to_addr = req.get_param_value("to_addr");
    }

    std::string amount;
    if (req.has_param("amount"))
    {
        amount = req.get_param_value("amount");
    }

    std::string fee_str = "1";
    if (req.has_param("fee"))
    {
        fee_str = req.get_param_value("fee");
    }

    std::stringstream ssminerfees;
    ssminerfees << fee_str;
    double fee;
    ssminerfees >> fee;

    std::vector<std::string> fromAddr;
    StringUtil::SplitString(from_addr, fromAddr, ",");

    std::vector<std::string> toAddr;
    StringUtil::SplitString(to_addr, toAddr, ",");

    std::vector<std::string> tmp;
    std::vector<uint64_t> v_amount;
    StringUtil::SplitString(amount, tmp, ",");
    for (auto i : tmp)
    {
        std::stringstream ssamount;
        ssamount << i;
        uint64_t amount;
        ssamount >> amount;
        v_amount.push_back(amount);
    }

    if (toAddr.size() > v_amount.size())
    {
        res.set_content("toAddr.size() > v_amount.size()", "text/plain");
        return;
    }

    std::map<std::string, int64_t> toAddrAmount;
    for (size_t i = 0; i < toAddr.size(); i++)
    {
        toAddrAmount[toAddr[i]] = v_amount[i] * DECIMAL_NUM;
    }

    uint64_t gasFee = fee *DECIMAL_NUM;
    uint32_t test_needVerifyPreHashCount = 3;
    CTransaction outTx;
    std::vector<TxHelper::Utxo> outVin;
	if (TxHelper::CreateTxTransaction(fromAddr, toAddrAmount, test_needVerifyPreHashCount , gasFee, outTx, outVin) != 0)
	{
		ERRORLOG("CreateTxTransaction error!!");
		return;
	}

    std::string serTx;
    std::string encodeStrHash;
    if(TxHelper::SignTransaction(outVin, outTx, serTx, encodeStrHash) != 0)
    {
		ERRORLOG("SignTransaction error!!");
		return;        
    }

	TxMsg txMsg;
	txMsg.set_version(getVersion());
	txMsg.set_tx(serTx);
	txMsg.set_txencodehash(encodeStrHash);

	DBReader db_reader;
	uint64_t top = 0;
	db_reader.GetBlockTop(top);
	txMsg.set_top(top);

	std::string blockHash;
	if (DBStatus::DB_SUCCESS != db_reader.GetBestChainHash(blockHash))
	{
		ERRORLOG("GetBestChainHash return no zero");
		return;
	}
	txMsg.set_prevblkhash(blockHash);
	txMsg.set_trycountdown(CalcTxTryCountDown(test_needVerifyPreHashCount));

	auto msg = make_shared<TxMsg>(txMsg);

	std::string txHash;
	int ret = DoHandleTx(msg, txHash);
	DEBUGLOG("Transaction result，ret:{}  txHash：{}", ret, txHash);
}

nlohmann::json jsonrpc_get_height(const nlohmann::json &param)
{
    DBReader db_reader;
    uint64_t top = 0;
    db_reader.GetBlockTop(top);

    nlohmann::json ret;
    ret["result"]["height"] = std::to_string(top);
    return ret;
}

// get basic information, add 20210617 Liu
nlohmann::json jsonrpc_get_basic_info(const nlohmann::json & param)
{
    
	std::string base58 = g_AccountInfo.DefaultKeyBs58Addr;

    uint64_t balance = 0;
    GetBalanceByUtxo(base58,balance);
	
    DBReader db_reader;
    uint64_t minerFee = 0;

    db_reader.GetDeviceSignatureFee(minerFee);

    uint64_t packageFee = 0;
    db_reader.GetDevicePackageFee(packageFee);

    uint64_t blockHeight = 0;
    db_reader.GetBlockTop(blockHeight);

    std::string bestChainHash;
    db_reader.GetBestChainHash(bestChainHash);

    std::string ownID = net_get_self_node_id();

    nlohmann::json ret;
    ret["result"]["base58"] = base58;
    ret["result"]["balance"] = balance;
    //ret["result"]["mac_md5"] = macMd5;
    ret["result"]["node_id"] = ownID;
    ret["result"]["signature_fee"] = minerFee;
    ret["result"]["package_fee"] = packageFee;
    ret["result"]["block_top"] = blockHeight;
    ret["result"]["top_block_hash"] = bestChainHash;

    return ret;
}

nlohmann::json jsonrpc_get_balance(const nlohmann::json &param)
{
    nlohmann::json ret;
    std::string address;
    try
    {
        if (param.find("address") != param.end())
        {
            address = param["address"].get<std::string>();
        }
        else
        {
            throw std::exception();
        }
    }
    catch (const std::exception &e)
    {
        ret["error"]["code"] = -32602;
        ret["error"]["message"] = "Invalid params";
        return ret;
    }

    if (!CheckBase58Addr(address))
    {
        ret["error"]["code"] = -1;
        ret["error"]["message"] = "address is invalid ";
        return ret;
    }
    
    uint64_t balance = 0;              
    if(GetBalanceByUtxo(address.c_str(),balance) != 0)
    {
        ret["error"]["code"] = -2;
        ret["error"]["message"] = "search balance failed";
        return ret;
    }
    ret["result"]["balance"] = std::to_string((double)balance / DECIMAL_NUM);
    return ret;
}

nlohmann::json jsonrpc_get_txids_by_height(const nlohmann::json &param)
{
    nlohmann::json ret;
    std::regex pattern("^[1-9]\\d*|0$");
    bool bIsvalid = std::regex_match(param["height"].get<std::string>(), pattern);
    if (!bIsvalid)
    {
        ret["error"]["code"] = -1;
        ret["error"]["message"] = "height is invalid";
        return ret;
    }
    uint32_t height = 0;
    try
    {
        if (param.find("height") != param.end())
        {
            height = atoi(param["height"].get<std::string>().c_str());
        }
        else
        {
            throw std::exception();
        }
    }
    catch (const std::exception &e)
    {
        ret["error"]["code"] = -32602;
        ret["error"]["message"] = "Invalid params";
        return ret;
    }

    DBReader db_reader;
    uint64_t top = 0;
    if (db_reader.GetBlockTop(top) != 0)
    {
        ret["error"]["code"] = -3;
        ret["error"]["message"] = "Getting the block top is error!";
        return ret;
    }

    if (height > top)
    {
        ret["error"]["code"] = -4;
        ret["error"]["message"] = "height more than block top ";
        return ret;
    }

    std::vector<std::string> vBlockHashs;
    db_reader.GetBlockHashsByBlockHeight(height, vBlockHashs);
    int k = 0;
    ret["result"] = nlohmann::json::array();

    for (auto hash : vBlockHashs)
    {
        string strHeader;
        db_reader.GetBlockByBlockHash(hash, strHeader);

        CBlock cblock;
        cblock.ParseFromString(strHeader);

        for (int i = 0; i < cblock.txs_size(); i++)
        {
            CTransaction tx = cblock.txs(i);
            ret["result"][k++] = tx.hash();
        }
    }

    return ret;
}

nlohmann::json jsonrpc_get_tx_by_txid(const nlohmann::json &param)
{
    nlohmann::json ret;
    std::string hash;
    try
    {
        if (param.find("hash") != param.end())
        {
            hash = param["hash"].get<std::string>();
        }
        else
        {
            throw std::exception();
        }
    }
    catch (const std::exception &e)
    {
        ret["error"]["code"] = -32602;
        ret["error"]["message"] = "Invalid params";
        return ret;
    }

    if (hash.size() != 64)
    {
        ret["error"]["code"] = -1;
        ret["error"]["message"] = "hash is invalid";
        return ret;
    }

    DBReader db_reader;
    std::string strTxRaw;
    if (DBStatus::DB_SUCCESS != db_reader.GetTransactionByHash(hash, strTxRaw))
    {
        ret["error"]["code"] = -32000;
        ret["error"]["message"] = "not find txraw";
        return ret;
    }

    string blockhash;
    unsigned height;
    auto stat = db_reader.GetBlockHashByTransactionHash(hash, blockhash);
    if (DBStatus::DB_SUCCESS != stat)
    {
        ret["error"]["code"] = -32602;
        ret["error"]["message"] = "rocksdb error";
        return ret;
    }
    stat = db_reader.GetBlockHeightByBlockHash(blockhash, height);
    if (DBStatus::DB_SUCCESS != stat)
    {
        ret["error"]["code"] = -32602;
        ret["error"]["message"] = "rocksdb error";
        return ret;
    }
    ret["result"]["height"] = std::to_string(height);

    CTransaction utxoTx;
    utxoTx.ParseFromString(strTxRaw);

    nlohmann::json extra = nlohmann::json::parse(utxoTx.extra());
    std::string txType = extra["TransactionType"].get<std::string>();

    std::transform(txType.begin(), txType.end(), txType.begin(),::toupper);
    ret["result"]["hash"] = utxoTx.hash();
    ret["result"]["time"] = utxoTx.time();
    

    bool majortype = true,signtype = false,awardtyep = false;
    std::vector<std::string> utxo_hashs;
    for (int i = 0; i < utxoTx.vin_size(); i++)
    {
        CTxin vin = utxoTx.vin(i);
        std::string utxo_hash = vin.prevout().hash();
        if(utxo_hash.empty())
        {
            awardtyep = true;
        }
        bool flag = false;
        for(uint32_t j = 0; j < utxo_hashs.size(); j++)
        {
            if(utxo_hash == utxo_hashs.at(j))
                flag = true;
        }
        if(flag)
            continue;
        utxo_hashs.push_back(utxo_hash);
        std::string pub = vin.scriptsig().pub();
        if(pub.empty() &&(!awardtyep))
        {
           signtype = true;
        }
        std::string addr = GetBase58Addr(pub);

        uint64_t amount = TxHelper::GetUtxoAmount(utxo_hash, addr);
        ret["result"]["vin"][i]["address"] = addr;
        ret["result"]["vin"][i]["prev_hash"] = utxo_hash;
        ret["result"]["vin"][i]["output_index"] = 0;
        ret["result"]["vin"][i]["output_value"] = std::to_string((double)amount / DECIMAL_NUM);
    }

    for (int i = 0; i < utxoTx.vout_size(); i++)
    {
        CTxout txout = utxoTx.vout(i);
        ret["result"]["vout"][i]["address"] = txout.scriptpubkey();
        ret["result"]["vout"][i]["value"] = std::to_string((double)txout.value() / DECIMAL_NUM);
    }

    if(majortype &&(!signtype)&&(!awardtyep))
    {
       
        ret["result"]["type"] =   txType ;
    }
    else if (signtype)
    {
        ret["result"]["type"] = FEE_SIGN_STR;    
    }
    else if (awardtyep)
    {
        ret["result"]["type"] = EXTRA_AWARD_SIGN_STR;
    }
    return ret;
}

nlohmann::json jsonrpc_get_tx_by_txid_and_nodeid(const nlohmann::json &param)
{
    nlohmann::json ret;

    std::string hash, nodeid;
    try
    {
        if (param.find("hash") == param.end() || param.find("nodeid") == param.end())
            throw std::exception();
        hash = param["hash"].get<std::string>();
        nodeid = param["nodeid"].get<std::string>();
    }
    catch (const std::exception &e)
    {
        ret["error"]["code"] = -32602;
        ret["error"]["message"] = "Invalid params";
        return ret;
    }

    if (hash.size() != 64)
    {
        ret["error"]["code"] = -1;
        ret["error"]["message"] = "hash is invalid";
        return ret;
    }
    //发送请求获取交易信息
    Node node;
    if (nodeid == Singleton<PeerNode>::get_instance()->get_self_id())
    {
        /*ret["error"]["code"] = -1;
        ret["error"]["message"] = "nodeid can not be self";
        return ret;*/
        node = Singleton<PeerNode>::get_instance()->get_self_node();
    }
    else if (!Singleton<PeerNode>::get_instance()->find_node(id_type(nodeid), node))
    {
        ret["error"]["code"] = -1;
        ret["error"]["message"] = "nodeid is invalid";
        return ret;
    }

    GetTransInfoReq req;
    req.set_txid(hash);
    req.set_nodeid( Singleton<PeerNode>::get_instance()->get_self_id() );
    net_com::send_message(node, req, net_com::Compress::kCompress_False, net_com::Encrypt::kEncrypt_False, net_com::Priority::kPriority_High_2);
    //等待 20秒，获取返回信息
    CTransaction utxo_tx;
    bool is_empty = true;
    int height = 0;
    for (int i = 0; i < 15; i++)
    {
        sleep(2); //休眠2秒
        {
            std::lock_guard<std::mutex> lock(global::g_mutex_transinfo);
            if (!global::g_is_utxo_empty)
            {
                global::g_is_utxo_empty = true;
                utxo_tx = global::g_trans_info.trans();
                is_empty = false;
                height = global::g_trans_info.height();
                break;
            }
        }
    }
    //如果30秒内没有返回数据，则查找失败
    if (is_empty)
    {
        ret["error"]["code"] = -32000;
        ret["error"]["message"] = "not find txraw";
        return ret;
    }

    nlohmann::json extra = nlohmann::json::parse(utxo_tx.extra());

    ret["result"]["hash"] = utxo_tx.hash();
    ret["result"]["time"] = utxo_tx.time();
    ret["result"]["type"] = extra["TransactionType"].get<std::string>();
    ret["result"]["height"] = std::to_string(height);
    std::vector<std::string> utxo_hashs;
    for (int i = 0; i < utxo_tx.vin_size(); i++)
    {
        CTxin vin = utxo_tx.vin(i);
        std::string utxo_hash = vin.prevout().hash();
        bool flag = false;
        for(uint32_t j = 0; j < utxo_hashs.size(); j++)
        {
            if(utxo_hash == utxo_hashs.at(j))
                flag = true;
        }
        if(flag)
            continue;
        utxo_hashs.push_back(utxo_hash);
        std::string pub = vin.scriptsig().pub();
        std::string addr = GetBase58Addr(pub);

        uint64_t amount = TxHelper::GetUtxoAmount(utxo_hash, addr);
        ret["result"]["vin"][i]["address"] = addr;
        ret["result"]["vin"][i]["prev_hash"] = utxo_hash;
        ret["result"]["vin"][i]["output_index"] = 0;
        ret["result"]["vin"][i]["output_value"] = std::to_string((double)amount / DECIMAL_NUM);
    }

    for (int i = 0; i < utxo_tx.vout_size(); i++)
    {
        CTxout txout = utxo_tx.vout(i);
        ret["result"]["vout"][i]["address"] = txout.scriptpubkey();
        ret["result"]["vout"][i]["value"] = std::to_string((double)txout.value() / DECIMAL_NUM);
    }
    return ret;
}

nlohmann::json jsonrpc_create_tx_message(const nlohmann::json &param)
{
    nlohmann::json ret;
    double fee = 0;
    std::vector<std::string> from_addrs;
    std::map<std::string, int64_t> toAddrAmount;
    std::string strFee;
    try
    {
        if (param.find("fee") != param.end())
        {
            strFee = param["fee"].get<std::string>();
        }
        else
        {
            throw std::exception();
        }
        istringstream issInputFee(strFee);
        issInputFee >> fee;
        fee = (fee + FIX_DOUBLE_MIN_PRECISION) * DECIMAL_NUM;

        if (param.find("from_addr") != param.end())
        {
            for (const auto &addr : param["from_addr"])
            {
                from_addrs.push_back(addr.get<std::string>());
            }
        }
        else
        {
            throw std::exception();
        }

        if (param.find("to_addr") != param.end())
        {
            int i = 0;
            for (const auto &addr : param["to_addr"])
            {
                if (param["to_addr"][i].find("addr") != param["to_addr"][i].end() && param["to_addr"][i].find("value") != param["to_addr"][i].end())
                {
                    std::regex pattern("[0-9]+(\\.[0-9]{1,6})?");
                    string search_value = addr["value"].get<std::string>();
                    bool bIsvalid = std::regex_match(search_value, pattern);
                    if (!bIsvalid)
                    {
                        ret["error"]["code"] = -32602;
                        ret["error"]["message"] = "The value is wrong or More than 6 decimal places";
                        return ret;
                    }
                    double value = 0;
                    istringstream issInputValue(addr["value"].get<std::string>());
                    issInputValue >> value;
                    value = (value + FIX_DOUBLE_MIN_PRECISION) * DECIMAL_NUM;
                    toAddrAmount[addr["addr"].get<std::string>()] = value;
                }
                else
                {
                    throw std::exception();
                }
                i++;
            }
        }
        else
        {
            throw std::exception();
        }
    }
    catch (const std::exception &e)
    {
        ret["error"]["code"] = -32602;
        ret["error"]["message"] = "Invalid params";
        return ret;
    }

    CTransaction outTx;
    std::vector<TxHelper::Utxo> outVin;
    int error_number = TxHelper::CreateTxTransaction(from_addrs, toAddrAmount, g_MinNeedVerifyPreHashCount, fee, outTx, outVin);
    if (error_number != 0)
    {
        ret["error"]["code"] = -32000;
        ret["error"]["message"] = "create fail,error number:" + std::to_string(error_number);
        return ret;
    }

    std::string serTx = outTx.SerializeAsString();
    std::string encodeStr = base64Encode(serTx);
    std::string encodeStrHash = getsha256hash(encodeStr);

    ret["result"]["tx_data"] = encodeStr;
    ret["result"]["tx_encode_hash"] = encodeStrHash;
    return ret;
}

nlohmann::json jsonrpc_send_tx(const nlohmann::json &param)
{
    nlohmann::json ret;
    std::string tx_data;
    std::string tx_signature;
    std::string public_key;
    std::string tx_encode_hash;

    try
    {
        if (param.find("tx_data") != param.end() &&
            param.find("tx_signature") != param.end() &&
            param.find("public_key") != param.end() &&
            param.find("tx_encode_hash") != param.end())
        {
            tx_data = param["tx_data"].get<std::string>();
            tx_signature = param["tx_signature"].get<std::string>();
            public_key = param["public_key"].get<std::string>();
            tx_encode_hash = param["tx_encode_hash"].get<std::string>();
        }
        else
        {
            throw std::exception();
        }
    }
    catch (const std::exception &e)
    {
        ret["error"]["code"] = -32602;
        ret["error"]["message"] = "Invalid params";
        return ret;
    }

    unsigned char * tx_data_tmp_ptr = new unsigned char[tx_data.size()]{0};
    unsigned long tx_data_len = base64_decode((unsigned char *)tx_data.data(), tx_data.size(), tx_data_tmp_ptr);
    std::string tx_data_str((char *)tx_data_tmp_ptr, tx_data_len);
    delete [] tx_data_tmp_ptr;
    tx_data_tmp_ptr = nullptr;

    unsigned char * public_key_tmp_ptr = new unsigned char[public_key.size()]{0};
    unsigned long public_key_len = base64_decode((unsigned char *)public_key.data(), public_key.size(), public_key_tmp_ptr);
    std::string public_key_str((char *)public_key_tmp_ptr, public_key_len);
    delete [] public_key_tmp_ptr;
    public_key_tmp_ptr = nullptr;

    unsigned char * tx_signature_tmp_ptr = new unsigned char[tx_signature.size()]{0};
    unsigned long tx_signature_len = base64_decode((unsigned char *)tx_signature.data(), tx_signature.size(), tx_signature_tmp_ptr);
    std::string tx_signature_str((char *)tx_signature_tmp_ptr, tx_signature_len);
    delete [] tx_signature_tmp_ptr;
    tx_signature_tmp_ptr = nullptr;

    if (tx_data_len == 0 || public_key_len == 0 || tx_signature_len == 0)
    {
        ret["error"]["code"] = -32602;
        ret["error"]["message"] = "Invalid base64 string";
        return ret;
    }

    CTransaction tx;
    tx.ParseFromString(tx_data_str);

    //签名
    for (int i = 0; i < tx.vin_size(); i++)
    {
        auto vin = tx.mutable_vin(i);
        vin->mutable_scriptsig()->set_sign(tx_signature_str);
        vin->mutable_scriptsig()->set_pub(public_key_str);
    }

    std::string serTx = tx.SerializeAsString();

    TxMsg txMsg;
    txMsg.set_version(getVersion());

    std::string ID = Singleton<PeerNode>::get_instance()->get_self_id();
    txMsg.set_id(ID);
    txMsg.set_txencodehash(tx_encode_hash);
    txMsg.set_tx(serTx);

    DBReader db_reader;
    uint64_t top = 0;
    db_reader.GetBlockTop(top);
    txMsg.set_top(top);

    std::string blockHash;
    db_reader.GetBestChainHash(blockHash);
    txMsg.set_prevblkhash(blockHash);

    auto extra = nlohmann::json::parse(tx.extra());
    int needVerifyPreHashCount = extra["NeedVerifyPreHashCount"].get<int>();
    txMsg.set_trycountdown(CalcTxTryCountDown(needVerifyPreHashCount));

    // msgdata是为了方便调用接口，没有实际意义
    auto msg = make_shared<TxMsg>(txMsg);

    std::string tx_hash;
    int error_num = DoHandleTx(msg, tx_hash);
    if (error_num != 0)
    {
        ret["error"]["code"] = -32000;
        ret["error"]["message"] = "create fail,error number:" + std::to_string(error_num);
        return ret;
    }
    ret["result"]["tx_hash"] = tx_hash;
    ret["result"]["height"] = top;

    return ret;
}

nlohmann::json jsonrpc_send_multi_tx(const nlohmann::json &param)
{
    nlohmann::json ret;
    std::string tx_data;
    std::string tx_signature;
    std::string public_key;
    std::string tx_encode_hash;

    std::map<std::string, std::tuple<std::string, std::string>> sign;
    try
    {
        if (param.find("tx_data") != param.end() &&
            param.find("tx_encode_hash") != param.end())
        {
            tx_data = param["tx_data"].get<std::string>();
            tx_encode_hash = param["tx_encode_hash"].get<std::string>();
        }
        else
        {
            throw std::exception();
        }

        if (param.find("sign") != param.end())
        {
            int i = 0;
            for (const auto &item : param["sign"])
            {
                if (param["sign"][i].find("public_key") != param["sign"][i].end() && param["sign"][i].find("tx_signature") != param["sign"][i].end())
                {
                    std::string public_key = item["public_key"].get<std::string>();
                    std::string tx_signature = item["tx_signature"].get<std::string>();
                    std::string decode_pub = base64Decode(public_key);

                    DEBUGLOG("public_key:{}, tx_signature:{}, addr:{}", public_key, tx_signature, GetBase58Addr(decode_pub));

                    sign[GetBase58Addr(decode_pub)] = std::make_tuple(decode_pub, base64Decode(tx_signature));
                }
                else
                {
                    throw std::exception();
                }
                i++;
            }
        }
        else
        {
            throw std::exception();
        }
    }
    catch (const std::exception &e)
    {
        ret["error"]["code"] = -32602;
        ret["error"]["message"] = "Invalid params";
        return ret;
    }

    std::string tx_data_str = base64Decode(tx_data);

    CTransaction tx;
    tx.ParseFromString(tx_data_str);

    //签名
    for (int i = 0; i < tx.vin_size(); i++)
    {
        auto vin = tx.mutable_vin(i);

        std::string addr = vin->mutable_scriptsig()->pub();
        auto result = sign.find(addr);
        if (result != sign.end())
        {
            vin->mutable_scriptsig()->set_sign(std::get<1>(sign[addr]));
            vin->mutable_scriptsig()->set_pub(std::get<0>(sign[addr]));
            DEBUGLOG("addr:{}, pub:{}, sign:{}", addr, base64Encode(std::get<0>(sign[addr])), base64Encode(std::get<1>(sign[addr])));
        }
    }

    std::string serTx = tx.SerializeAsString();

    TxMsg txMsg;
    txMsg.set_version(getVersion());

    std::string ID = Singleton<PeerNode>::get_instance()->get_self_id();
    txMsg.set_id(ID);
    txMsg.set_txencodehash(tx_encode_hash);
    txMsg.set_tx(serTx);

    uint64_t top = 0;
    DBReader db_reader;
    db_reader.GetBlockTop(top);
    txMsg.set_top(top);

    std::string blockHash;
    db_reader.GetBestChainHash(blockHash);
    txMsg.set_prevblkhash(blockHash);

    auto extra = nlohmann::json::parse(tx.extra());
    int needVerifyPreHashCount = extra["NeedVerifyPreHashCount"].get<int>();
    txMsg.set_trycountdown(CalcTxTryCountDown(needVerifyPreHashCount));

    auto msg = make_shared<TxMsg>(txMsg);

    std::string tx_hash;
    int error_num = DoHandleTx(msg, tx_hash);
    if (error_num != 0)
    {
        ret["error"]["code"] = -32000;
        ret["error"]["message"] = "create fail,error number:" + std::to_string(error_num);
        return ret;
    }
    ret["result"]["tx_hash"] = tx_hash;

    return ret;
}

nlohmann::json jsonrpc_get_avg_fee(const nlohmann::json &param)
{
    int64_t avg_fee_tmp = m_api::getAvgFee();
    double avg_fee = 0;
    if (avg_fee_tmp < 0)
    {
        avg_fee = 0;
    }
    else
    {
        avg_fee = (double)avg_fee_tmp / DECIMAL_NUM;
    }

    nlohmann::json ret;
    ret["result"]["avg_fee"] = std::to_string(avg_fee);
    return ret;
}

nlohmann::json jsonrpc_generate_wallet(const nlohmann::json &param)
{
    nlohmann::json ret;

    const int BUFF_SIZE = 128;
    char *out_private_key = new char[BUFF_SIZE]{0};
    int *out_private_key_len = new int{BUFF_SIZE};
    char *out_public_key = new char[BUFF_SIZE]{0};
    int *out_public_key_len = new int{BUFF_SIZE};
    char *out_bs58addr = new char[BUFF_SIZE]{0};
    int *out_bs58addr_len = new int{BUFF_SIZE};
    GenWallet_(out_private_key, out_private_key_len, out_public_key, out_public_key_len, out_bs58addr, out_bs58addr_len);

    char *base64_pri_key = new char[BUFF_SIZE]{0};
    char *base64_pub_key = new char[BUFF_SIZE]{0};
    base64_encode((unsigned char *)out_private_key, *out_private_key_len, (unsigned char *)base64_pri_key);
    base64_encode((unsigned char *)out_public_key, *out_public_key_len, (unsigned char *)base64_pub_key);
    std::string private_key(base64_pri_key);
    std::string public_key(base64_pub_key);
    std::string address(out_bs58addr);

    ret["result"]["private_key"] = private_key;
    ret["result"]["public_key"] = public_key;
    ret["result"]["address"] = address;

    delete[] out_private_key;
    delete[] out_private_key_len;
    delete[] out_public_key;
    delete[] out_public_key_len;
    delete[] out_bs58addr;
    delete[] out_bs58addr_len;
    delete[] base64_pub_key;
    delete[] base64_pri_key;

    return ret;
}

nlohmann::json jsonrpc_generate_sign(const nlohmann::json &param)
{
    nlohmann::json ret;
    std::string data;
    std::string private_key_base64;
    const int BUFF_SIZE = 128;
    try
    {
        if (param.find("data") != param.end() &&
            param.find("private_key") != param.end())
        {
            data = param["data"].get<std::string>();
            private_key_base64 = param["private_key"].get<std::string>();
            char *private_key = new char[BUFF_SIZE]{0};
            int size = base64_decode((unsigned char *)private_key_base64.c_str(), private_key_base64.size(), (unsigned char *)private_key);

            char *signature_msg = new char[BUFF_SIZE]{0};
            int *out_len = new int{BUFF_SIZE};
            GenSign_(private_key, size, data.c_str(), data.size(), signature_msg, out_len);

            std::string signature(signature_msg, *out_len);
            ret["result"]["message"] = signature;

            delete[] out_len;
            delete[] signature_msg;
            delete[] private_key;
        }
        else
        {
            throw std::exception();
        }
    }
    catch (const std::exception &e)
    {
        ret["error"]["code"] = -32602;
        ret["error"]["message"] = "Invalid params";
        return ret;
    }

    return ret;
}

nlohmann::json jsonrpc_get_pending_transaction(const nlohmann::json &param)
{
    nlohmann::json ret;

    std::string address;

    if (param.find("address") != param.end())
    {
        address = param["address"].get<std::string>();
    }

    if (!address.empty())
    {
        if (!CheckBase58Addr(address))
        {
            ret["error"]["code"] = -1;
            ret["error"]["message"] = "address is invalid";
            return ret;
        }
    }

    std::vector<TxVinCache::Tx> vectTxs;
    if (address.empty())
    {
        MagicSingleton<TxVinCache>::GetInstance()->GetAllTx(vectTxs);
    }
    else
    {
        MagicSingleton<TxVinCache>::GetInstance()->Find(address, vectTxs);
    }

    ret["result"]["total"] = vectTxs.size();

    int row = 0;
    for (auto iter = vectTxs.begin(); iter != vectTxs.end(); ++iter)
    {
        nlohmann::json tx;
        tx["hash"] = iter->txHash;

        int pos = 0;
        for (const auto &in : iter->vins)
        {
            tx["vin"][pos++] = in;
        }

        pos = 0;
        for (const auto &from : iter->from)
        {
            tx["from"][pos++] = from;
        }

        pos = 0;
        for (const auto &to : iter->to)
        {
            tx["to"][pos++] = to;
        }

        tx["amount"] = iter->amount;
        tx["timestamp"] = iter->timestamp;
        tx["broadstamp"] = iter->txBroadcastTime;
        tx["gap"] = iter->gas;

        pos = 0;
        for (const auto &amount : iter->toAmount)
        {
            tx["toAmount"][pos++] = amount;
        }

        tx["type"] = iter->type;

        ret["result"]["transaction"][row++] = tx;
    }

    return ret;
}

nlohmann::json jsonrpc_get_failure_transaction(const nlohmann::json &param)
{
    nlohmann::json ret;

    std::string address;
    if (param.find("address") != param.end())
    {
        address = param["address"].get<std::string>();
    }

    if (!address.empty())
    {
        if (!CheckBase58Addr(address))
        {
            ret["error"]["code"] = -1;
            ret["error"]["message"] = "address isinvalid";
            return ret;
        }
    }

    std::vector<TxVinCache::Tx> vectTxs;
    if (address.empty())
    {
        MagicSingleton<TxFailureCache>::GetInstance()->GetAll(vectTxs);
    }
    else
    {
        MagicSingleton<TxFailureCache>::GetInstance()->Find(address, vectTxs);
    }

    ret["result"]["total"] = vectTxs.size();

    int row = 0;
    for (auto iter = vectTxs.begin(); iter != vectTxs.end(); ++iter)
    {
        nlohmann::json tx;
        tx["hash"] = iter->txHash;

        int pos = 0;
        for (const auto &in : iter->vins)
        {
            tx["vin"][pos++] = in;
        }

        pos = 0;
        for (const auto &from : iter->from)
        {
            tx["from"][pos++] = from;
        }

        pos = 0;
        for (const auto &to : iter->to)
        {
            tx["to"][pos++] = to;
        }

        tx["amount"] = iter->amount;
        tx["timestamp"] = iter->timestamp;
        tx["broadstamp"] = iter->txBroadcastTime;
        tx["gap"] = iter->gas;

        pos = 0;
        for (const auto &amount : iter->toAmount)
        {
            tx["toAmount"][pos++] = amount;
        }

        tx["type"] = iter->type;

        ret["result"]["transaction"][row++] = tx;
    }

    return ret;
}

nlohmann::json jsonrpc_get_block_info_list(const nlohmann::json &param)
{
    nlohmann::json ret;
    uint32_t index = 0;
    uint32_t count = 0;
    uint32_t type = 0;

    std::regex pattern("^[1-9]\\d*|0$");
    if (param.find("index") != param.end())
    {
        bool bIsvalid = std::regex_match(param["index"].get<std::string>(), pattern);
        if (!bIsvalid)
        {
            ret["error"]["code"] = -32602;
            ret["error"]["message"] = "index Invalid params";
            return ret;
        }
        index = atoi(param["index"].get<std::string>().c_str());
    }
    if (param.find("count") != param.end())
    {

        bool bIsvalid = std::regex_match(param["count"].get<std::string>(), pattern);
        if (!bIsvalid)
        {
            ret["error"]["code"] = -32602;
            ret["error"]["message"] = "count Invalid params";
            return ret;
        }
        count = atoi(param["count"].get<std::string>().c_str());
    }
    if (param.find("type") != param.end())
    {
        bool bIsvalid = std::regex_match(param["type"].get<std::string>(), pattern);
        if (!bIsvalid)
        {
            ret["error"]["code"] = -32602;
            ret["error"]["message"] = "type Invalid params";
            return ret;
        }
        type = atoi(param["type"].get<std::string>().c_str());
    }

    if (type != 0)
    {
    }

    if (count == 0)
    {
        ret["error"]["code"] = -1;
        ret["error"]["message"] = "count is not equal zero！";
        return ret;
    }


    DBReader db_reader;
    uint64_t top = 0;
    if (DBStatus::DB_SUCCESS != db_reader.GetBlockTop(top))
    {
        ret["error"]["code"] = -3;
        ret["error"]["message"] = "Getting the block top is error!";
        return ret;
    }

    if (count > top)
    {
        ret["error"]["code"] = -4;
        ret["error"]["message"] = "count more than block top ";
        return ret;
    }

    uint64_t maxHeight = 0;
    uint64_t minHeight = 0;

    if (index == 0)
    {
        maxHeight = top;
        minHeight = maxHeight > count ? maxHeight - count : 0;
    }
    else
    {
        maxHeight = index > top ? top : index;
        minHeight = index > count ? index - count : 0;
    }

    for (uint32_t height = maxHeight; height > minHeight; --height)
    {
        std::vector<std::string> blockHashs;
        if (DBStatus::DB_SUCCESS != db_reader.GetBlockHashsByBlockHeight(height, blockHashs))
        {
            continue;
        }

        std::vector<CBlock> blocks;
        for (auto &blkHash : blockHashs)
        {
            std::string blockStr;
            if (DBStatus::DB_SUCCESS != db_reader.GetBlockByBlockHash(blkHash, blockStr))
            {
                continue;
            }

            CBlock cblock;
            cblock.ParseFromString(blockStr);
            if (cblock.hash().empty())
            {
                continue;
            }
            blocks.push_back(cblock);
        }
        // 单层高度所有块按时间倒序
        std::sort(blocks.begin(), blocks.end(), [](CBlock &a, CBlock &b) { return a.time() > b.time(); });

        nlohmann::json jsonHeight;
        for (auto &cblock : blocks)
        {
            nlohmann::json jsonBlock;
            jsonBlock["block_hash"] = cblock.hash();
            jsonBlock["block_height"] = cblock.height();
            jsonBlock["block_time"] = cblock.time();

            CTransaction tx;
            for (auto &t : cblock.txs())
            {
                CTxin txin0 = t.vin(0);
                if (txin0.scriptsig().sign() != std::string(FEE_SIGN_STR) &&
                    txin0.scriptsig().sign() != std::string(EXTRA_AWARD_SIGN_STR))
                {
                    tx = t;
                    break;
                }
            }

            jsonBlock["tx"]["hash"] = tx.hash();

            std::vector<std::string> owners = TxHelper::GetTxOwner(tx);
            for (auto &txOwner : owners)
            {
                jsonBlock["tx"]["from"].push_back(txOwner);
            }

            uint64_t amount = 0;
            nlohmann::json extra = nlohmann::json::parse(tx.extra());
            std::string txType = extra["TransactionType"].get<std::string>();
            if (txType == TXTYPE_REDEEM)
            {
                nlohmann::json txInfo = extra["TransactionInfo"].get<nlohmann::json>();
                std::string redeemUtxo = txInfo["RedeemptionUTXO"];

                std::string txRaw;
                if (DBStatus::DB_SUCCESS == db_reader.GetTransactionByHash(redeemUtxo, txRaw))
                {
                    CTransaction utxoTx;
                    utxoTx.ParseFromString(txRaw);
                    std::string fromAddrTmp;

                    for (int i = 0; i < utxoTx.vout_size(); i++)
                    {
                        CTxout txout = utxoTx.vout(i);
                        if (txout.scriptpubkey() != VIRTUAL_ACCOUNT_PLEDGE)
                        {
                            fromAddrTmp = txout.scriptpubkey();
                            continue;
                        }
                        amount = txout.value();
                    }
                    if (!fromAddrTmp.empty())
                        jsonBlock["tx"]["to"].push_back(fromAddrTmp);
                }
            }

            for (auto &txOut : tx.vout())
            {
                if (owners.end() != find(owners.begin(), owners.end(), txOut.scriptpubkey()))
                {
                    continue;
                }
                else
                {
                    jsonBlock["tx"]["to"].push_back(txOut.scriptpubkey());
                    amount += txOut.value();
                }
            }

            jsonBlock["tx"]["amount"] = to_string((double_t)amount / DECIMAL_NUM);

            jsonHeight.push_back(jsonBlock);
        }
        ret["result"]["height"].push_back(jsonHeight);
    }

    return ret;
}

// Create: Confirm the transaction with transaction hash, 20210311,   Liu
nlohmann::json jsonrpc_confirm_transaction(const nlohmann::json &param)
{
    nlohmann::json ret;

    std::string txHash;
    uint64_t height = 0;
    if (param.find("tx_hash") != param.end())
    {
        txHash = param["tx_hash"].get<std::string>();
    }

    if (param.find("height") != param.end())
    {
        height = param["height"].get<uint64_t>();
    }

    if (txHash.empty())
    {
        ret["error"]["code"] = -1;
        ret["error"]["message"] = "Please input tx hash.";
        return ret;
    }

    if (txHash.size() != 64)
    {
        ret["error"]["code"] = -2;
        ret["error"]["message"] = "hash is invalid.";
        return ret;
    }

    if (height == 0)
    {
        ret["error"]["code"] = -3;
        ret["error"]["message"] = "height is zero.";
        return ret;
    }

    g_RpcTransactionConfirm.add(txHash, TxConfirmation::DEFAULT_CONFIRM_TOTAL);
    if (SendConfirmTransaction(txHash, height, ConfirmRpcFlag, TxConfirmation::DEFAULT_CONFIRM_TOTAL) != 0)
    {
        ret["error"]["code"] = -4;
        ret["error"]["message"] = "Send Confirm transaction failed.";
        return ret;
    } 

    int i = 0;
    const int WAIT_TIMES = 5;
    do
    {
        sleep(3);
    } while (!g_RpcTransactionConfirm.is_confirm_ok(txHash) && i++ < WAIT_TIMES);

    std::vector<std::string> ids;
    g_RpcTransactionConfirm.get_ids(txHash,ids);
    ret["result"]["total"] = ids.size();
    nlohmann::json txid;
    int pos = 0;
    for(const auto &id:ids)
    {
        txid[pos++] = id;
    }

    ret["result"]["success"] = g_RpcTransactionConfirm.is_success(txHash);
    
    int success_count = g_RpcTransactionConfirm.get_count(txHash);
    int failed_count = g_RpcTransactionConfirm.get_failed_count(txHash);
    int total_count = success_count + failed_count;
    ret["result"]["count_success"] = success_count;
    ret["result"]["count_failed"] = failed_count;
    ret["result"]["count_total"] = total_count;
    ret["result"]["count_success_rate"] = g_RpcTransactionConfirm.get_success_rate(txHash);
    ret["result"]["nodeid"] = txid;
    g_RpcTransactionConfirm.remove(txHash);

    return ret;
}

//返回交易数据组成的json
static bool GetTxInfoJson(const CTransaction &utxo_tx, uint32_t height, nlohmann::json &json)
{
    std::string type;
    const std::string &strextra = utxo_tx.extra();
    if(!strextra.empty())
    {
        nlohmann::json extra = nlohmann::json::parse(strextra);
        type = extra["TransactionType"].get<std::string>();
    }
    json["type"] = type;
    json["hash"] = utxo_tx.hash();
    json["time"] = utxo_tx.time();
    json["height"] = height;
    std::vector<CTxout> txouts;
    std::vector<std::string> utxo_hashs;
    for (int i = 0; i < utxo_tx.vin_size(); i++)
    {
        CTxin vin = utxo_tx.vin(i);
        std::string utxo_hash = vin.prevout().hash();
        //遍历相关的交易是否已经添加
        bool flag = false;
        for(uint32_t j = 0; j < utxo_hashs.size(); j++)
        {
            if(utxo_hash == utxo_hashs.at(j))
                flag = true;
        }
        if(flag)
            continue;
        utxo_hashs.push_back(utxo_hash);
        std::string pub = vin.scriptsig().pub();
        std::string txaddr = GetBase58Addr(pub);

        json["vin"][i]["address"] = txaddr;
        json["vin"][i]["prev_hash"] = utxo_hash;
        uint64_t amount = TxHelper::GetUtxoAmount(utxo_hash, txaddr);
        json["vin"][i]["output_value"] = std::to_string((double)amount / DECIMAL_NUM);
    }
    for (int i = 0; i < utxo_tx.vout_size(); i++)
    {
        CTxout txout = utxo_tx.vout(i);
        json["vout"][i]["address"] = txout.scriptpubkey();
        json["vout"][i]["value"] = std::to_string((double)txout.value() / DECIMAL_NUM);
    }
    return true;
}

//查询质押和解质押，输入地址、条数，输出交易信息 GetTxInfoListAck
nlohmann::json jsonrpc_get_pledge_redeem_tx_by_addr(const nlohmann::json & param)
{
    nlohmann::json ret, errorret;
    std::string addr;
    if (param.find("addr") != param.end())
        param["addr"].get_to(addr);

    if (addr.empty())
    {
        errorret["error"]["code"] = -1;
        errorret["error"]["message"] = "Please input address.";
        return errorret;
    }

    std::vector<UtxoTx> utxt_out;
    if((!TransUtils::GetPledgeAndRedeemUtxoByAddr(addr, utxt_out)) || utxt_out.empty())
    {
        errorret["error"]["code"] = -3;
        errorret["error"]["message"] = "select utxo is empty";
        return errorret;
    }
    int i = 0;
    for(auto it = utxt_out.cbegin(); it != utxt_out.cend(); it++)
    {
        nlohmann::json json;
        if(GetTxInfoJson(it->utxo, it->height, json))
        {
           ret["result"][i++] = json;
        }
    }
    return ret;
}

//查询一定高度内指定账户的签名交易，输入起始高度，结束高度，账户，输出交易信息 GetTxInfoListAck
nlohmann::json jsonrpc_get_sign_tx_by_addr_and_height(const nlohmann::json & param)
{
    nlohmann::json ret, errorret;
    std::string addr;
    int start = -1, end = -1;
    if (param.find("addr") != param.end())
        param["addr"].get_to(addr);
    if (param.find("start") != param.end())
        param["start"].get_to(start);
    if (param.find("end") != param.end())
        param["end"].get_to(end);
    if (addr.empty())
    {
        errorret["error"]["code"] = -1;
        errorret["error"]["message"] = "Please input address.";
        return errorret;
    }
    if(start > end)
    {
        errorret["error"]["code"] = -2;
        errorret["error"]["message"] = "start > end .";
        return errorret;
    }
    std::vector<UtxoTx> utxt_out;
    if((!TransUtils::GetAwardUtxoByAddr(addr, utxt_out)) || utxt_out.empty())
    {
        errorret["error"]["code"] = -3;
        errorret["error"]["message"] = "select utxo is empty";
        return errorret;
    }
    int i = 0;
    for(auto it = utxt_out.cbegin(); it != utxt_out.cend(); it++)
    {
        if((-1 == start || it->height >= start) && (-1 == end || it->height <= end))
        {
            nlohmann::json json;
            if(GetTxInfoJson(it->utxo, it->height, json))
            {
                ret["result"][i++] = json;
            }
        }
    }
    return ret;
}

//查询一定高度内某账户向指定账户正常交易信息，输入起始高度，结束高度，交易账户，目标账户，输出交易信息 GetTxInfoListAck
nlohmann::json jsonrpc_get_tx_by_addr_and_height(const nlohmann::json & param)
{
    nlohmann::json ret, errorret;
    std::string fromaddr, toaddr;
    int start = -1, end = -1;
    if (param.find("fromaddr") != param.end())
        param["fromaddr"].get_to(fromaddr);
    if (param.find("toaddr") != param.end())
        param["toaddr"].get_to(toaddr);
    if (param.find("start") != param.end())
        param["start"].get_to(start);
    if (param.find("end") != param.end())
        param["end"].get_to(end);
    if (fromaddr.empty() || toaddr.empty())
    {
        errorret["error"]["code"] = -1;
        errorret["error"]["message"] = "Please input from and to address.";
        return errorret;
    }
    if(start > end)
    {
        errorret["error"]["code"] = -2;
        errorret["error"]["message"] = "start > end .";
        return errorret;
    }
    std::vector<UtxoTx> utxt_out;
    if((!TransUtils::GetTxUtxoByAddr(fromaddr, utxt_out)) || utxt_out.empty())
    {
        errorret["error"]["code"] = -3;
        errorret["error"]["message"] = "select utxo is empty";
        return errorret;
    }
    int i = 0;
    for(auto it = utxt_out.cbegin(); it != utxt_out.cend(); it++)
    {
        if((-1 == start || it->height >= start) && (-1 == end || it->height <= end))
        {
            bool flag = false;
            //判断输入中是否包含指定地址
            for (int i = 0; i < it->utxo.vin_size(); i++)
            {
                CTxin vin = it->utxo.vin(i);
                uint64_t amount = TxHelper::GetUtxoAmount(it->utxo.hash(), fromaddr);
                if(GetBase58Addr(vin.scriptsig().pub()) == fromaddr && amount > 0)
                {
                    flag = flag || true;
                    break;
                }
            }
            //判断输出中是否包含指定地址
            for (int i = 0; i < it->utxo.vout_size(); i++)
            {
                auto vout = it->utxo.vout(i);
                if(vout.scriptpubkey() == toaddr && vout.value() > 0)
                {
                    flag = flag && true;
                    break;
                }
            }
            if(!flag)
            {
                continue;
            }
            
            nlohmann::json json;
            if(GetTxInfoJson(it->utxo, it->height, json))
            {
                ret["result"][i++] = json;
            }
        }
    }
    if(0 == i)
    {
        errorret["error"]["code"] = -3;
        errorret["error"]["message"] = "select utxo is empty";
        return errorret;
    }
    return ret;
}

nlohmann::json jsonrpc_get_utxo(const nlohmann::json & param)
{
    nlohmann::json ret;

    std::string address;
    if (param.find("address") != param.end())
    {
        address = param["address"].get<std::string>();
    }

    if (address.empty() || !CheckBase58Addr(address))
    {
        ret["error"]["code"] = -1;
        ret["error"]["message"] = "address is invalid";
        return ret;
    }

    std::vector<TxHelper::Utxo> vectUtxos;
    if (GetUtxos(address, vectUtxos) != 0)
    {
        ret["error"]["code"] = -2;
        ret["error"]["message"] = "GetUtxos failed";
        return ret;
    }

    nlohmann::json utxos;
    for (auto & item : vectUtxos)
    {
        nlohmann::json utxo;
        utxo["hash"] = item.hash;
        utxo["value"] = item.value;
        utxo["n"] = item.n;
        utxos.push_back(utxo);
    }
   
    ret["result"]["address"] = address;
    ret["result"]["count"] = utxos.size();
    ret["result"]["utxo"] = utxos;

    return ret;
}


nlohmann::json jsonrpc_deploy_contract(const nlohmann::json & param)
{
    nlohmann::json ret;
    std::string owneraddr;
    std::string contract;
    std::string abi;
    std::string contract_raw;
    std::string abi_raw;
    std::string strFee;
    std::string deployvalue;
    std::string execnumber;
    std::string value;
    std::string mergename;
    std::string contractversion;
    std::string description;
    try
    {
        if ( param.find("contract") != param.end()  &&
             param.find("abi") != param.end()    &&
             param.find("owneraddr") != param.end() &&
             param.find("fee") != param.end() &&
             param.find("deployamount") != param.end() &&
             param.find("execnumber") != param.end() &&
             param.find("version") != param.end() &&
             param.find("description") != param.end()
            )
        {
            owneraddr = param["owneraddr"].get<std::string>();
            contract = param["contract"].get<std::string>();
            abi = param["abi"].get<std::string>();
            strFee = param["fee"].get<std::string>();
            deployvalue = param["deployamount"].get<std::string>();
            execnumber  = param["execnumber"].get<std::string>();
            contractversion = param["version"].get<std::string>();
            description = param["description"].get<std::string>();
            double fee = 0;
            istringstream issInputFee(strFee);
            issInputFee >> fee;
            fee = fee * DECIMAL_NUM;

            uint64_t amount = (std::stod(deployvalue) + FIX_DOUBLE_MIN_PRECISION) * DECIMAL_NUM;
            if(amount < g_deployamount)
            {
                {
                    ret["error"]["code"] = -32600;
                    ret["error"]["message"] = "the deploy of the contract amount less one uenc";
                    return ret;
                }
            }
            uint64_t impcnt = stoi(execnumber);
            if( impcnt <= g_MinExeCount )
            {
                impcnt = g_MinExeCount ;
            }

            wasm::read_and_validate_code(contract, contract_raw);
            wasm::read_and_validate_abi (abi, abi_raw);
            std::cout << "owneraddr:" << owneraddr << std::endl;
            std::cout << "fee:" << fee << std::endl;
            std::cout << "amount:" << amount << std::endl;
            std::cout << "impcnt:" << impcnt << std::endl;
           
            DBReadWriter  db_readerwriter;

            db_readerwriter.GetContractNameAbiName(value);
            string base64abi = base64Encode(abi_raw);
            mergename = contract  +  "_" + base64abi;
            
            if(!value.compare(mergename))
            {
                {
                    ret["error"]["code"] = -32601;
                    ret["error"]["message"] = "contract has been exist";
                    return ret;
                }
            }

            std::string tx_hash;
            TxHelper::DoDeployContract(owneraddr, contract, contract_raw, abi_raw, contractversion,description,g_MinNeedVerifyPreHashCount, fee,amount, impcnt,tx_hash); 
            ret["result"]["tx_hash"] = tx_hash;
        }
        else
        {
            throw std::exception();
        }  
    }
    catch(const std::exception& e)
    {
        ret["error"]["code"] = -32602;
        ret["error"]["message"] = "Invalid params";
        return ret;
    }
    return ret;
}




nlohmann::json jsonrpc_execute_contract(const nlohmann::json & param)
{
    nlohmann::json ret;
    std::string contract;
    std::string abi;
    std::string action;
    std::string params;
    try
    {
        if ( param.find("contract") != param.end()  &&
             param.find("abi") != param.end()     &&
             param.find("action") != param.end()     &&
             param.find("params") != param.end()
            )
        {
            contract = param["contract"].get<std::string>();
            abi = param["abi"].get<std::string>();
            action = param["action"].get<std::string>();
            params = param["params"].dump();
        }
        else
        {
            throw std::exception();
        }  
    }
    catch(const std::exception& e)
    {
        ret["error"]["code"] = -32602;
        ret["error"]["message"] = "Invalid params";
        return ret;
    }

    std::string executor;
    if(param.find("executor") != param.end())
    {
        executor = param["executor"].get<std::string>();
    }
    else
    {
        executor = "";
    }

    wasm::wasm_context cnt(wasm::ExecuteType::file, contract, abi, "","", action, params, 0, executor);
    cnt.execute();

    ret["result"] = 0;
    return ret;
}



nlohmann::json jsonrpc_execute_contract_rpc(const nlohmann::json & param)
{
    nlohmann::json ret;
    std::string addr;
    std::string contract;
    std::string action;
    std::string params;
    std::string strFee;
    double fee = 0;
    try
    {
        if ( param.find("addr") != param.end() &&
             param.find("contract") != param.end()  &&
             param.find("action") != param.end()     &&
             param.find("params") != param.end()  &&
             param.find("fee") != param.end()   
            )
        {
            addr = param["addr"].get<std::string>();
            contract = param["contract"].get<std::string>();
            action = param["action"].get<std::string>();
            params = param["params"].dump();
           

            strFee = param["fee"].get<std::string>();
            istringstream issInputFee(strFee);
            issInputFee >> fee;
            fee = fee * DECIMAL_NUM;

            std::string tx_hash;
            TxHelper::DoeExecuteContract(addr, contract, action, params, g_MinNeedVerifyPreHashCount, fee, tx_hash);
            ret["result"]["tx_hash"] = tx_hash;

        }
        else
        {
            throw std::exception();
        }  
    }
    catch(const std::exception& e)
    {
        ret["error"]["code"] = -32602;
        ret["error"]["message"] = "Invalid params";
        return ret;
    }

    return ret;
}



nlohmann::json jsonrpc_get_db_prefix_key(const nlohmann::json & param)
{
    nlohmann::json ret;
    std::string contract_name;
    try
    {
    	contract_name = param["contract_name"].get<std::string>();
    }
    catch(const std::exception& e)
    {
        ret["error"]["code"] = -32602;
        ret["error"]["message"] = "Invalid params";
        return ret;
    }

    // auto pRocksDb = MagicSingleton<Rocksdb>::GetInstance();
    // std::map<std::string, std::string> res = pRocksDb->prefix_key(contract_name);
    int i = 0;
    // for(const auto& data:res)
    // {

    //     ret["result"]["data"][i]["key"] =  data.first;
    //     ret["result"]["data"][i]["value"] =  data.second;
    //     i++;
    // }
    return ret;

}

nlohmann::json jsonrpc_get_contract_data(const nlohmann::json & param)
{
    nlohmann::json ret;
    std::string contract;
    std::string table;
    //todo: primary类型要根据合约abi来获取
    std::string primary;     
    try
    {


        if ( param.find("contract") != param.end()  &&
             param.find("table") != param.end()     &&
             param.find("primary") != param.end()
              )
        {

            contract = param["contract"].get<std::string>();
            table = param["table"].get<std::string>();
            primary = param["primary"].get<std::string>();
        }
        else
        {
            throw std::exception();
        }  
    }
    catch(const std::exception& e)
    {
        ret["error"]["code"] = -32602;
        ret["error"]["message"] = "Invalid params";
        return ret;
    }

    std::vector<char> prefix = wasm::pack(contract+ ".wasm");


    uint64_t scope = 0;
    std::vector<char> key;
    if(primary.size() == 0)
    {
        key = wasm::pack(std::tuple(wasm::name(table), scope));

    }else{
        key = wasm::pack(std::tuple(wasm::name(table), scope, primary));
    }
    
    std::string prefix_s =  string((const char *) prefix.data(), prefix.size()) + string((const char *) key.data(), key.size());

    // auto pRocksDb = MagicSingleton<Rocksdb>::GetInstance();
    // std::map<std::string, std::string> res = pRocksDb->prefix_key(prefix_s);
    // cout<<"prefix_s = "<<prefix_s<<endl;
    //  map<string, string>::iterator  iter;
    //  for(iter = res.begin();iter!=res.end();++iter)
    //  {
    //     cout<<"iter->first = "<<iter->first<<endl;
    //     cout<<"iter->second = "<<iter->second<<endl;
    //  }
    //获取abi
    std::string  abi;
    wasm::read_and_validate_abi (contract + ".abi", abi);
    std::vector<char> v_abi(abi.begin(),abi.end());

    ret["result"]["data"] = nlohmann::json::array();
    int i = 0;
    // for(const auto& data:res)
    // {
    //     std::vector<char> value_bytes(data.second.begin(), data.second.end());
    //     json_spirit::Value   value_json  = wasm::abi_serializer::unpack(v_abi, wasm::name(table).value, value_bytes, wasm::max_serialization_time);
    //     // json_spirit::Object& object_json = value_json.get_obj();
    //     std::string json_str = json_spirit::write(value_json);

    //     auto json_obj = nlohmann::json::parse(json_str);

    //     ret["result"]["data"][i] =  json_str;
    //     // ret["result"]["data"][i] =  json_obj;
    //     // ret["result"]["data"][i]["key"] =  data.first;
    //     // ret["result"]["data"][i]["value"] =  data.second;
    //     i++;
    // }
    return ret;
}