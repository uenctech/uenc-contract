#include "ca.h"

#include "unistd.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <random>
#include <map>
#include <array>
#include <fcntl.h>
#include <thread>
#include <shared_mutex>

#include "proto/interface.pb.h"
#include "proto/ca_test.pb.h"

#include "include/net_interface.h"
#include "net/net_api.h"
#include "net/node_cache.h"
#include "net/ip_port.h"
#include "common/devicepwd.h"
#include "common/setting_wizard.h"
#include "utils/qrcode.h"
#include "utils/string_util.h"
#include "utils//util.h"
#include "utils/time_util.h"
#include "db/db_api.h"
#include "ca_block_http_callback.h"
#include "ca_device.h"
#include "ca_hexcode.h"
#include "ca_message.h"
#include "ca_coredefs.h"
#include "ca_console.h"
#include "ca_base64.h"
#include "ca_blockpool.h"
#include "ca_txhelper.h"
#include "ca_test.h"
#include "ca_AwardAlgorithm.h"
#include "ca_rollback.h"
#include "ca_buffer.h"
#include "ca_transaction.h"
#include "ca_global.h"
#include "ca_interface.h"
#include "ca_test.h"
#include "ca_device.h"
#include "ca_header.h"
#include "ca_clientinfo.h"
#include "ca_MultipleApi.h"
#include "ca_synchronization.h"
#include "ca_txhelper.h"
#include "ca_bip39.h"
#include "ca_txvincache.h"
#include "ca_txfailurecache.h"
#include "ca_txconfirmtimer.h"
#include "ca_http_api.h"
#include "ca_pwdattackchecker.h"
#include "ca/sync_block.h"
#include "ca_pledgecache.h"
#include "ca/ca_black_list_cache.h"

const char* kVersion = "1.0.1";
std::shared_mutex g_NodeInfoLock;
bool bStopTx = false;
bool bIsCreateTx = false;

void InitPeerNodeIdentity();

std::string PrintTime(uint64_t time)
{
    time_t s = (time_t)time / 1000000;
    struct tm* gm_date;
    gm_date = localtime(&s);
    char ps[512] = {};
    sprintf(ps, "%d-%d-%d %d:%d:%d (%zu) ", gm_date->tm_year + 1900, gm_date->tm_mon + 1, gm_date->tm_mday, gm_date->tm_hour, gm_date->tm_min, gm_date->tm_sec, time);
    return string(ps);
}

int GetAllAccountSignNum()
{
    std::map<std::string, uint64_t> addrSignNum;
    ofstream file("sign.txt");
    if (!file.is_open())
    {
        ERRORLOG("Open file sign.txt failed!");
        return -2;
    }

    uint64_t blockHeight = 0;
    DBReader db_reader;
    auto db_status = db_reader.GetBlockTop(blockHeight);
    if (db_status != 0)
    {
        ERRORLOG("GetBlockTop failed!");
        return -3;
    }

    // 本地节点高度为0时
    if (blockHeight <= 0)
    {
        ERRORLOG("The top is 0!");
        return -4;
    }

    for (unsigned int height = 1; height <= blockHeight; height++)
    {
        std::vector<std::string> hashs;
        db_status = db_reader.GetBlockHashsByBlockHeight(height, hashs);
        if (db_status != 0)
        {
            ERRORLOG("GetBlockHashsByBlockHeight failed!");
            return -3;
        }

        for (auto blockHash : hashs)
        {
            std::string blockHeaderStr;
            db_status = db_reader.GetBlockByBlockHash(blockHash, blockHeaderStr);
            if (db_status != 0)
            {
                ERRORLOG("GetBlockByBlockHash failed!");
                return -3;
            }

            CBlock cblock;
            cblock.ParseFromString(blockHeaderStr);

            for (int txCount = 0; txCount < cblock.txs_size(); txCount++)
            {
                CTransaction transaction = cblock.txs(txCount);
                if (CheckTransactionType(transaction) == kTransactionType_Award)
                {
                    CTransaction copyTx = transaction;
                    for (int voutCount = 0; voutCount != copyTx.vout_size(); ++voutCount)
                    {
                        CTxout txout = copyTx.vout(voutCount);
                        auto iter = addrSignNum.find(txout.scriptpubkey());
                        if (iter != addrSignNum.end())
                        {
                            int tmpValue = iter->second;
                            tmpValue += txout.value();
                            addrSignNum.insert(make_pair(iter->first, iter->second));
                        }
                        else
                        {
                            addrSignNum.insert(make_pair(txout.scriptpubkey(), txout.value()));
                        }
                    }
                }
            }
        }
    }

    file << std::string("账号总数：");
    file << std::to_string(addrSignNum.size()) << "\n";
    for (auto addrSignAmt : addrSignNum)
    {
        file << addrSignAmt.first;
        file << ",";
        file << addrSignAmt.second;
        file << "\n";
    }

    file.close();
    return 0;
}

bool isInteger(string str)
{
    //判断没有输入的情况
    if (str == "")
    {
        return false;
    }
    else
    {
        //有输入的情况况
        for (size_t i = 0; i < str.size(); i++)
        {
            if (str.at(i) == '-' && str.size() > 1) // 有可能出现负数
                continue;
            // 数值在ascii码（编码）的‘0’-‘9’之间
            if (str.at(i) > '9' || str.at(i) < '0')
                return false;
        }
        return true;
    }
}

int InitRocksDb()
{
    if (!DBInit("./data.db"))
    {
        return -1;
    }
    DBReadWriter db_read_writer;
    std::string bestChainHash;
    if (db_read_writer.GetBestChainHash(bestChainHash) && bestChainHash.empty())
    {
        //  初始化0块, 这里简化处理直接读取关键信息保存到数据库中
        std::string strBlockHeader0;
        if (0 == g_testflag)
        {
            strBlockHeader0 = "08011240306162656230393963333866646635363363356335396231393365613738346561643530343938623135656264613639376432333264663134386362356237641a40303030303030303030303030303030303030303030303030303030303030303030303030303030303030303030303030303030303030303030303030303030302a403432613165336664353035666532613764343032366533326430633266323537303864633464616338343739366366373237343766326238353433653135623232b60208011099f393bcaae0ea0222223136707352697037385176557275517239664d7a7238456f6d74465331625661586b3a40343261316533666435303566653261376434303236653332643063326632353730386463346461633834373936636637323734376632623835343365313562324294010a480a403030303030303030303030303030303030303030303030303030303030303030303030303030303030303030303030303030303030303030303030303030303010ffffffff0f12420a4047454e4553495320202020202020202020202020202020202020202020202020202020202020202020202020202020202020202020202020202020202020202018ffffffff0f4a2c088080feeabaa41b12223136707352697037385176557275517239664d7a7238456f6d74465331625661586b3a240a223136707352697037385176557275517239664d7a7238456f6d74465331625661586b5099f393bcaae0ea02";
        }
        else if (1 == g_testflag)
        {
            strBlockHeader0 = "08011240613938396462353936653637653538343563313065313532333231613830646362353730663732386361323033303834336363613662646261393837663664621a40303030303030303030303030303030303030303030303030303030303030303030303030303030303030303030303030303030303030303030303030303030302a403535626137366362616136653964646261333639646262633635363331373837633435646235643538356361333461366361386433313066623965313539343532b602080110909fcaf8b8bef20222223139636a4a4e367071456a777456507a70506d4d3756696e4d7458564b5a584544753a40353562613736636261613665396464626133363964626263363536333137383763343564623564353835636133346136636138643331306662396531353934354294010a480a403030303030303030303030303030303030303030303030303030303030303030303030303030303030303030303030303030303030303030303030303030303010ffffffff0f12420a4047454e4553495320202020202020202020202020202020202020202020202020202020202020202020202020202020202020202020202020202020202020202018ffffffff0f4a2c088080e983b1de1612223139636a4a4e367071456a777456507a70506d4d3756696e4d7458564b5a584544753a240a223030303030303030303030303030303030303030303030303030303030303030303050909fcaf8b8bef202";
        }
        else
        {
            strBlockHeader0 = "08011240663431336131653831396533376335643538346331636662353835616161343364386339383133663464396633613834316537393065306136366530366134661a40303030303030303030303030303030303030303030303030303030303030303030303030303030303030303030303030303030303030303030303030303030302a403835666466326630663834656331313234626132626336623239613665373431383134303366383232326665393232303435633036653339303165616131343532b402080110a69995f284dbeb02222131766b533436516666654d3473444d42426a754a4269566b4d514b59375a3854753a40383566646632663066383465633131323462613262633662323961366537343138313430336638323232666539323230343563303665333930316561613134354294010a480a403030303030303030303030303030303030303030303030303030303030303030303030303030303030303030303030303030303030303030303030303030303010ffffffff0f12420a4047454e4553495320202020202020202020202020202020202020202020202020202020202020202020202020202020202020202020202020202020202020202018ffffffff0f4a2b088080e983b1de16122131766b533436516666654d3473444d42426a754a4269566b4d514b59375a3854753a240a223030303030303030303030303030303030303030303030303030303030303030303050a69995f284dbeb02";
        }

        int size = strBlockHeader0.size();
        char* buf = new char[size] {0};
        size_t outLen = 0;
        decode_hex(buf, size, strBlockHeader0.c_str(), &outLen);

        CBlock block;
        block.ParseFromString(std::string(buf));
        delete[] buf;

        if (block.txs_size() == 0)
        {
            return -2;
        }
        CTransaction tx = block.txs(0);
        if (tx.vout_size() == 0)
        {
            return -3;
        }

        db_read_writer.SetBlockHeightByBlockHash(block.hash(), block.height());
        db_read_writer.SetBlockHashByBlockHeight(block.height(), block.hash(), true);
        db_read_writer.SetBlockByBlockHash(block.hash(), block.SerializeAsString());
        unsigned int height = 0;
        db_read_writer.SetBlockTop(height);
        db_read_writer.SetBestChainHash(block.hash());
        db_read_writer.SetUtxoHashsByAddress(tx.owner(), tx.hash());
        db_read_writer.SetTransactionByHash(tx.hash(), tx.SerializeAsString());
        db_read_writer.SetBlockHashByTransactionHash(tx.hash(), block.hash());
        unsigned int txIndex = 0;
        db_read_writer.SetTransactionByAddress(tx.owner(), txIndex, tx.SerializeAsString());
        db_read_writer.SetTransactionTopByAddress(tx.owner(), txIndex);
        CTxout vout = tx.vout(0);
        db_read_writer.SetBalanceByAddress(tx.owner(), vout.value());
        db_read_writer.SetAllTransactionByAddress(tx.owner(), tx.hash());

        CBlockHeader header;
        header.set_height(0);
        header.set_prevhash(block.prevhash());
        header.set_hash(block.hash());
        header.set_time(block.time());
        db_read_writer.SetBlockHeaderByBlockHash(header.hash(), header.SerializeAsString());
    }
    db_read_writer.SetInitVer(getVersion());

    if (DBStatus::DB_SUCCESS != db_read_writer.TransactionCommit())
    {
        ERRORLOG("(rocksdb init) TransactionCommit failed !");
        return -4;
    }
    return 0;
}

void test_time()
{
    tm tmp_time;

    string date_s, date_e;
    cout << "请输入开始日期(格式 2020-01-01): ";
    cin >> date_s;
    date_s += " 00:00:00";
    strptime(date_s.c_str(), "%Y-%m-%d %H:%M:%S", &tmp_time);
    uint64_t t_s = (uint64_t)mktime(&tmp_time);
    cout << t_s << endl;

    cout << "请输入结束日期(格式 2020-01-07): ";
    cin >> date_e;
    date_e += " 23:59:59";
    strptime(date_e.c_str(), "%Y-%m-%d %H:%M:%S", &tmp_time);
    uint64_t t_e = (uint64_t)mktime(&tmp_time);
    cout << t_e << endl;

    DBReader db_reader;
    uint64_t top = 0;
    db_reader.GetBlockTop(top);

    map<string, uint64_t> addr_award;
    std::vector<std::string> hashs;
    bool b{ false };

    while (true)
    {
        auto db_status = db_reader.GetBlockHashsByBlockHeight(top, hashs);
        for (auto v : hashs)
        {
            std::string header;
            db_status = db_reader.GetBlockByBlockHash(v, header);
            DEBUGLOG("GetBlockByBlockHash ret {}", db_status);
            CBlock cblock;
            cblock.ParseFromString(header);
            for (auto i = 2; i < cblock.txs_size(); i += 3)
            {
                CTransaction tx = cblock.txs(i);
                if (tx.time() / 1000000 > t_s && tx.time() / 1000000 < t_e)
                {
                    for (int j = 0; j < tx.vout_size(); j++)
                    {
                        CTxout txout = tx.vout(j);
                        addr_award[txout.scriptpubkey()] += txout.value();
                    }
                }

                if (tx.time() / 1000000 < t_s)
                {
                    b = true;
                }
            }

            if (b)
            {
                break;
            }
        }
        hashs.clear();
        if (b)
        {
            break;
        }
        top--;
    }

    for (auto v : addr_award)
    {
        cout << "addr " << v.first << " award " << v.second << endl;
    }
}

void InitTxCount()
{
    size_t b_count = 0;
    DBReadWriter db_readwriter;
    db_readwriter.GetTxCount(b_count);
    if (!b_count)
    {
        uint64_t top = 0;
        db_readwriter.GetBlockTop(top);
        auto amount = to_string(top);

        std::vector<std::string> vBlockHashs;

        for (size_t i = top; i > 0; --i)
        {
            db_readwriter.GetBlockHashsByBlockHeight(i, vBlockHashs);
            b_count += vBlockHashs.size();
            //cout << "height------- " << i << endl;
            //cout << "count======= " << vBlockHashs.size() << endl;
            vBlockHashs.clear();
        }
        db_readwriter.SetTxCount(b_count);
        db_readwriter.SetGasCount(b_count);
        db_readwriter.SetAwardCount(b_count);
    }
}

int ca_startTimerTask()
{
    //加块线程
    g_blockpool_timer.AsyncLoop(1000, []()
        { MagicSingleton<BlockPoll>::GetInstance()->Process(); });

    //区块同步线程
    //*
        Singleton<SyncBlock>::get_instance()->ThreadStart();
    //*/
    /*
    int sync_time = Singleton<Config>::get_instance()->GetSyncDataPollTime();
    cout << "sync_time = " << sync_time << endl;
    g_synch_timer.AsyncLoop(sync_time * 1000, []()
        { g_synch->Process(); });
    //*/

    //获取矿机在线时长
    g_deviceonline_timer.AsyncLoop(5 * 60 * 1000, GetOnLineTime);

    // Public node refresh to config file, 20201110
    g_public_node_refresh_timer.AsyncLoop(60 * 60 * 1000, []()
        { UpdatePublicNodeToConfigFile(); });

    MagicSingleton<TxVinCache>::GetInstance()->Process();

    MagicSingleton<TransactionConfirmTimer>::GetInstance()->timer_start();

    // Run http callback
    string url = Singleton<Config>::get_instance()->GetHttpCallbackIp();
    int port = Singleton<Config>::get_instance()->GetHttpCallbackPort();
    string path = Singleton<Config>::get_instance()->GetHttpCallbackPath();
    if (!url.empty() && port > 0)
    {
        MagicSingleton<CBlockHttpCallback>::GetInstance()->Start(url, port, path);
    }
    else
    {
        cout << "Http callback is not config!" << endl;
    }

    // Start pledge cache, 20211020  Liu
    MagicSingleton<PledgeCache>::GetInstance()->Start();

    return 0;
}

bool ca_init()
{
    // 初始化账户
    InitAccount(&g_AccountInfo, "./cert");

    // 初始化数据库
    InitRocksDb();

    //手机端接口注册
    MuiltipleApi();

    // 初始化矿费和打包费设置
    InitFee();

    // 向网络层注册接口
    RegisterCallback();

    // 注册http相关接口
    ca_register_http_callbacks();

    // 初始化交易统计
    InitTxCount();

    // 启动定时器任务
    ca_startTimerTask();

    //NTP校验  
    checkNtpTime();

    Singleton<Config>::get_instance()->UpdateNewConfig();

    InitPeerNodeIdentity();

    return true;
}

void InitPeerNodeIdentity()
{
    string strPub;
    g_AccountInfo.GetPubKeyStr(g_AccountInfo.DefaultKeyBs58Addr.c_str(), strPub);
    Singleton<PeerNode>::get_instance()->set_self_id(g_AccountInfo.DefaultKeyBs58Addr);
    Singleton<PeerNode>::get_instance()->set_self_pub(strPub);
    Singleton<PeerNode>::get_instance()->set_self_height();
}

int ca_endTimerTask()
{
    g_blockpool_timer.Cancel();

    g_synch_timer.Cancel();

    g_deviceonline_timer.Cancel();

    g_public_node_refresh_timer.Cancel();

    // Stop pledge timer, 20211103  Liu
    MagicSingleton<PledgeCache>::GetInstance()->Stop();

    return true;
}

void ca_cleanup()
{
    ca_endTimerTask();
    DBDestory();
}

const char* ca_version()
{
    return kVersion;
}

// 获取某个节点的最高块的高度和hash
void SendDevInfoReq(const std::string id)
{
    GetDevInfoReq getDevInfoReq;
    getDevInfoReq.set_version(getVersion());

    std::string ownID = net_get_self_node_id();
    getDevInfoReq.set_id(ownID);

    net_send_message<GetDevInfoReq>(id, getDevInfoReq);
}

int HandleGetDevInfoReq(const std::shared_ptr<GetDevInfoReq>& msg, const MsgData& msgdata)
{
    DEBUGLOG("recv HandleGetDevInfoReq!");

    // 判断版本是否兼容
    if (0 != Util::IsVersionCompatible(msg->version()))
    {
        ERRORLOG("HandleExitNode IsVersionCompatible");
        return 0;
    }

    // 版本
    GetDevInfoAck getDevInfoAck;
    getDevInfoAck.set_version(getVersion());

    std::string ownID = net_get_self_node_id();
    getDevInfoAck.set_id(ownID);

    DBReader db_reader;
    // 高度
    uint64_t blockHeight = 0;
    auto status = db_reader.GetBlockTop(blockHeight);
    if (DBStatus::DB_SUCCESS != status)
    {
        ERRORLOG("HandleGetDevInfoReq GetBlockTop failed !");
        return 0;
    }
    getDevInfoAck.set_height(blockHeight);

    // bestchain
    std::string blockHash;
    status = db_reader.GetBestChainHash(blockHash);
    if (DBStatus::DB_SUCCESS != status)
    {
        ERRORLOG("HandleGetDevInfoReq GetBestChainHash failed !");
        return 0;
    }
    getDevInfoAck.set_hash(blockHash);

    // base58地址
    getDevInfoAck.set_base58addr(g_AccountInfo.DefaultKeyBs58Addr);

    net_send_message<GetDevInfoAck>(msg->id(), getDevInfoAck);
    return 0;
}

int HandleGetDevInfoAck(const std::shared_ptr<GetDevInfoAck>& msg, const MsgData& msgdata)
{
    // 版本判断
    if (0 != Util::IsVersionCompatible(msg->version()))
    {
        ERRORLOG("HandleExitNode IsVersionCompatible");
        return 0;
    }

    g_NodeInfoLock.lock();
    g_nodeinfo.push_back(*msg);
    g_NodeInfoLock.unlock();
    return 0;
}

void ca_print_basic_info()
{
    std::string version = getVersion();
    std::string base58 = g_AccountInfo.DefaultKeyBs58Addr;

    uint64_t balance = 0;
    GetBalanceByUtxo(base58,balance);
    uint64_t minerFee = 0;
    DBReader db_reader;

    db_reader.GetDeviceSignatureFee(minerFee);

    uint64_t packageFee = 0;
    db_reader.GetDevicePackageFee(packageFee);

    uint64_t blockHeight = 0;
    db_reader.GetBlockTop(blockHeight);

    std::string bestChainHash;
    db_reader.GetBestChainHash(bestChainHash);

    std::string ownID = net_get_self_node_id();

    ca_console infoColor(kConsoleColor_Green, kConsoleColor_Black, true);
    cout << infoColor.color();
    cout << "*********************************************************************************" << endl;
    cout << "Version: " << version << endl;
    cout << "Base58: " << base58 << endl;
    cout << "Balance: " << balance << endl;
    cout << "Signature Fee: " << minerFee << endl;
    cout << "Package Fee: " << packageFee << endl;
    cout << "Block top: " << blockHeight << endl;
    cout << "Top block hash: " << bestChainHash << endl;
    cout << "*********************************************************************************" << endl;
    cout << infoColor.reset();
}

int get_device_signature_fee(uint64_t& minerFee)
{
    DBReader reader;
    int result = reader.GetDeviceSignatureFee(minerFee);
    return result;
}

int set_device_signature_fee(uint64_t minerFee)
{
    DBReadWriter db_writer;

    if (DBStatus::DB_SUCCESS != db_writer.SetDeviceSignatureFee(minerFee))
    {
        ERRORLOG("set device signature fee failed");
        return -1;
    }

    net_update_fee_and_broadcast(minerFee);

    if (DBStatus::DB_SUCCESS != db_writer.TransactionCommit())
    {
        ERRORLOG("db commit failed");
        return -2;
    }

    return 0;
}

int get_device_package_fee(uint64_t& packageFee)
{
    DBReader db_reader;
    int result = db_reader.GetDevicePackageFee(packageFee);
    return result;
}

int set_device_package_fee(uint64_t packageFee)
{
    DBReadWriter db_writer;

    if (DBStatus::DB_SUCCESS != db_writer.SetDevicePackageFee(packageFee))
    {
        ERRORLOG("set device package fee failed");
        return -1;
    }

    net_update_package_fee_and_broadcast(packageFee);

    if (DBStatus::DB_SUCCESS != db_writer.TransactionCommit())
    {
        ERRORLOG("db commit failed");
        return -2;
    }

    return 0;
}

// markhere:modify
void handle_transaction()
{
    std::cout << std::endl << std::endl;

    std::string strFromAddr;
    std::cout << "input FromAddr :" << std::endl;
    std::cin >> strFromAddr;

    std::string strToAddr;
    std::cout << "input ToAddr :" << std::endl;
    std::cin >> strToAddr;

    std::string strAmt;
    std::cout << "input amount :" << std::endl;
    std::cin >> strAmt;
    std::regex pattern("^\\d+(\\.\\d+)?$");
    if (!std::regex_match(strAmt, pattern))
    {
        std::cout << "input amount error ! " << std::endl;
        return;
    }

    std::string strNeedVerifyPreHashCount;
    std::cout << "input needVerifyPreHashCount :" << std::endl;
    std::cin >> strNeedVerifyPreHashCount;
    pattern = "^[1-9]\\d*$";
    if (!std::regex_match(strNeedVerifyPreHashCount, pattern))
    {
        std::cout << "input needVerifyPreHashCount error ! " << std::endl;
        return;
    }

    std::string strMinerFees;
    std::cout << "input minerFees :" << std::endl;
    std::cin >> strMinerFees;
    pattern = "^\\d+(\\.\\d+)?$";
    if (!std::regex_match(strMinerFees, pattern))
    {
        std::cout << "input minerFees error ! " << std::endl;
        return;
    }
    std::vector<std::string> fromAddr;
    fromAddr.emplace_back(strFromAddr);
    uint64_t amount = (std::stod(strAmt) + FIX_DOUBLE_MIN_PRECISION) * DECIMAL_NUM;
    std::map<std::string, int64_t> toAddrAmount;
    toAddrAmount[strToAddr] = amount;
    uint32_t needVerifyPreHashCount = std::stoul(strNeedVerifyPreHashCount);
    uint64_t minerFeesConvert =  (stod(strMinerFees) + FIX_DOUBLE_MIN_PRECISION )* DECIMAL_NUM ;
    
	CTransaction outTx;
    std::vector<TxHelper::Utxo> outVin;
	if (TxHelper::CreateTxTransaction(fromAddr, toAddrAmount, needVerifyPreHashCount, minerFeesConvert, outTx, outVin) != 0)
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
	txMsg.set_trycountdown(CalcTxTryCountDown(needVerifyPreHashCount));

	auto msg = make_shared<TxMsg>(txMsg);

	std::string txHash;
	int ret = DoHandleTx(msg, txHash);
	DEBUGLOG("Transaction result，ret:{}  txHash：{}", ret, txHash);

    return;
}

// markhere:modify
void handle_pledge()
{
    std::cout << std::endl << std::endl;

    std::string strFromAddr = GetDefault58Addr();
    std::cout << "pledge addr: " << strFromAddr << std::endl;
    std::string strPledgeFee;
    std::cout << "Please enter the amount to pledge：" << std::endl;
    std::cin >> strPledgeFee;
    std::regex pattern("^\\d+(\\.\\d+)?$");
    if (!std::regex_match(strPledgeFee, pattern))
    {
        std::cout << "input pledge fee error " << std::endl;
        return;
    }

    uint32_t nPledgeType;
    std::cout << "Please enter pledge type: (0: node, 1: public) " << std::endl;
    std::cin >> nPledgeType;
    TxHelper::PledgeType pledgeType;
    if (nPledgeType == 0)
    {
        pledgeType = TxHelper::PledgeType::kPledgeType_Node;
    }
    else if (nPledgeType == 1)
    {
        pledgeType = TxHelper::PledgeType::kPledgeType_PublicNode;
    }
    else
    {
        std::cout << "input pledge type error " << std::endl;
        return;
    }

    std::string strGasFee;
    std::cout << "Please enter GasFee:" << std::endl;
    std::cin >> strGasFee;
    pattern = "^\\d+(\\.\\d+)?$";
    if (!std::regex_match(strGasFee, pattern))
    {
        std::cout << "input gas fee error " << std::endl;
        return;
    }

    std::string password;
    std::cout << "password" << std::endl;
    std::cin >> password;

    // 判断矿机密码是否正确
    std::string hashOriPass = generateDeviceHashPassword(password);
    std::string targetPassword = Singleton<DevicePwd>::get_instance()->GetDevPassword();
    if (hashOriPass != targetPassword) 
    {
        return;
    }

    uint64_t GasFee = std::stod(strGasFee) * DECIMAL_NUM;
    uint64_t pledge_amount = std::stod(strPledgeFee) * DECIMAL_NUM;
	
    CTransaction outTx;
    std::vector<TxHelper::Utxo> outVin;
	if(TxHelper::CreatePledgeTransaction(strFromAddr, pledge_amount, g_MinNeedVerifyPreHashCount, GasFee, pledgeType, outTx, outVin) != 0)
	{
		return;
	}

	std::string serTx = outTx.SerializeAsString();

	size_t encodeLen = serTx.size() * 2 + 1;
	unsigned char encode[encodeLen] = {0};
	memset(encode, 0, encodeLen);
	long codeLen = base64_encode((unsigned char *)serTx.data(), serTx.size(), encode);
	std::string encodeStr( (char *)encode, codeLen );

	std::string encodeStrHash = getsha256hash(encodeStr);

	std::string signature;
	std::string strPub;
    if (GetSignString(strFromAddr, encodeStrHash, signature, strPub) != 0)
    {
        return;
    }

	for (int i = 0; i < outTx.vin_size(); i++)
	{
		CTxin * txin = outTx.mutable_vin(i);
		txin->mutable_scriptsig()->set_sign(signature);
		txin->mutable_scriptsig()->set_pub(strPub);
	}

	serTx = outTx.SerializeAsString();

	TxMsg txMsg;
    txMsg.set_version( getVersion() );
	txMsg.set_tx(serTx);
	txMsg.set_txencodehash( encodeStrHash );

    DBReader db_reader;
	uint64_t top = 0;
	int db_status = db_reader.GetBlockTop(top);
    if (DBStatus::DB_SUCCESS != db_status)
    {
        return;
    }
	txMsg.set_top(top);

    std::string blockHash;
    if (DBStatus::DB_SUCCESS != db_reader.GetBestChainHash(blockHash) )
    {
        return;
    }
    txMsg.set_prevblkhash(blockHash);
    txMsg.set_trycountdown(CalcTxTryCountDown(g_MinNeedVerifyPreHashCount));

	auto msg = make_shared<TxMsg>(txMsg);
    std::string txHash;
    int ret = DoHandleTx(msg, txHash);
    if (ret != 0)
	{
		ret -= 100;
	}
	DEBUGLOG("DoHandleTx ret:{}", ret);
}

// markhere:modify
void handle_redeem_pledge()
{
    std::cout << std::endl << std::endl;

    std::string strFromAddr;
    std::cout << "Please enter addr：" << std::endl;
    std::cin >> strFromAddr;

    DBReader db_reader;
    std::vector<string> utxos;
    db_reader.GetPledgeAddressUtxo(strFromAddr, utxos);
    std::reverse(utxos.begin(), utxos.end());
    std::cout << "-- Current pledge amount: -- " << std::endl;
    for (auto& utxo : utxos)
    {
        std::cout << "utxo: " << utxo << std::endl;
    }
    std::cout << std::endl;

    std::string strUtxoHash;
    std::cout << "utxo：";
    std::cin >> strUtxoHash;

    std::string strGasFee;
    std::cout << "GasFee：";
    std::cin >> strGasFee;

    std::string strPassword;
    std::cout << "password";
    std::cin >> strPassword;

    // 判断矿机密码是否正确
    std::string hashOriPass = generateDeviceHashPassword(strPassword);
    std::string targetPassword = Singleton<DevicePwd>::get_instance()->GetDevPassword();
    if (hashOriPass != targetPassword)
    {
        return;
    }

	uint64_t GasFee = std::stod(strGasFee) * DECIMAL_NUM;

	CTransaction outTx;
    std::vector<TxHelper::Utxo> outVin;
	if(TxHelper::CreateRedeemTransaction(strFromAddr,strUtxoHash, g_MinNeedVerifyPreHashCount, GasFee, outTx, outVin) != 0)
	{
        return;
	}

	std::string serTx = outTx.SerializeAsString();

	size_t encodeLen = serTx.size() * 2 + 1;
	unsigned char encode[encodeLen] = {0};
	memset(encode, 0, encodeLen);
	long codeLen = base64_encode((unsigned char *)serTx.data(), serTx.size(), encode);
	std::string encodeStr( (char *)encode, codeLen );

	std::string encodeStrHash = getsha256hash(encodeStr);

	std::string signature;
	std::string strPub;
	if (GetSignString(strFromAddr, encodeStrHash, signature, strPub) != 0)
    {
        return;
    }

	for (int i = 0; i < outTx.vin_size(); i++)
	{
		CTxin * txin = outTx.mutable_vin(i);
		txin->mutable_scriptsig()->set_sign(signature);
		txin->mutable_scriptsig()->set_pub(strPub);
	}

	serTx = outTx.SerializeAsString();

	TxMsg txMsg;
	txMsg.set_version( getVersion() );

	txMsg.set_tx(serTx);
	txMsg.set_txencodehash( encodeStrHash );

	uint64_t top = 0;
    if(DBStatus::DB_SUCCESS != db_reader.GetBlockTop(top))
    {
        return;
    }
	txMsg.set_top(top);

    std::string blockHash;
    if (DBStatus::DB_SUCCESS != db_reader.GetBestChainHash(blockHash) )
    {
        return;
    }
    txMsg.set_prevblkhash(blockHash);
    txMsg.set_trycountdown(CalcTxTryCountDown(g_MinNeedVerifyPreHashCount));

	auto msg = make_shared<TxMsg>(txMsg);
    std::string txHash;
    int ret = DoHandleTx(msg, txHash);
	if (ret != 0)
	{
		ret -= 100;
	}
	DEBUGLOG("DoHandleTx ret:{}", ret);
}

void handle_change_password()
{
    std::cout << std::endl
        << std::endl;

    std::string old_pass;
    std::cout << "Please input the old password: ";
    std::cin >> old_pass;

    std::string new_pass;
    std::cout << "Please input the new password: ";
    std::cin >> new_pass;

    if (old_pass.empty() || new_pass.empty())
    {
        std::cout << "The password cannot be empty." << endl;
        return;
    }

    if (!isDevicePasswordValid(old_pass))
    {
        std::cout << "The old password is invalid." << endl;
        return;
    }

    if (!isDevicePasswordValid(new_pass))
    {
        std::cout << "The new password is invalid." << endl;
        return;
    }

    if (old_pass == new_pass)
    {
        std::cout << "The new password cannot be the same as the old one" << endl;
        return;
    }

    std::string hashOriPass = generateDeviceHashPassword(old_pass);
    std::string targetPassword = Singleton<DevicePwd>::get_instance()->GetDevPassword();
    auto pCPwdAttackChecker = MagicSingleton<CPwdAttackChecker>::GetInstance();

    uint32_t secondscount;
    bool retval = pCPwdAttackChecker->IsOk(secondscount);
    if (retval == false)
    {
        ERRORLOG("Wrong input for 3 consecutive times. Please re-enter after {} seconds.", secondscount);
        std::cout << "Wrong input for 3 consecutive times. Please re-enter after " << secondscount << " seconds." << endl;
        return;
    }

    if (hashOriPass.compare(targetPassword))
    {
        DEBUGLOG("Number of times to start recording due to incorrect password input");
        if (pCPwdAttackChecker->Wrong())
        {
            ERRORLOG("Password input error.");
            std::cout << "The old password is incorrect." << endl;
            return;
        }
        else
        {
            ERRORLOG("Wrong password for the third time.");
            std::cout << "The three inputs is incorrect." << endl;
            return;
        }
    }
    else
    {
        DEBUGLOG("Reset to 0.");
        pCPwdAttackChecker->Right();
    }

    if (hashOriPass != targetPassword)
    {
        std::cout << "Incorrect old password." << endl;
        return;
    }

    std::string hashNewPass = generateDeviceHashPassword(new_pass);
    if (!Singleton<DevicePwd>::get_instance()->SetDevPassword(hashNewPass))
    {
        std::cout << "Unknown error." << endl;
    }
    else
    {
        std::cout << "Password change was successful." << endl;
    }

    return;
}

void handle_set_config()
{
    std::cout << std::endl
        << std::endl;
    Singleton<SettingWizard>::get_instance()->AskWizard();
}

void handle_set_signature_fee()
{
    std::cout << std::endl
        << std::endl;
    uint64_t signatureFee = 0;
    get_device_signature_fee(signatureFee);
    std::cout << "Current signature fee: " << signatureFee << std::endl;

    std::string strSignatureFee;
    std::cout << "< Hint: any positive integer from 1000 to 100000 >" << std::endl;
    std::cout << "Set signature fee: ";
    std::cin >> strSignatureFee;
    std::regex pattern("^[1-9]\\d*$");

    if (!std::regex_match(strSignatureFee, pattern))
    {
        std::cout << "Input invalid." << std::endl;
        return;
    }
    signatureFee = std::stoull(strSignatureFee);

    if (signatureFee < 1000 || signatureFee > 100000)
    {
        std::cout << "Input invalid." << std::endl;
        return;
    }

    if (set_device_signature_fee(signatureFee) == 0)
    {
        std::cout << "Signature fee is successful to set: " << signatureFee << "." << std::endl;
    }
    else
    {
        std::cout << "Signature fee is failed to set." << std::endl;
    }
}

void handle_set_package_fee()
{
    std::cout << std::endl
        << std::endl;

    uint64_t packageFee = 0;
    get_device_package_fee(packageFee);
    std::cout << "Current package fee: " << packageFee << std::endl;

    std::string strPackageFee;
    std::cout << "< Hint: any positive integer from 0 to 100000000 >" << std::endl;
    std::cout << "Set package fee: ";
    std::cin >> strPackageFee;
    std::regex pattern("^([1-9]\\d*)|0$");
    if (!std::regex_match(strPackageFee, pattern))
    {
        std::cout << "Input invalid." << std::endl;
        return;
    }
    packageFee = std::stoull(strPackageFee);

    if (packageFee > 100 * DECIMAL_NUM)
    {
        std::cout << "Input invalid." << std::endl;
        return;
    }

    if (set_device_package_fee(packageFee) == 0)
    {
        std::cout << "Package fee is successful to set: " << packageFee << "." << std::endl;
    }
    else
    {
        std::cout << "Package fee is failed to set." << std::endl;
    }
}

void handle_set_fee()
{
    while (true)
    {
        std::cout << std::endl
            << std::endl;
        std::cout << "1.Set signature fee.\n"
            "2.Set package fee.\n"
            "0.Exit.\n";

        std::string strKey;
        std::cout << "Please input your choice: ";
        std::cin >> strKey;
        std::regex pattern("[0-2]");
        if (!std::regex_match(strKey, pattern))
        {
            std::cout << "Invalid input." << std::endl;
            continue;
        }

        int key = std::stoi(strKey);
        switch (key)
        {
        case 0:
            return;
        case 1:
            handle_set_signature_fee();
            break;
        case 2:
            handle_set_package_fee();
            break;
        }
    }
}

void handle_export_private_key()
{
    std::cout << std::endl
        << std::endl;

    //1 私钥、2 注记符、3 二维码
    std::string fileName("account_private_key.txt");
    ofstream file;
    file.open(fileName);

    file << "Please use Courier New font to view" << std::endl
        << std::endl;
    for (auto& item : g_AccountInfo.AccountList)
    {
        file << "Base58 addr: " << item.first << std::endl;
        std::cout << "Base58 addr: " << item.first << std::endl;

        char out_data[1024] = { 0 };
        int data_len = sizeof(out_data);
        mnemonic_from_data((const uint8_t*)item.second.sPriStr.c_str(), item.second.sPriStr.size(), out_data, data_len);
        file << "Mnemonic: " << out_data << std::endl;
        std::cout << "Mnemonic: " << out_data << std::endl;

        std::string strPriHex = Str2Hex(item.second.sPriStr);
        file << "Private key: " << strPriHex << std::endl;
        std::cout << "Private key: " << strPriHex << std::endl;

        file << "QRCode:";
        std::cout << "QRCode:";

        QRCode qrcode;
        uint8_t qrcodeData[qrcode_getBufferSize(5)];
        qrcode_initText(&qrcode, qrcodeData, 5, ECC_MEDIUM, strPriHex.c_str());

        file << std::endl
            << std::endl;
        std::cout << std::endl
            << std::endl;

        for (uint8_t y = 0; y < qrcode.size; y++)
        {
            file << "        ";
            std::cout << "        ";
            for (uint8_t x = 0; x < qrcode.size; x++)
            {
                file << (qrcode_getModule(&qrcode, x, y) ? "\u2588\u2588" : "  ");
                std::cout << (qrcode_getModule(&qrcode, x, y) ? "\u2588\u2588" : "  ");
            }

            file << std::endl;
            std::cout << std::endl;
        }

        file << std::endl
            << std::endl
            << std::endl
            << std::endl
            << std::endl
            << std::endl;
        std::cout << std::endl
            << std::endl
            << std::endl
            << std::endl
            << std::endl
            << std::endl;
    }

    ca_console redColor(kConsoleColor_Red, kConsoleColor_Black, true);
    std::cout << redColor.color() << "You can also view above in file：" << fileName << " of current directory." << redColor.reset() << std::endl;
    return;
}

void handle_print_pending_transaction_in_cache()
{
    std::cout << std::endl << std::endl;
    MagicSingleton<TxVinCache>::GetInstance()->Print();    //现在   
}


/**
A类地址：10.0.0.0 - 10.255255.255 
B类地址：172.16.0.0 - 172.31.255.255
C类地址：192.168.0.0 -192.168255.255 
 */
bool isPublicIp(const string& ip)
{
    static const char* ip192 = "192.168.";
    static const char* ip10 = "10.";
    static const char* ip172 = "172.";
    static const char* ipself = "127.0.0.1";
    if (strncmp(ip.c_str(), ipself, strlen(ipself)) == 0)
    {
        return false;
    }
    if (strncmp(ip.c_str(), ip192, strlen(ip192)) == 0)
    {
        return false;
    }
    else if (strncmp(ip.c_str(), ip10, strlen(ip10)) == 0)
    {
        return false;
    }
    else if (strncmp(ip.c_str(), ip172, strlen(ip172)) == 0)
    {
        int first = ip.find(".");
        int second = ip.find(".", first + 1);
        if (first >= 0 && second >= 0)
        {
            string strIp2 = ip.substr(first + 1, second - first - 1);
            int nIp2 = atoi(strIp2.c_str());
            if (nIp2 >= 16 && nIp2 <= 31)
            {
                return false;
            }
        }
    }

    return true;
}

// Declare: update the public node to config file, create: 20201109   LiuMingLiang
int UpdatePublicNodeToConfigFile()
{
    std::vector<Node> nodes = net_get_all_public_node();
    std::vector<Node> publicNodes;

    // Add Self if it is public
    if (Singleton<Config>::get_instance()->GetIsPublicNode())
    {
        std::string localIp = Singleton<Config>::get_instance()->GetLocalIP();
        if (isPublicIp(localIp))
        {
            Node selfNode = net_get_self_node();
            cout << "Add self node: ^^^^^VVVVVV " << selfNode.base58address << ",Fee " << selfNode.sign_fee << endl;
            nodes.push_back(selfNode);
        }
        else
        {
            cout << "Self node is not public ip ^^^^^VVVVVV " << localIp << endl;
        }
    }

    // Verify pledge and fees
    for (auto node : nodes)
    {
        if (node.public_ip == node.listen_ip && isPublicIp(IpPort::ipsz(node.public_ip)))
        {
            if (node.public_port != SERVERMAINPORT)
            {
                std::cout << "if (node.public_port != SERVERMAINPORT)" << __LINE__ << std::endl;
                continue;
            }
            uint64_t amount = 0;
            if (SearchPledge(node.base58address, amount) != 0)
            {
                cout << "No pledge " << IpPort::ipsz(node.public_ip) << endl;
                continue;
            }
            if (amount < g_TxNeedPledgeAmt)
            {
                cout << "Less mount of pledge " << IpPort::ipsz(node.public_ip) << endl;
                // 质押金额不足
                continue;
            }

            if (node.sign_fee > 0)
            {
                auto iter = publicNodes.begin();
                for (; iter != publicNodes.end(); ++iter)
                {
                    if (iter->public_ip == node.public_ip && iter->listen_ip == node.listen_ip)
                    {
                        break;
                    }
                }
                if (iter == publicNodes.end())
                {
                    publicNodes.push_back(node);
                    std::sort(publicNodes.begin(), publicNodes.end(), Compare(false));
                    publicNodes.erase(unique(publicNodes.begin(), publicNodes.end()), publicNodes.end());
                    cout << "New public node ^^^^^VVVVV " << IpPort::ipsz(node.public_ip) << endl;
                }
                else
                {
                    cout << "Duplicate node ^^^^^^VVVVV " << IpPort::ipsz(node.public_ip) << endl;
                }
            }
        }
        else
        {
            cout << "Not public ip ^^^^^VVVVV " << IpPort::ipsz(node.public_ip) << "," << IpPort::ipsz(node.listen_ip) << endl;
        }
    }

    if (publicNodes.empty())
    {
        cout << "Update node is empty ^^^^^^VVVVVV" << endl;
        return 0;
    }

    if (publicNodes.size() < 5)
    {
        cout << "Update node is less than 5 ^^^^^VVVVV" << endl;
        cout << "Update node is less than 5 Node: " << publicNodes.size() << endl;
        return 1;
    }

    Singleton<Config>::get_instance()->ClearNode();
    Singleton<Config>::get_instance()->ClearServer();
    // Add node to config, and write to config file
    for (auto& node : publicNodes)
    {
        string ip(IpPort::ipsz(node.public_ip));

        u16 port = node.public_port;
        string name(ip);
        string enable("1");

        node_info info /*{name, ip, port, enable}*/;
        info.ip = ip;
        info.port = port;
        info.name = name;
        info.enable = enable;
        Singleton<Config>::get_instance()->AddNewNode(info);
        Singleton<Config>::get_instance()->AddNewServer(ip, port);
    }
    Singleton<Config>::get_instance()->WriteServerToFile();
    cout << "Node: " << publicNodes.size() << " update to config!" << endl;

    return 0;
}

int get_chain_height(unsigned int& chainHeight)
{
    DBReader db_reader;
    uint64_t top = 0;
    if (DBStatus::DB_SUCCESS != db_reader.GetBlockTop(top))
    {
        return -2;
    }
    chainHeight = top;
    return 0;
}

void net_register_chain_height_callback()
{
    net_callback::register_chain_height_callback(get_chain_height);
}

/**
 * @description: 注册回调函数
 * @param {*} 无
 * @return {*} 无
 */
void RegisterCallback()
{
    // 区块同步相关
    net_register_callback<SyncGetnodeInfoReq>(HandleSyncGetnodeInfoReq);
    net_register_callback<SyncGetnodeInfoAck>(HandleSyncGetnodeInfoAck);
    net_register_callback<SyncBlockInfoReq>(HandleSyncBlockInfoReq);
    net_register_callback<SyncBlockInfoAck>(HandleSyncBlockInfoAck);

    net_register_callback<FastSyncGetHashReq>(HandleFastSyncGetHashReq);
    net_register_callback<FastSyncGetHashAck>(HandleFastSyncGetHashAck);
    net_register_callback<FastSyncGetBlockReq>(HandleFastSyncGetBlockReq);
    net_register_callback<FastSyncGetBlockAck>(HandleFastSyncGetBlockAck);

    net_register_callback<SyncGetSumHashReq>(HandleSyncGetSumHashReq);
    net_register_callback<SyncGetSumHashAck>(HandleSyncGetSumHashAck);
    net_register_callback<SyncGetHeightHashReq>(HandleSyncGetHeightHashReq);
    net_register_callback<SyncGetHeightHashAck>(HandleSyncGetHeightHashAck);
    net_register_callback<SyncGetBlockReq>(HandleSyncGetBlockReq);
    net_register_callback<SyncGetBlockAck>(HandleSyncGetBlockAck);

    net_register_callback<SyncLoseBlockReq>(HandleSyncLoseBlockReq);
    net_register_callback<SyncLoseBlockAck>(HandleSyncLoseBlockAck);
    net_register_callback<SyncGetPledgeNodeReq>(HandleSyncGetPledgeNodeReq);
    net_register_callback<SyncGetPledgeNodeAck>(HandleSyncGetPledgeNodeAck);
    net_register_callback<SyncVerifyPledgeNodeReq>(HandleSyncVerifyPledgeNodeReq);
    net_register_callback<SyncVerifyPledgeNodeAck>(HandleSyncVerifyPledgeNodeAck);

    // PC端相关
    net_register_callback<TxMsg>(HandleTx);                                         // PC端交易流转
    net_register_callback<TxPendingBroadcastMsg>(HandleTxPendingBroadcastMsg);      // 交易挂起广播
    net_register_callback<TxDoubleSpendMsg>(HandleTxDoubleSpendMsg);                // 交易双花广播
    net_register_callback<BuildBlockBroadcastMsg>(HandleBuildBlockBroadcastMsg);    // 建块广播

    // 手机端相关
    net_register_callback<CreateTxMsgReq>(HandleCreateTxInfoReq);                   // 手机端主网账户一对一交易（老版本）第一步
    net_register_callback<TxMsgReq>(HandlePreTxRaw);                                // 手机端主网账户一对一交易（老版本）第二步
    net_register_callback<CreateMultiTxMsgReq>(HandleCreateMultiTxReq);             // 手机端主网账户发起多重交易第一步
    net_register_callback<MultiTxMsgReq>(HandleMultiTxReq);                         // 手机端主网账户发起多重交易第二步
    net_register_callback<CreatePledgeTxMsgReq>(HandleCreatePledgeTxMsgReq);        // 手机端主网账户发起质押第一步
    net_register_callback<PledgeTxMsgReq>(HandlePledgeTxMsgReq);                    // 手机端主网账户发起质押第二步
    net_register_callback<CreateRedeemTxMsgReq>(HandleCreateRedeemTxMsgReq);        // 手机端主网账户发起解除质押第一步
    net_register_callback<RedeemTxMsgReq>(HandleRedeemTxMsgReq);                    // 手机端主网账户发起解除质押第二步

    net_register_callback<CreateDeviceTxMsgReq>(HandleCreateDeviceTxMsgReq);                // 手机端连接矿机一对一交易
    net_register_callback<CreateDeviceMultiTxMsgReq>(HandleCreateDeviceMultiTxMsgReq);      // 手机端连接矿机账户发起多重交易
    net_register_callback<CreateDevicePledgeTxMsgReq>(HandleCreateDevicePledgeTxMsgReq);    // 手机端连接矿机发起质押交易
    net_register_callback<CreateDeviceRedeemTxMsgReq>(HandleCreateDeviceRedeemTxMsgReq);    // 手机端连接矿机发起解除质押交易

    // 获取节点缓存
    net_register_callback<GetNodeCacheReq>(HandleGetNodeCacheReq);
    net_register_callback<GetNodeCacheAck>(HandleGetNodeCacheAck);
    
    // 设备相关
    net_register_callback<VerifyDevicePasswordReq>(HandleVerifyDevicePassword);
    net_register_callback<GetDevInfoAck>(HandleGetDevInfoAck);
    net_register_callback<GetDevInfoReq>(HandleGetDevInfoReq);
    net_register_callback<GetMacReq>(HandleGetMacReq);                                      // 扫描矿机获取矿机信息接口

    // 确认交易是否成功
    net_register_callback<ConfirmTransactionReq>(HandleConfirmTransactionReq);
    net_register_callback<ConfirmTransactionAck>(HandleConfirmTransactionAck);

    net_register_chain_height_callback();
}

/**
 * @description: 初始化手续费和打包费
 * @param {*} 无
 * @return {*} 无
 */
void InitFee()
{
    DBReadWriter db_writer;

    uint64_t publicNodePackageFee = 0;
    if (DBStatus::DB_SUCCESS != db_writer.GetDevicePackageFee(publicNodePackageFee))
    {
        if (DBStatus::DB_SUCCESS != db_writer.SetDevicePackageFee(publicNodePackageFee))
        {
            ERRORLOG("set device package fee failed ");
            return;
        }
    }
    net_set_self_package_fee(publicNodePackageFee);

    uint64_t signFee = 0;
    if (DBStatus::DB_SUCCESS != db_writer.GetDeviceSignatureFee(signFee))
    {
        if (signFee == 0)
        {
            signFee = 1000;
        }

        if (DBStatus::DB_SUCCESS != db_writer.SetDeviceSignatureFee(signFee))
        {
            ERRORLOG("set device signature fee failed ");
            return;
        }
    }
    net_set_self_fee(signFee);

    if (DBStatus::DB_SUCCESS != db_writer.TransactionCommit())
    {
        ERRORLOG("db commit failed ");
        return;
    }

    // 向网络层注册主账号地址
    net_set_self_base58_address(g_AccountInfo.DefaultKeyBs58Addr);
    ca_console RegisterColor(kConsoleColor_Yellow, kConsoleColor_Black, true);
    std::cout << RegisterColor.color().c_str() << "RegisterCallback bs58Addr : " << g_AccountInfo.DefaultKeyBs58Addr << RegisterColor.reset().c_str() << std::endl;
}

void TestCreateTx(const std::vector<std::string>& addrs, const int& sleepTime, const std::string& signFee)
{
    if (addrs.size() < 2)
    {
        std::cout << "账号数量不足" << std::endl;
        return;
    }
#if 0
    bIsCreateTx = true;
    while (1)
    {
        if (bStopTx)
        {
            break;
        }
        int intPart = rand() % 10;
        double decPart = (double)(rand() % 100) / 100;
        double amount = intPart + decPart;
        std::string amountStr = std::to_string(amount);

        std::cout << std::endl << std::endl << std::endl << "=======================================================================" << std::endl;
        CreateTx("1vkS46QffeM4sDMBBjuJBiVkMQKY7Z8Tu", "18RM7FNtzDi41QEU5rAnrFdVaGBHvhTTH1", amountStr.c_str(), NULL, 6, "0.01");
        std::cout << "=====交易发起方：1vkS46QffeM4sDMBBjuJBiVkMQKY7Z8Tu" << std::endl;
        std::cout << "=====交易接收方：18RM7FNtzDi41QEU5rAnrFdVaGBHvhTTH1" << std::endl;
        std::cout << "=====交易金额  ：" << amountStr << std::endl;
        std::cout << "=======================================================================" << std::endl << std::endl << std::endl << std::endl;

        sleep(sleepTime);
    }
    bIsCreateTx = false;

#endif

#if 1
    bIsCreateTx = true;
    // while(1)
    for (int i = 0; i < addrs.size(); i++)
    {
        if (bStopTx)
        {
            std::cout << "结束交易！" << std::endl;
            break;
        }
        int intPart = rand() % 10;
        double decPart = (double)(rand() % 100) / 100;
        std::string amountStr = std::to_string(intPart + decPart);

        // std::string from = addrs[rand() % addrs.size()];
        // std::string to = addrs[rand() % addrs.size()];

        std::string from, to;
        if (i >= addrs.size() - 1)
        {
            from = addrs[addrs.size() - 1];
            to = addrs[0];
            i = 0;
        }
        else
        {
            from = addrs[i];
            to = addrs[i + 1];
        }
        if(from != "")
        {
            if (!g_AccountInfo.SetKeyByBs58Addr(g_privateKey, g_publicKey, from.c_str())) {
                DEBUGLOG("Illegal account.");
                return;
            }
        }
        else
        {
            g_AccountInfo.SetKeyByBs58Addr(g_privateKey, g_publicKey, NULL);
        }

        std::cout << std::endl
            << std::endl
            << std::endl
            << "=======================================================================" << std::endl;

        std::vector<std::string> fromAddr;
        fromAddr.emplace_back(from);
        std::map<std::string, int64_t> toAddrAmount;
        uint64_t amount = (stod(amountStr) + FIX_DOUBLE_MIN_PRECISION) * DECIMAL_NUM;
        toAddrAmount[to] = amount;
        uint64_t gasFee =  (stod(signFee) + FIX_DOUBLE_MIN_PRECISION )* DECIMAL_NUM ;

        CTransaction outTx;
        std::vector<TxHelper::Utxo> outVin;
        if (TxHelper::CreateTxTransaction(fromAddr, toAddrAmount, g_MinNeedVerifyPreHashCount, gasFee, outTx, outVin) != 0)
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
        txMsg.set_trycountdown(CalcTxTryCountDown(g_MinNeedVerifyPreHashCount));

        auto msg = make_shared<TxMsg>(txMsg);

        std::string txHash;
        int ret = DoHandleTx(msg, txHash);
        DEBUGLOG("Transaction result，ret:{}  txHash：{}", ret, txHash);

        std::cout << "=====交易发起方：" << from << std::endl;
        std::cout << "=====交易接收方：" << to << std::endl;
        std::cout << "=====交易金额  ：" << amountStr << std::endl;
        std::cout << "=======================================================================" << std::endl
            << std::endl
            << std::endl
            << std::endl;

        sleep(sleepTime);
    }
    bIsCreateTx = false;
#endif
}
void ThreadStart()
{
    std::vector<std::string> addrs;
    for (auto iter = g_AccountInfo.AccountList.begin(); iter != g_AccountInfo.AccountList.end(); iter++)
    {
        addrs.push_back(iter->first);
    }
    std::string signFee = "0.01";
    int sleepTime = 8;
    std::thread th(TestCreateTx, addrs, sleepTime, signFee);
    th.detach();
}

int checkNtpTime()
{
    //Ntp 校验
    int64_t getNtpTime = Singleton<TimeUtil>::get_instance()->getNtpTimestamp();
    int64_t getLocTime = Singleton<TimeUtil>::get_instance()->getlocalTimestamp();

    int64_t tmpTime = abs(getNtpTime - getLocTime);

    std::cout << "Local Time: " << Singleton<TimeUtil>::get_instance()->formatTimestamp(getLocTime) << std::endl;
    std::cout << "Ntp Time: " << Singleton<TimeUtil>::get_instance()->formatTimestamp(getNtpTime) << std::endl;

    if (tmpTime <= 1000000)
    {
        DEBUGLOG("ntp timestamp check success");
        return 0;
    }
    else
    {
        DEBUGLOG("ntp timestamp check fail");
        std::cout << "time check fail" << std::endl;
        return -1;
    }
}
