#include "ca_AdvancedMenu.h"

#include "sys/socket.h"
#include "netinet/in.h"
#include "arpa/inet.h"

#include <regex>

#include "proto/ca_test.pb.h"

#include "net/node_cache.h"
#include "net/net_api.h"
#include "net/peer_node.h"
#include "net/socket_buf.h"
#include "include/all.h"
#include "include/logging.h"
#include "include/ScopeGuard.h"
#include "utils/singleton.h"
#include "utils/time_util.h"
#include "utils/qrcode.h"
#include "common/devicepwd.h"
#include "common/setting_wizard.h"
#include "MagicSingleton.h"
#include "ca_test.h"
#include "ca_global.h"
#include "ca_transaction.h"
#include "ca_interface.h"
#include "ca_hexcode.h"
#include "ca_txvincache.h"
#include "ca_txfailurecache.h"
#include "ca_rollback.h"
#include "ca_MultipleApi.h"
#include "ca_txhelper.h"
#include "ca_AwardAlgorithm.h"
#include "ca_bip39.h"
#include "ca_synchronization.h"
#include "ca.h"
#include "common/global_data.h"
#include "ca/sync_block.h"
#include "ca/ca_blockcache.h"
#include "ca/ca_CCalBlockAward.h"
#include "ca/ca_CCalBlockCount.h"
#include "ca_blockcache.h"
#include "ca/ca_algorithm.h"
#include "ca_pledgecache.h"
#include "../common/time_report.h"
#include "ca/ca_CCalAbnormalAwardList.h"
#include <sys/time.h>

#pragma region 一级菜单      
void menu_advanced()                                          
{
    while (true)
    {
        std::cout << std::endl; 
        std::cout << "1.Ca" << std::endl;
        std::cout << "2.Net" << std::endl;
        std::cout << "0.Exit" << std::endl;

        std::string strKey;
        std::cout << "please input your choice:";
        std::cin >> strKey;

        std::regex pattern("[0-2]");
        if(!std::regex_match(strKey, pattern))
        {
            std::cout << "Input invalid." << std::endl;
            return;
        }
        int key = std::stoi(strKey);
        switch (key)
        {
            case 0:
                return;
            case 1:
                menu_ca();
                break;
            case 2:
                menu_net();
                break;
            default:
                std::cout << "Invalid input." << std::endl;
                continue;
        }
        sleep(1);
    }
}
#pragma endregion

#pragma region 二级菜单 
#pragma region ca菜单
void menu_ca()
{
    while(true)
    {
        std::cout << std::endl;
        std::cout << "ca v" << ca_version() << std::endl;            
        std::cout << "1.Generate Public Key and Private Key." << std::endl;
        std::cout << "2.Import private key" << std::endl;
        std::cout << "3.Export private key" << std::endl;
        std::cout << "4.Get user's account balance." << std::endl;
        std::cout << "5.Create tx." << std::endl;
        std::cout << "6.Create coinbase tx." <<std::endl;
        std::cout << "7.View block info." << std::endl;
        std::cout << "8.Rollback block." << std::endl;
        std::cout << "9.Print online time." << std::endl;
        std::cout << "10.Print hash of first 100 blocks in descending order." << std::endl;
        std::cout << "11.Test." << std::endl;
        std::cout << "12.Get node info." << std::endl;
        std::cout << "13.Sync block form other node." << std::endl;
        std::cout << "14.Print pending transaction in cache." << std::endl;
        std::cout << "15.Print failure transaction in cache." << std::endl;
        std::cout << "16.Clear pending transaction in cache." << std::endl;
        std::cout << "17.获取所有公网节点指定高度区间的hash" << std::endl;
        std::cout << "18.test pledge cache." << std::endl;
        std::cout << "0.Exit." << std::endl;

        std::cout<<"AddrList : "<<std::endl;
        g_AccountInfo.print_AddrList();

        std::string strKey;
        std::cout << "please input your choice:";
        std::cin >> strKey;

        std::regex pattern("^[0-9]|([1][0-8])$");
        if(!std::regex_match(strKey, pattern))
        {
            std::cout << "Input invalid." << std::endl;
            return;
        }
        int key = std::stoi(strKey);
        switch (key)
        {
            case 0:
                return;
            case 1:
                gen_key();
                break;
            case 2:
                in_private_key();
                break;
            case 3:
                out_private_key();
                break;
            case 4:
                get_account_balance();
                break;
            case 5:
                handle_transaction();
                break;
            case 6:
                create_coinbase_tx();  
                break;
            case 7:
                menu_blockinfo();
                break;
            case 8:
                rollback();
                break;
            case 9:
                PrintOnLineTime();
                break;
            case 10:
                print_all_block();             
                break;
            case 11:
                menu_test();
                break;
            case 12:
                menu_node();
                break;		
            case 13:
                synchronize();  
                break;
            case 14:
                MagicSingleton<TxVinCache>::GetInstance()->Print(); 
                break;
            case 15:
                MagicSingleton<TxFailureCache>::GetInstance()->Print();
                break;
            case 16:
                MagicSingleton<TxVinCache>::GetInstance()->Clear();
                break;
            case 17:
                CompPublicNodeHash();
                break;
            default:
                std::cout << "Invalid input." << std::endl;
                continue;
        }
    }
}

void gen_key()
{
    std::cout << "请输入要生成的账户数量：";
    int num = 0;
    std::cin >> num;
    if (num <= 0)
    {
        return;
    }

    for (int i = 0; i != num; ++i)
    {
        g_AccountInfo.GenerateKey();
        std::cout << "DefaultKeyAddr: [" << g_AccountInfo.DefaultKeyBs58Addr << "]" << std::endl;
    }
}

void in_private_key()
{
    // interface_Init("./cert");
    char data[1024] = {0};
    cout << "please input private key:";
    cin >> data;
    int ret = interface_ImportPrivateKeyHex(data);

    if(ret)
        cout << "import private key suc." << endl;
    else
        cout << "import private key fai." << endl;            
}

void out_private_key()
{
    char out_data[1024] = {0};                
    int data_len = 1024;

    // interface_Init("./cert");

    int ret = interface_GetPrivateKeyHex(nullptr, out_data, data_len);
    if(ret)
        cout << "get private key success." << endl;
    else
        cout << "get private key error." << endl;

    cout << "private key:" << out_data << endl;

    memset(out_data,0,1024);

    ret = interface_GetMnemonic(nullptr, out_data, data_len);
    if(ret)
        cout << "get mem success." << endl;
    else
        cout << "get mem error." << endl;

    cout << "mem data:" << out_data << endl;            
}

void get_account_balance()
{
    std::string FromAddr;
    std::cout <<"Input User's Address:"  << std::endl;
    std::cin >> FromAddr;

    std::string addr;
    if (FromAddr.size() == 1)
    {
        addr = g_AccountInfo.DefaultKeyBs58Addr;
    }
    else
    {
        addr = FromAddr;
    }
    uint64_t amount = 0;              
    if(GetBalanceByUtxo(addr.c_str(),amount) != 0)
    {
        INFOLOG("Search Amount failed ");
        return ; 
    }
    std::cout << "User Amount[" << addr.c_str() << "] : " << amount << std::endl;
    INFOLOG("User Amount[{}] : {}", addr.c_str(), amount );    
}

void create_coinbase_tx()
{
    GetNodeInfoReq  getNodeInfoReq;
    getNodeInfoReq.set_version(getVersion());
    
    std::string id ;
    cout<<"请输入id账号"<<endl;
    cin >> id;

    net_send_message<GetNodeInfoReq>(id, getNodeInfoReq);
    net_register_callback<GetNodeInfoAck>([] (const std::shared_ptr<GetNodeInfoAck> & ack, const MsgData & msgdata){
        
        for(size_t i = 0; i != (size_t)ack->node_list_size(); ++i)
        {
            const NodeList & item = ack->node_list(i);
            for (int j = 0 ;j < item.node_info_size();j++)
            {
                std::cout << "enable: " << item.node_info(j).enable() << std::endl;
                std::cout << "ip: " << item.node_info(j).ip() << std::endl;   
                std::cout << "name: " << item.node_info(j).name() << std::endl;   
                std::cout << "port: " << item.node_info(j).port() << std::endl;  
                std::cout << "price: " << item.node_info(j).price() << std::endl;     
            }
        }
        return 0;
    });
}

void print_all_block()
{
    std::string str = printBlocks(100);
    std::cout << str << std::endl;
    ofstream file("blockdata.txt", fstream::out);
    str = printBlocks();
    file << str;
    file.close();  
}

void rollback()
{
    std::cout << "1.Rollback block from Height" << std::endl;
    std::cout << "2.Rollback block from Hash" << std::endl;
    std::cout << "0.Quit" << std::endl;

    int iSwitch = 0;
    std::cin >> iSwitch;
    switch(iSwitch)
    {
        case 0:
        {
            break;
        }
        case 1:
        {
            unsigned int height = 0;
            std::cout << "Rollback block height: " ;
            std::cin >> height;
            auto ret = ca_algorithm::RollBackToHeight(height);
            if(0 != ret)
            {
                std::cout << std::endl << "ca_algorithm::RollBackToHeight:" << ret << std::endl;
                break;
            }
            Singleton<PeerNode>::get_instance()->set_self_height();
            break;
        }
        case 2:
        {
            std::string hash;
            std::cout << "Rollback block hash: " ;
            std::cin >> hash;
            auto ret = ca_algorithm::RollBackByHash(hash);
            if(0 != ret)
            {
                std::cout << std::endl << "ca_algorithm::RollBackByHash:" << ret << std::endl;
                break;
            }
            break;
        }
        default:
        {
            std::cout << "Input error !" << std::endl;
            break;
        }
    }
}

void synchronize()
{
    string id;
    cout << "please input id:";
    cin >> id;
    g_synch->verifying_node.id = id;
    int sync_cnt = Singleton<Config>::get_instance()->GetSyncDataCount();
    g_synch->DataSynch(id, sync_cnt);
}

void CompPublicNodeHash()
{
    uint64_t wait_time = 0;
    std::cout << "请输入最长等待时间（秒）" << std::endl;
    std::cin >> wait_time;
    uint64_t start_height = 0, end_height = 0;
    std::cout << "请输入起始高度" << std::endl;
    std::cin >> start_height;
    std::cout << "请输入终止高度" << std::endl;
    std::cin >> end_height;
    std::cout << "start:" << start_height << ", end:" << end_height << std::endl;
    if(start_height >= end_height)
    {
        std::cout << "起始高度大于终止高度" << std::endl;
        return;
    }
    if(end_height - start_height > 10000)
    {
        std::cout << "区间不能大于10000" << std::endl;
        return;
    }
    std::vector<Node> nodeInfos = Singleton<PeerNode>::get_instance()->get_public_node();
    std::string msg_id;
    GLOBALDATAMGRPTR.CreateWait(wait_time, nodeInfos.size(), msg_id);
    std::cout << "send data num:" << nodeInfos.size() << std::endl;
    for(auto &node : nodeInfos)
    {
        SendFastSyncGetHashReq(node.base58address, msg_id, start_height, end_height);
    }
    std::vector<std::string> ret_data;
    GLOBALDATAMGRPTR.WaitData(msg_id, ret_data);
    std::cout << "return data num:" << ret_data.size() << std::endl;
    if(ret_data.empty())
    {
        return;
    }
    //map中需要存储hash、存在数量
    Node node;
    FastSyncGetHashAck ack;
    for (auto &data : ret_data)
    {
        ack.Clear();
        if (!ack.ParseFromString(data))
        {
            continue;
        }
        Singleton<PeerNode>::get_instance()->find_node(ack.self_node_id(), node);
        std::cout << "ip: " << setw(15) << IpPort::ipsz(node.public_ip) << ", node: " << ack.self_node_id()
                  << ", hash: " << ack.hash() << std::endl;
    }
    return;
}

#pragma endregion

#pragma region net菜单
void menu_net()
{    
    while (true)
    {
        std::cout << std::endl;             
        std::cout << "1.Send message To user." << std::endl;
        std::cout << "2.Show my K bucket." << std::endl;
        std::cout << "3.Kick out node." << std::endl;
        std::cout << "4.Test echo." << std::endl;
        std::cout << "5.Broadcast sending Messages." << std::endl;
        std::cout << "6.Print req and ack." << std::endl;
        std::cout << "7.Print buffers." << std::endl;
        std::cout << "8.Big data send to user." << std::endl;
        std::cout << "9.Print peer node." << std::endl;
        std::cout << "10.Show my ID." << std::endl;
        std::cout << "11.Register to public." << std::endl;
        std::cout << "0.Exit" << std::endl;

        std::string strKey;
        std::cout << "please input your choice:";
        std::cin >> strKey;

        std::regex pattern("^[0-9]|([1][0-1])$");
        if(!std::regex_match(strKey, pattern))
        {
            std::cout << "Input invalid." << std::endl;
            return;
        }
        int key = std::stoi(strKey);
        switch (key)
        {
            case 0:
                return;
            case 1:
                send_message_to_user();
                break;
            case 2:
                show_my_k_bucket();
                break;
            case 3:
                kick_out_node();
                break;
            case 4:
                test_echo();
                break;
            case 5:
                net_com::test_broadcast_message();
                break;
            case 6:
                print_req_and_ack();
                break;
            case 7:
                Singleton<BufferCrol>::get_instance()->print_bufferes(); 
                break;
            case 8:
                net_com::test_send_big_data();  
                break;
            case 9:
                print_peer_node();
                break;
            case 10:
                printf("MyID : %s\n", Singleton<PeerNode>::get_instance()->get_base58addr().c_str());
                break;
            case 11:
                net_com::RegisterToPublic();
                break;
            default:
                std::cout << "Invalid input." << std::endl;
                continue;
        }
    }
}    

void send_message_to_user()
{
    if (net_com::input_send_one_message() == 0)      
        DEBUGLOG("send one msg Succ.");
    else
        DEBUGLOG("send one msg Fail.");
}

void show_my_k_bucket()
{
    std::cout << std::endl << "The K bucket is being displayed..." << std::endl;
    auto nodelist = Singleton<PeerNode>::get_instance()->get_nodelist();
    Singleton<PeerNode>::get_instance()->print(nodelist);                
}

void kick_out_node()
{
    std::string id;
    std::cout << "input id:" << std::endl;
    std::cin >> id;
    Singleton<PeerNode>::get_instance()->delete_node(id);
    std::cout << "Kick out node succeed！" << std::endl;
}

void test_echo()
{
    EchoReq echoReq;
    echoReq.set_id(Singleton<PeerNode>::get_instance()->get_self_id());
    net_com::broadcast_message(echoReq, net_com::Compress::kCompress_False, net_com::Encrypt::kEncrypt_False, net_com::Priority::kPriority_Low_0);
}

void print_req_and_ack()
{
    double total = .0f;
    std::cout << "------------------------------------------" << std::endl;
    for (auto & item : global::reqCntMap)
    {
        total += (double)item.second.second;
        std::cout.precision(3);
        std::cout << item.first << ": " << item.second.first << " size: " << (double)item.second.second / 1024 / 1024 << " MB" << std::endl;
    }
    std::cout << "------------------------------------------" << std::endl;
    std::cout << "Total: " << total / 1024 / 1024 <<" MB" << std::endl;                
}

void print_peer_node()
{
    auto list = Singleton<PeerNode>::get_instance()->get_public_node();
    Singleton<PeerNode>::get_instance()->print(list);                
}
#pragma endregion
#pragma endregion

#pragma region 三级菜单
#pragma region blockinfo菜单
void menu_blockinfo()
{
    while(true)
    {
        DBReader reader; 
        uint64_t top = 0;
        reader.GetBlockTop(top); 

        std::cout << std::endl;
        std::cout << "Height: " << top << std::endl;
        std::cout << "1.获取全部交易笔数\n"
                     "2.获取交易块详细信息\n"
                     "3.查看当前区块奖励和总区块块奖励\n"
                     "4.查看所有区块里边签名账号的累计金额\n"
                     "5.获取设备密码\n"
                     "6.设置设备密码\n"
                     "7.获取设备私钥\n"
                     "0.退出\n";

        std::string strKey;
        std::cout << "please input your choice:";
        std::cin >> strKey;

        std::regex pattern("^[0-7]$");
        if(!std::regex_match(strKey, pattern))
        {
            std::cout << "Input invalid." << std::endl;
            return;
        }
        int key = std::stoi(strKey);
        switch (key) 
        {
            case 0:
                return;
            case 1: 
                get_all_tx_number(top, reader);
                break;
            case 2: 
                get_tx_block_info(top);
                break;
            case 3:
                view_block_award(top, reader);
                break;
            case 4:
                view_total_money(top, reader);            
                break;
            case 5:
                get_device_password();
                break;
            case 6:
                set_device_password();
                break;
            case 7:
                get_device_prikey();
                break;
            default:
                std::cout << "Invalid input." << std::endl;
                continue;
        }

        sleep(1);
    }
}

void get_all_tx_number(uint64_t& top, DBReader& reader)
{   
    std::vector<std::string> vBlockHashs;
    size_t b_count = 0;

    for (size_t i = top; i > 0; --i) 
    {
        reader.GetBlockHashsByBlockHeight(i, vBlockHashs);
        b_count += vBlockHashs.size();
        std::cout << "height------- " << i << std::endl;
        std::cout << "count======= " << vBlockHashs.size() << std::endl;
        vBlockHashs.clear();
    }
    std::cout << "b_count>>>>>>> " << b_count << std::endl;
}

void get_tx_block_info(uint64_t& top)
{
    auto amount = to_string(top);
    std::string input_s, input_e;
    uint64_t start, end;

    std::cout << "amount: " << amount << std::endl;
    std::cout << "pleace input start: ";
    std::cin >> input_s;
    if (input_s == "a" || input_s == "pa") 
    {
        input_s = "0";
        input_e = amount;
    } 
    else 
    {
        if (std::stoul(input_s) > std::stoul(amount)) 
        {
            std::cout << "input > amount" << std::endl;
            return;
        }
        std::cout << "pleace input end: ";
        std::cin >> input_e;
        if (std::stoul(input_s) < 0 || std::stoul(input_e) < 0) 
        {
            std::cout << "params < 0!!" << endl;
            return;
        }
        if (std::stoul(input_s) > std::stoul(input_e)) 
        {
            input_s = input_e;
        }
        if (std::stoul(input_e) > std::stoul(amount)) 
        {
            input_e = std::to_string(top);
        }
    }
    start = std::stoul(input_s);
    end = std::stoul(input_e);

    printRocksdb(start, end);
}

void view_block_award(uint64_t& top, DBReader& reader)
{   
    uint64_t maxheight = top ;
    uint64_t totalaward = 0;

    std::vector<std::string> addr;
    for(unsigned int i = maxheight;  i >0; i--)
    { 
        std::vector<std::string> blockhashes;
        reader.GetBlockHashsByBlockHeight(top, blockhashes);
        std::cout << "blockhashes.size()=" << blockhashes.size() << std::endl;
        uint64_t SingleTotal = 0;
        for(auto &blockhash: blockhashes)
        {
            std::string blockstr;
            reader.GetBlockByBlockHash(blockhash, blockstr);
            CBlock Block;
            Block.ParseFromString(blockstr);

            for (int j = 0; j < Block.txs_size(); j++) 
            {
                CTransaction tx = Block.txs(j);
                if(CheckTransactionType(tx) == kTransactionType_Award)
                {
                    for (int k = 0; k < tx.vout_size(); k++)
                    {
                        CTxout txout = tx.vout(k);
                        SingleTotal += txout.value();
                    } 
                }
            }        
        }
        totalaward += SingleTotal;
        std::cout << "第" << top << "个区块奖励金额:" << SingleTotal << std::endl;
        top--;
    }
    std::cout << "所有区块奖励金额:" << totalaward << std::endl;
}

void view_total_money(uint64_t& top, DBReader& reader)
{
    uint64_t maxheight = top;
    std::map<std::string,int64_t> addrvalue;
    for(unsigned int i = maxheight;  i > 0; i--)
    { 
        vector<string> blockhashes;
        reader.GetBlockHashsByBlockHeight(top, blockhashes);
        std::cout << "blockhashes.size()=" << blockhashes.size() << std::endl;
        for(auto &blockhash: blockhashes )
        {
            std::string blockstr;
            reader.GetBlockByBlockHash(blockhash, blockstr);
            CBlock Block;
            Block.ParseFromString(blockstr);
            for (int j = 0; j < Block.txs_size(); j++) 
            {
                CTransaction tx = Block.txs(j);
                if(CheckTransactionType(tx) == kTransactionType_Award)
                {
                    for (int k = 0; k < tx.vout_size(); k++)
                    {
                        CTxout txout = tx.vout(k);
                        if(addrvalue.empty())
                        {
                            addrvalue.insert(std::pair<std::string, int64_t>(txout.scriptpubkey(), txout.value()));
                            std::cout<<"第一次放入map的第"<<i <<"区块的签名额外奖励"<<txout.scriptpubkey()<< "->"<<txout.value()<<endl;
                            
                        }
                        else
                        {
                            std::map<std::string, int64_t>::iterator  iter;  
                            int64_t newvalue = txout.value();
                            iter = addrvalue.find(txout.scriptpubkey());
                            if(iter != addrvalue.end())
                            {
                                int64_t value = iter->second;
                                newvalue += value;
                                addrvalue.erase(iter);
                                addrvalue.insert(pair<string,int64_t>(txout.scriptpubkey(),newvalue));
                                std::cout << "地址相同时第" << i << "区块的签名额外奖励" << txout.scriptpubkey() << "->" << newvalue << std::endl;
                            }
                            else
                            {
                                addrvalue.insert(pair<string,int64_t>(txout.scriptpubkey(),txout.value()));
                                std::cout << "地址不同时第" << i << "区块的签名额外奖励" << txout.scriptpubkey() << "->" << txout.value() << std::endl;
                            }
                        }
                    } 
                    
                }
            }        
        }
        top--;
    }
    std::map<std::string, int64_t>::iterator it;
    int fd = -1;
    if (addrvalue.size() > 10) 
    {
        fd = open("Base_value.txt", O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    }
    CaTestFormatType formatType;
    if (fd == -1) 
    {
        formatType = kCaTestFormatType_Print;
    } 
    else 
    {
        formatType = kCaTestFormatType_File;
    }
    for(it = addrvalue.begin(); it != addrvalue.end(); it++)  
    {         
        blkprint(formatType, fd, "%s,%u\n", it->first.c_str(),it->second); 
    } 
    close(fd); 
}

void get_device_password()
{
    GetDevPasswordReq getDevPasswordReq;
    getDevPasswordReq.set_version(getVersion());
    std::string id;
    cout<<"请输入id"<<endl;
    cin >> id;
    std::string paswd ;
    cout<<"请输入密码账号"<<endl;
    cin >> paswd;
    getDevPasswordReq.set_password(paswd);
    net_send_message<GetDevPasswordReq>(id, getDevPasswordReq);
}

void set_device_password()
{
    SetDevPasswordReq  setDevPasswordReq;
    std::string id;
    cout<<"请输入id"<<endl;
    cin >> id;
    std::string oldpaswd ;
    cout<<"请输入旧密码账号"<<endl;
    cin >> oldpaswd;
    std::string newpaswd ;
    cout<<"请输入新密码账号"<<endl;
    cin >> newpaswd;
    setDevPasswordReq.set_version(getVersion());
    setDevPasswordReq.set_old_pass(oldpaswd);
    setDevPasswordReq.set_new_pass(newpaswd);
    net_send_message<SetDevPasswordReq>(id, setDevPasswordReq);
}

void get_device_prikey()
{
    GetDevPrivateKeyReq  getDevPrivateKeyReq;
    std::string id;
    cout<<"请输入id"<<endl;
    cin >> id;
    std::string paswd ;
    cout<<"请输入旧密码账号"<<endl;
    cin >> paswd;
    std::string bs58addr; 
    cout<<"请输入bs58addr"<<endl;
    cin >> bs58addr;
    getDevPrivateKeyReq.set_version(getVersion());
    getDevPrivateKeyReq.set_bs58addr(bs58addr);
    getDevPrivateKeyReq.set_password(paswd);
    net_send_message<GetDevPrivateKeyReq>(id, getDevPrivateKeyReq);
}
#pragma endregion

#pragma region test菜单
void menu_test()
{
    while(true)
    {
        std::cout << std::endl;            
        std::cout << "1. 产生助记词." << std::endl;
        std::cout << "2. 模拟质押资产." << std::endl;
        std::cout << "3. 获得交易hash列表" << std::endl;
        std::cout << "4. 根据UTXO查询余额" << std::endl;
        std::cout << "5. 设置节点签名费" << std::endl;
        std::cout << "8. 模拟解质押资产" << std::endl;
        std::cout << "9. 查询账号质押资产额度" << std::endl;
        std::cout << "10.多账号交易" << std::endl;
        std::cout << "11.查询交易列表" << std::endl;
        std::cout << "12.查询交易详情" << std::endl;
        std::cout << "13.查询区块列表" << std::endl;
        std::cout << "14.查询区块详情" << std::endl;
        std::cout << "15.查询所有质押地址" << std::endl;
        std::cout << "16.获取5分钟内总块数" << std::endl;
        std::cout << "17.设置节点打包费用" << std::endl;
        std::cout << "18.获得所有公网节点打包费用" << std::endl;
        std::cout << "19.自动乱序交易(简化版)" << std::endl;
        std::cout << "20.获取前1000高度各个账号奖励" << std::endl;
        std::cout << "21.通过交易hash获取块信息" << std::endl;
        std::cout << "22.获取失败交易列表信息" << std::endl;
        std::cout << "23.获取节点高度是否符合高度范围" << std::endl;
        std::cout << "24.获取质押和解质押" << std::endl;
        std::cout << "25.获取二维码" << std::endl;
        std::cout << "26.获取节点缓存" << std::endl;
        std::cout << "27.根据地址获取质押时间、签名次数和奖励总额" << std::endl;
        std::cout << "28.根据高度获取单位时间内区块数量以及区块奖励总数" << std::endl;
        std::cout << "29.获得所有utxo" << std::endl;
        std::cout << "30.查询异常账号" << std::endl;
        std::cout << "31.获取块缓存哈希" << std::endl;
        std::cout << "0. 退出" << std::endl;

        std::string strKey;
        std::cout << "please input your choice:";
        std::cin >> strKey;
        std::regex pattern("^[0-9]|([1][0-9])|([2][0-9])|([3][0-2])$");
        if(!std::regex_match(strKey, pattern))
        {
            std::cout << "Input invalid." << std::endl;
            return;
        }
        int key = std::stoi(strKey);
        switch (key)
        {
            case 0:
                return;
            case 1:
                gen_mnemonic();
                break;
            case 2:
                handle_pledge();
                break;
            case 3:
                get_hashlist();
                break;
            case 4:
                get_balance_by_utxo();
                break;
            case 5 :
                set_signfee();
                break;
            case 6 :
                get_hash_height_mac();
                break;
            case 7:
                imitate_create_tx_struct();
                break;
            case 8:
                handle_redeem_pledge();
                break;
            case 9:
                get_pledge();
                break;
            case 10:
                multi_tx();
                break;
            case 11:
                get_tx_list();
                break;
            case 12:
                get_tx_info();
                break;
            case 13:
                get_block_list();
                break;
            case 14:
                get_block_info();
                break;
            case 15:
                get_all_pledge_addr();
                break;
            case 16:
                get_all_block_num_in_5min();
                break;
            case 17:
                set_packagefee();
                break;
            case 18:
                get_all_pubnode_packagefee();
                break;
            case 19:
                auto_tx();
                break;
            case 20:
                get_former100_block_award();
                break;
            case 21:
                get_blockinfo_by_txhash();
                break;
            case 22:
                get_failure_tx_list_info();
                break;
            case 23:
                get_nodeheight_isvalid();
                break;
            case 24:
                get_pledge_redeem();
                break;
            case 25:
                get_qrcode();
                break;
            case 26:
                get_node_cache();
                break;
            case 27:
                get_sign_and_award();
                break;
            case 28:
                get_block_num_and_block_award_total();
                break;
            case 29:
                get_utxo();
                break;
            case 30:
                get_abnormal_list();
                break;
            case 31:
                get_block_cache_hash();
                break;
            default:
                std::cout << "Invalid input." << std::endl;
                continue;
        }

        sleep(1);
    }

}

void gen_mnemonic()
{
    char out[24*10] = {0};
    char keystore_data[2400] = {0};

    interface_GetMnemonic(nullptr, out, 24*10);
    printf("Mnemonic_data: [%s]\n", out);

    memset(keystore_data, 0x00, sizeof(keystore_data));
    interface_GetKeyStore(nullptr, "123456", keystore_data, 2400);
    printf("keystore_data %s\n", keystore_data);

    memset(keystore_data, 0x00, sizeof(keystore_data));
    interface_GetPrivateKeyHex(nullptr, keystore_data, 2400);
    printf("PrivateKey_data  %s\n", keystore_data);

    memset(keystore_data, 0x00, sizeof(keystore_data));
    interface_GetBase58Address(keystore_data, 2400);
    printf("PrivateKey_58Address  %s\n", keystore_data);
}

void get_hashlist()
{
    std::cout << "查询地址：" ;
    std::string addr;
    std::cin >> addr;

    DBReader reader;

    std::vector<std::string> txHashs;
    reader.GetAllTransactionByAddreess(addr, txHashs);

    auto txHashOut = [addr, txHashs](ostream & stream){
        
        stream << "账户:" << addr << " tx hash list " << std::endl;
        for (auto hash : txHashs)
        {
            stream << hash << std::endl;
        }

        stream << "地址: "<< addr << " tx hash size：" <<  txHashs.size() << std::endl;
    };

    if(txHashs.size() < 10)
    {
        txHashOut(std::cout);
    }
    else
    {
        std::string fileName = "tx_" + addr + ".txt";
        ofstream file(fileName);
        if( !file.is_open() )
        {
            ERRORLOG("Open file failed!");
            return;
        }
        txHashOut(file);
        file.close();
    }           
}

void get_balance_by_utxo()
{
    std::cout << "查询地址：" ;
    std::string addr;
    std::cin >> addr;

    DBReader reader;
    std::vector<std::string> utxoHashs;
    reader.GetUtxoHashsByAddress(addr, utxoHashs);

    auto utxoOutput = [addr, utxoHashs, &reader](ostream & stream){
        
        stream << "账户:" << addr << " utxo list " << std::endl;

        uint64_t total = 0;
        for (auto i : utxoHashs)
        {
            std::string txRaw;
            reader.GetTransactionByHash(i, txRaw);

            CTransaction tx;
            tx.ParseFromString(txRaw);

            uint64_t value = 0;
            for (int j = 0; j < tx.vout_size(); j++)
            {
                CTxout txout = tx.vout(j);
                if (txout.scriptpubkey() != addr)
                {
                    continue;
                }
                value += txout.value();
            }
            stream << i << " : " << value << std::endl;
            total += value;
        }

        stream << "地址: "<< addr << " UTXO 总数：" <<  utxoHashs.size() <<  " UTXO 总值：" << total << std::endl;
    };

    if(utxoHashs.size() < 10)
    {
        utxoOutput(std::cout);
    }
    else
    {
        std::string fileName = "utxo_" + addr + ".txt";
        ofstream file(fileName);
        if( !file.is_open() )
        {
            ERRORLOG("Open file failed!");
            return;
        }
        utxoOutput(file);
        file.close();
    }            
}

void set_signfee()
{
    uint64_t service_fee = 0;  //原来
    cout << "输入矿费: ";
    cin >> service_fee;

    if(service_fee < 1000  ||  service_fee > 100000)
    {
        cout<<" 输入矿费不在合理范围！"<<endl;
        return;
    }

    DBReadWriter writer;
    if (DBStatus::DB_SUCCESS != writer.SetDeviceSignatureFee(service_fee) ||
        DBStatus::DB_SUCCESS != writer.TransactionCommit()) 
    {
        std::cout << "set signature fee failed" << std::endl;
        return;
    }

    net_update_fee_and_broadcast(service_fee);

    cout << "矿费---------------------------------------------------------->>" << service_fee << endl;            
}

void get_hash_height_mac()
{
    std::vector<std::string> test_list;
    test_list.push_back("1. 获得最高块hash和高度");
    test_list.push_back("2. 获得mac");

    for (auto v : test_list) 
    {
        std::cout << v << std::endl;
    }

    std::string node_id;
    std::cout << "输入id: ";
    std::cin >> node_id;

    uint32_t test_num {0};
    std::cout << "输入选项号: ";
    std::cin >> test_num;

    CaTestInfoReq ca_test_req;
    ca_test_req.set_test_num(test_num);

    net_send_message<CaTestInfoReq>(node_id, ca_test_req);
}

void imitate_create_tx_struct()
{
    const std::string addr = "19cjJN6pqEjwtVPzpPmM7VinMtXVKZXEDu";
    CTransaction tx;

    uint64_t time = Singleton<TimeUtil>::get_instance()->getlocalTimestamp();
    tx.set_version(1);
    tx.set_time(time);
    tx.set_owner(addr);
    tx.set_n(0);

    CTxin * txin = tx.add_vin();
    CTxprevout * prevout = txin->mutable_prevout();
    prevout->set_hash( std::string(64, '0') );
    prevout->set_n(-1);

    CScriptSig * scriptSig = txin->mutable_scriptsig();
    scriptSig->set_sign(GENESIS_BASE_TX_SIGN);

    txin->set_sequence(-1);

    CTxout * txout = tx.add_vout();
    txout->set_value(100000000000000);
    txout->set_scriptpubkey(addr);

    CalcTransactionHash(tx);

    CBlock cblock;
    cblock.set_time(time);
    cblock.set_version(1);
    cblock.set_prevhash(std::string(64, '0'));
    cblock.set_height(0);
    CTransaction * tx0 = cblock.add_txs();
    *tx0 = tx;

    COwnerAddr * ownerAddr = cblock.add_addresses();
    ownerAddr->set_txowner("0000000000000000000000000000000000");
    ownerAddr->set_n(0);

    CalcBlockMerkle(cblock);

    std::string serBlockHeader = cblock.SerializeAsString();
    cblock.set_hash(getsha256hash(serBlockHeader));

    std::string blockHeaderStr = cblock.SerializeAsString();
    char * hexstr = new char[blockHeaderStr.size() * 2 + 2]{0};
    encode_hex(hexstr, blockHeaderStr.data(), blockHeaderStr.size());

    std::cout << std::endl << hexstr << std::endl;

    delete [] hexstr;            
}

void get_pledge()
{
    std::cout << "查询账号质押资产额度：" << std::endl;
    { // 查询质押

        std::string addr;
        std::cout << "查询账号：";
        std::cin >> addr;

        std::string txHash;
        std::cout << "输入hash(第一次直接回车)：";
        std::cin.get();
        std::getline(std::cin, txHash);

        size_t count = 0;
        std::cout << "输入count：";
        std::cin >> count;

        std::shared_ptr<GetPledgeListReq> req = std::make_shared<GetPledgeListReq>();
        req->set_version(getVersion());
        req->set_addr(addr);
        //req->set_index(0);
        req->set_txhash(txHash);
        req->set_count(count);

        GetPledgeListAck ack;
        m_api::HandleGetPledgeListReq(req, ack);


        if (ack.code() != 0)
        {
            ERRORLOG("get pledge failed!");
            std::cout << "get pledge failed!" << std::endl;
            return;
        }

        std::cout << "pledge total: " << ack.total() << std::endl;
        std::cout << "last hash: " << ack.lasthash() << std::endl;

        size_t size = (size_t)ack.list_size();
        for (size_t i = 0; i < size; i++)
        {
            std::cout << "----- pledge " << i << " start -----" << std::endl;
            const PledgeItem item = ack.list(i);
            std::cout << "blockhash: " << item.blockhash() << std::endl;
            std::cout << "blockheight: " << item.blockheight() << std::endl;
            std::cout << "utxo: " << item.utxo() << std::endl;
            std::cout << "amount: " << item.amount() << std::endl;
            std::cout << "time: " << PrintTime(item.time()) << std::endl;
            std::cout << "fromaddr: " << item.fromaddr() << std::endl;
            std::cout << "toaddr: " << item.toaddr() << std::endl;
            std::cout << "detail: " << item.detail() << std::endl;

            std::cout << "----- pledge " << i << " end -----" << std::endl << std::endl;
        }

    }            
}

void multi_tx()
{
    int addrCount = 0;
    std::cout << "发起方账号个数:";
    std::cin >> addrCount;

    std::vector<std::string> fromAddr;
    for (int i = 0; i < addrCount; ++i)
    {
        std::string addr;
        std::cout << "发起账号" << i + 1 << ": ";
        std::cin >> addr;
        fromAddr.push_back(addr);
    }

    std::cout << "接收方账号个数:";
    std::cin >> addrCount;

    std::map<std::string, int64_t> toAddr;
    for (int i = 0; i < addrCount; ++i)
    {
        std::string addr;
        double amt = 0; 
        std::cout << "接收账号" << i + 1 << ": ";
        std::cin >> addr;
        std::cout << "金额: ";
        std::cin >> amt;
        toAddr.insert(make_pair(addr, amt * DECIMAL_NUM));
    }

    uint32_t needVerifyPreHashCount = 0;
    std::cout << "签名数:";
    std::cin >> needVerifyPreHashCount;

    double minerFees = 0;
    std::cout << "手续费:";
    std::cin >> minerFees;

    uint64_t gasFee = minerFees * DECIMAL_NUM;
    CTransaction outTx;
    std::vector<TxHelper::Utxo> outVin;
	if (TxHelper::CreateTxTransaction(fromAddr, toAddr, needVerifyPreHashCount, gasFee, outTx, outVin) != 0)
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
	DEBUGLOG("Transaction result, ret:{}  txHash：{}", ret, txHash);
}

void get_tx_list()
{
    { // 交易列表
        std::string addr;
        std::cout << "查询账号：";
        std::cin >> addr;

        //size_t index = 0;
        std::string txHash;
        std::cout << "输入hash(第一次直接回车)：";
        std::cin.get();
        std::getline(std::cin, txHash);

        size_t count = 0;
        std::cout << "输入count：";
        std::cin >> count;

        std::shared_ptr<GetTxInfoListReq> req = std::make_shared<GetTxInfoListReq>();
        req->set_version(getVersion());
        req->set_addr(addr);
        //req->set_index(index);
        req->set_txhash(txHash);
        req->set_count(count);

        GetTxInfoListAck ack;
        m_api::HandleGetTxInfoListReq(req, ack);

        if (ack.code() != 0)
        {
            std::cout << "code: " << ack.code() << std::endl;
            std::cout << "Description: " << ack.description() << std::endl;
            ERRORLOG("get pledge failed!");
            return;
        }

        std::cout << "total:" << ack.total() << std::endl;
        //std::cout << "index:" << ack.index() << std::endl;
        std::cout << "last hash:" << ack.lasthash() << std::endl;

        for (size_t i = 0; i < (size_t)ack.list_size(); i++)
        {
            std::cout << "----- txInfo " << i << " start -----" << std::endl;
            const TxInfoItem item = ack.list(i);

            std::cout << "txhash: " << item.txhash() << std::endl;
            std::cout << "time: " << PrintTime(item.time()) << std::endl;
            std::cout << "amount: " << item.amount() << std::endl;

            TxInfoType type = item.type();
            std::string strType;
            if (type == TxInfoType_Originator)
            {
                strType = "交易发起方";
            }
            else if (type == TxInfoType_Receiver)
            {
                strType = "交易接收方";
            }
            else if (type == TxInfoType_Gas)
            {
                strType = "手续费奖励";
            }
            else if (type == TxInfoType_Award)
            {
                strType = "区块奖励";
            }
            else if (type == TxInfoType_Pledge)
            {
                strType = "质押";
            }
            else if (type == TxInfoType_Redeem)
            {
                strType = "解除质押";
            }
            else if (type == TxInfoType_PledgedAndRedeemed)
            {
                strType = "质押但已解除";
            }

            std::cout << "type: " << strType << std::endl;
            std::cout << "----- txInfo " << i << " end -----" << std::endl;
        }

    }
}

void get_tx_info()
{
    { // 查询交易
        std::string txHash;
        std::cout << "查询交易哈希：";
        std::cin >> txHash;

        std::string addr;
        std::cout << "查询地址: " ;
        std::cin >> addr;

        std::shared_ptr<GetTxInfoDetailReq> req = std::make_shared<GetTxInfoDetailReq>();
        req->set_version(getVersion());
        req->set_txhash(txHash);
        req->set_addr(addr);
        
        GetTxInfoDetailAck ack;
        m_api::HandleGetTxInfoDetailReq(req, ack);

        std::cout << "----- txInfo start -----" << std::endl;
        std::cout << "    ----- tx -----" << std::endl;
        std::cout << "    code: " << ack.code() << std::endl;
        std::cout << "    blockhash: " << ack.blockhash() << std::endl;
        std::cout << "    blockheight: " << ack.blockheight() << std::endl;
        std::cout << "    txhash: " << ack.txhash() << std::endl;
        std::cout << "    time: " << PrintTime(ack.time()) << std::endl;

        for (auto from : ack.fromaddr())
        {
            std::cout << "    fromaddr: " << from << std::endl;    
        }
        
        for (auto to : ack.toaddr())
        {
            std::cout << "    to: " << to.toaddr() << " amt: " << to.amt() << std::endl;
        }
        
        std::cout << "    gas: " << ack.gas() << std::endl;
        std::cout << "    amount: " << ack.amount() << std::endl;
        std::cout << "    award: " << ack.award() << std::endl;
        
        std::cout << "    ----- award -----" << std::endl;
        std::cout << "awardGas: " << ack.awardgas() << std::endl;
        std::cout << "awardAmount: " << ack.awardamount() << std::endl;
        
        
        std::cout << "----- txInfo end -----" << std::endl;
    }            
}

void get_block_list()
{
    size_t index = 0;
    std::cout << "查询index：";
    std::cin >> index;

    size_t count = 0;
    std::cout << "查询count：" ;
    std::cin >> count;

    GetBlockInfoListReq req;
    req.set_version(getVersion());
    req.set_index(index);
    req.set_count(count);

    std::cout << " -- 附近节点id列表 -- " << std::endl;
    std::vector<Node> idInfos = Singleton<NodeCache>::get_instance()->get_nodelist();
    std::vector<std::string> ids;
    for (const auto & idInfo : idInfos)
    {
        ids.push_back(idInfo.base58address);
    }

    for (auto & id : ids)
    {
        std::cout << id << std::endl;
    }

    std::string id;
    std::cout << "查询ID：";
    std::cin >> id;

    net_register_callback<GetBlockInfoListAck>([] (const std::shared_ptr<GetBlockInfoListAck> & ack, const MsgData & msgdata){
        
        std::cout << std::endl;
        std::cout << "top: " << ack->top() << std::endl;
        std::cout << "txcount: " << ack->txcount() << std::endl;
        std::cout << std::endl;

        for(size_t i = 0; i != (size_t)ack->list_size(); ++i)
        {
            std::cout << " ---- block list " << i << " start ---- " << std::endl;
            const BlockInfoItem & item = ack->list(i);
            std::cout << "blockhash: " << item.blockhash() << std::endl;
            std::cout << "blockheight: " << item.blockheight() << std::endl;
            std::cout << "time: " << PrintTime(item.time()) << std::endl;
            std::cout << "txHash: " << item.txhash() << std::endl;

            for (auto from : item.fromaddr())
            {
                std::cout << "fromaddr: " << from << std::endl;    
            }
            
            for (auto to : item.toaddr())
            {
                std::cout << "    to: " << to << std::endl;
            }
            
            std::cout << "amount: " << item.amount() << std::endl;


            std::cout << " ---- block list " << i << " end ---- " << std::endl;
        }

        std::cout << "查询结束" << std::endl;
        return 0;
    });

    net_send_message<GetBlockInfoListReq>(id, req, net_com::Priority::kPriority_Middle_1);            
}

void get_block_info()
{
    std::string hash;
    std::cout << "查询 block hash：" ;
    std::cin >> hash;

    GetBlockInfoDetailReq req;
    req.set_version(getVersion());
    req.set_blockhash(hash);

    std::cout << " -- 附近节点id列表 -- " << std::endl;
    std::vector<Node> idInfos = Singleton<NodeCache>::get_instance()->get_nodelist();
    std::vector<std::string> ids;
    for (const auto & idInfo : idInfos)
    {
        ids.push_back(idInfo.base58address);
    }
    for (auto & id : ids)
    {
        std::cout << id << std::endl;
    }

    std::string id;
    std::cout << "查询ID：";
    std::cin >> id;

    net_register_callback<GetBlockInfoDetailAck>([] (const std::shared_ptr<GetBlockInfoDetailAck> & ack, const MsgData &msgdata){
        
        std::cout << std::endl;

        std::cout << " ---- block info start " << " ---- " << std::endl;

        std::cout << "blockhash: " << ack->blockhash() << std::endl;
        std::cout << "blockheight: " << ack->blockheight() << std::endl;
        std::cout << "merkleRoot: " << ack->merkleroot() << std::endl;
        std::cout << "prevHash: " << ack->prevhash() << std::endl;
        std::cout << "time: " << PrintTime(ack->time()) << std::endl;
        std::cout << "tatalAmount: " << ack->tatalamount() << std::endl;

        for (auto & s : ack->signer())
        {
            std::cout << "signer: " << s << std::endl;
        }

        for (auto & i : ack->blockinfooutaddr())
        {
            std::cout << "addr: " << i.addr() << " amount: " << i.amount() << std::endl;
        }

        std::cout << " ---- block info end " << " ---- " << std::endl;

        std::cout << "查询结束" << std::endl;
        return 0;
    });

    net_send_message<GetBlockInfoDetailReq>(id, req);            
}

void get_all_pledge_addr()
{
    uint32_t nPledgeType;
    std::cout << "Please enter pledge type: (0: node, 1: public, 2: all) " << std::endl;
    std::cin >> nPledgeType;

    if (nPledgeType > 2)
    {
        std::cout << "input error" << std::endl;
        return;
    }

    PledgeType arrPledgeType[] = {PledgeType::kPledgeType_Node, PledgeType::kPledgeType_Public, PledgeType::kPledgeType_All};
    PledgeType pledgeType = PledgeType::kPledgeType_Unknown;
    pledgeType = arrPledgeType[nPledgeType];

    DBReader reader;
    std::vector<std::string> addressVec;
    reader.GetPledgeAddress(addressVec);

    auto allPledgeOutput = [addressVec](ostream & stream, PledgeType pledgeType){
        stream << std::endl << "---- 已质押地址 start ----" << std::endl;
        for (auto & addr : addressVec)
        {
            uint64_t pledgeamount = 0;
            // SearchPledge(addr, pledgeamount);
            SearchPledge(addr, pledgeamount, pledgeType);
            stream << addr << " : " << pledgeamount << std::endl;
        }
        stream << "---- 已质押地址数量:" << addressVec.size() << " ----" << std::endl << std::endl;
        stream << "---- 已质押地址 end  ----" << std::endl << std::endl;            
    };

    if (addressVec.size() <= 10)
    {   
        allPledgeOutput(std::cout, pledgeType);
    }
    else
    {
        std::string fileName = "all_pledge.txt";

        std::cout << "输出到文件" << fileName << std::endl;

        std::ofstream fileStream;
        fileStream.open(fileName);
        if(!fileStream)
        {
            std::cout << "Open file failed!" << std::endl;
            return;
        }

        allPledgeOutput(fileStream, pledgeType);

        fileStream.close();
    }
}

void get_all_block_num_in_5min()
{
    std::cout << "获取5分钟内总块数" << std::endl;
    a_award::AwardAlgorithm awardAlgorithm;

    uint64_t top = 0;

    DBReader db_reader;
    if(DBStatus::DB_SUCCESS != db_reader.GetBlockTop(top))
    {
        ERRORLOG("(GetBlockNumInUnitTime) GetBlockTop failed !");
        return;
    }

    uint64_t blockSum = 0;
    awardAlgorithm.GetBlockNumInUnitTime(top, blockSum);
    std::cout << "总块数：" << blockSum << std::endl;            
}

void set_packagefee()
{
    uint64_t packageFee = 0;                        
    get_device_package_fee(packageFee);

    std::cout << "当前打包费用: " << packageFee << std::endl;
    std::cout << "设置打包费用(必须为正整数): ";
    std::string strPackageFee;
    std::cin >> strPackageFee;
    int newPackageFee = 0;
    try
    {
        newPackageFee = std::stoi(strPackageFee);
    }
    catch (...)
    {
        newPackageFee = -1;
    }
    if (newPackageFee < 0 || newPackageFee > 100 * DECIMAL_NUM)
    {
        std::cout << "请输入正确范围内的打包费(0-100000000)" << std::endl;
        return;
    }

    int result = set_device_package_fee(newPackageFee);
    if (result == 0)
    {
        std::cout << "设置打包费成功 " << newPackageFee << std::endl;
    }
    else
    {
        std::cout << "设置打包费失败" << std::endl;
    }        
}

void get_all_pubnode_packagefee()
{
    std::cout << " -- 附近节点id列表 -- " << std::endl;
    std::vector<Node> idInfos = Singleton<NodeCache>::get_instance()->get_nodelist();
    std::vector<std::string> ids;
    for (const auto & i : idInfos)
    {
        ids.push_back(i.base58address);
    }
    for (auto & id : ids)
    {
        std::cout << id << std::endl;
    }

    std::string id;
    std::cout << "查询ID：";
    std::cin >> id;


    net_register_callback<GetNodeInfoAck>([] (const std::shared_ptr<GetNodeInfoAck> & ack, const MsgData &msgdata){

        std::cout << "显示公网节点" << std::endl;
        for (auto & node : ack->node_list())
        {
            std::cout << " -- local " << node.local() << " -- " << std::endl;
            for (auto & info : node.node_info())
            {
                std::cout << "ip:" << info.ip() << std::endl;
                std::cout << "name:" << info.name() << std::endl;
                std::cout << "port:" << info.port() << std::endl;
                std::cout << "price:" << info.price() << std::endl;
            }

            std::cout << std::endl;
        }
        return 0;
    });

    GetNodeInfoReq req;
    req.set_version(getVersion());
    net_send_message<GetNodeInfoReq>(id, req);            
}

void auto_tx()
{
    if (bIsCreateTx)
    {
        int i = 0;
        std::cout << "1. 结束交易" << std::endl;
        std::cout << "0. 继续交易" << std::endl;
        std::cout << ">>>" << std::endl;
        std::cin >> i;
        if (i == 1)
        {
            bStopTx = true;
        }
        else if (i == 0)
        {
            return;
        }
        else
        {
            std::cout << "Error!" << std::endl;
            return;
        }
    }
    else
    {
        bStopTx = false;
        std::vector<std::string> addrs;
        g_AccountInfo.print_AddrList();
        for(auto iter = g_AccountInfo.AccountList.begin(); iter != g_AccountInfo.AccountList.end(); iter++)
        {
            addrs.push_back(iter->first);
            }
        std::string signFee("0.02");
        int sleepTime = 0;
        std::cout << "间隔时间(秒)：";
        std::cin >> sleepTime;
        std::thread th(TestCreateTx, addrs, sleepTime, signFee);
        th.detach();
        return;
    }
}

void get_former100_block_award()
{
    int blockNum = 1000;
    clock_t start = clock();

    DBReader reader;
    uint64_t top = 0;
    if ( DBStatus::DB_SUCCESS != reader.GetBlockTop(top) )
    {
        std::cout << "GetBlockTop failed! " << std::endl;
        return;
    }

    if (0 == top)
    {
        return;
    }
    uint64_t highest_height = top;
    uint64_t lowest_height = top > 1000 ? top - 1000 : 1;
    std::vector<std::string> block_hashes;
    DBReader db_reader;
    if (DBStatus::DB_SUCCESS != db_reader.GetBlockHashesByBlockHeight(lowest_height, highest_height, block_hashes))
    {
        std::cout << "error" << std::endl;
        return;
    }
    std::vector<std::string> blocks;
    if (DBStatus::DB_SUCCESS != db_reader.GetBlocksByBlockHash(block_hashes, blocks))
    {
        std::cout << "error" << std::endl;
        return;
    }
    std::unordered_map<std::string, std::pair<uint64_t, uint64_t>> addr_sign_cnt_and_award_amount;

    CBlock block;
    for (auto &block_raw : blocks)
    {
        if (!block.ParseFromString(block_raw))
        {
            std::cout << "error" << std::endl;
            return;
        }
        for (auto &tx : block.txs())
        {
            if (CheckTransactionType(tx) != kTransactionType_Award)
            {
                continue;
            }
            for (auto &vout : tx.vout())
            {
                if (vout.value() <= 0)
                {
                    continue;
                }
                auto it = addr_sign_cnt_and_award_amount.find(vout.scriptpubkey());
                if (addr_sign_cnt_and_award_amount.end() == it)
                {
                    addr_sign_cnt_and_award_amount.insert(std::make_pair(vout.scriptpubkey(), std::make_pair(0, 0)));
                }
                auto &value = addr_sign_cnt_and_award_amount.at(vout.scriptpubkey());
                ++value.first;
                value.second += vout.value();
            }
        }
    }

    uint64_t quarter_num = addr_sign_cnt_and_award_amount.size() * 0.25;
    uint64_t three_quarter_num = addr_sign_cnt_and_award_amount.size() * 0.75;
    if (quarter_num == three_quarter_num)
    {
        return;
    }

    std::vector<uint64_t> sign_cnt;     // 存放签名次数
    std::vector<uint64_t> award_amount; // 存放所有奖励值
    for (auto &item : addr_sign_cnt_and_award_amount)
    {
        sign_cnt.push_back(item.second.first);
        award_amount.push_back(item.second.second);
        std::cout << "账号：" << item.first << "   次数：" << item.second.first << "   奖励：" << item.second.second << std::endl;
    }
    std::sort(sign_cnt.begin(), sign_cnt.end());
    std::sort(award_amount.begin(), award_amount.end());

    uint64_t sign_cnt_quarter_num_value = sign_cnt.at(quarter_num);
    uint64_t sign_cnt_three_quarter_num_value = sign_cnt.at(three_quarter_num);
    uint64_t sign_cnt_upper_limit_value = sign_cnt_three_quarter_num_value +
                                          ((sign_cnt_three_quarter_num_value - sign_cnt_quarter_num_value) * 1.5);

    uint64_t award_amount_quarter_num_value = award_amount.at(quarter_num);
    uint64_t award_amount_three_quarter_num_value = award_amount.at(three_quarter_num);
    uint64_t award_amount_upper_limit_value = award_amount_three_quarter_num_value +
                                              ((award_amount_three_quarter_num_value - award_amount_quarter_num_value) * 1.5);

    uint32_t award_abnormal_count = 0;
    uint32_t sign_abnormal_count = 0;
    uint32_t abnormal_count = 0;
    for (auto &item : addr_sign_cnt_and_award_amount)
    {
        if (item.second.first > sign_cnt_upper_limit_value || item.second.second > award_amount_upper_limit_value)
        {
            ++abnormal_count;
        }
        if (item.second.first > sign_cnt_upper_limit_value)
        {
            ++award_abnormal_count;
            std::cout << "********** 签名次数异常账号：" << item.first << "   总次数 = " << item.second.first << std::endl;
        }
        if (item.second.second > award_amount_upper_limit_value)
        {
            ++sign_abnormal_count;
            std::cout << "********** 奖励总值异常账号：" << item.first << "   总奖励 = " << item.second.second << std::endl;
        }
    }
    std::cout << "总耗时：" << ((double)clock() - start) / CLOCKS_PER_SEC << "秒" <<std::endl;
    std::cout << "********** 奖励总值---1/4 = " << award_amount_quarter_num_value << std::endl;
    std::cout << "********** 奖励总值---3/4 = " << award_amount_three_quarter_num_value << std::endl;
    std::cout << "********** 奖励总值---差值 = " << award_amount_three_quarter_num_value - award_amount_quarter_num_value << std::endl;
    std::cout << "********** 奖励总值---上限 = " << award_amount_upper_limit_value << std::endl;
    std::cout << "********** 奖励总值---总帐号个数 = " << addr_sign_cnt_and_award_amount.size() << std::endl;
    std::cout << "********** 奖励总值---异常帐号个数 = " << award_abnormal_count << std::endl;
    std::cout << "********** 奖励总值---异常帐号占比 = " << ( (double)award_abnormal_count / addr_sign_cnt_and_award_amount.size() ) * 100 << "%" << std::endl;

    std::cout << "********** 签名次数---1/4 = " << sign_cnt_quarter_num_value << std::endl;
    std::cout << "********** 签名次数---3/4 = " << sign_cnt_three_quarter_num_value << std::endl;
    std::cout << "********** 签名次数---差值 = " << sign_cnt_three_quarter_num_value - sign_cnt_quarter_num_value << std::endl;
    std::cout << "********** 签名次数---上限 = " << sign_cnt_upper_limit_value << std::endl;
    std::cout << "********** 签名次数---总帐号个数 = " << addr_sign_cnt_and_award_amount.size() << std::endl;
    std::cout << "********** 签名次数---异常帐号个数 = " << sign_abnormal_count << std::endl;
    std::cout << "********** 签名次数---异常帐号占比 = " << ( (double)sign_abnormal_count / addr_sign_cnt_and_award_amount.size() ) * 100 << "%" << std::endl;

    std::cout << std::endl << "********** 总异常账号个数 = " << abnormal_count << std::endl;
    std::cout << "********** 签名次数占比 = " << (double)sign_abnormal_count / abnormal_count * 100 << "%" << std::endl;
    std::cout << "********** 奖励总值占比 = " << (double)award_abnormal_count / abnormal_count * 100 << "%" << std::endl;
    std::cout << "不包含第一个签名节点" << std::endl;
}

void get_blockinfo_by_txhash()
{
    DBReader reader;

    std::cout << "Tx Hash : ";
    std::string txHash;
    std::cin >> txHash;

    std::string blockHash;
    reader.GetBlockHashByTransactionHash(txHash, blockHash);

    if (blockHash.empty())
    {
        std::cout << "Error : GetBlockHashByTransactionHash failed !" << std::endl;
        return;
    }

    std::string blockStr;
    reader.GetBlockByBlockHash(blockHash, blockStr);
    CBlock block;
    block.ParseFromString(blockStr);

    std::cout << "Block Hash : " << blockHash << std::endl;
    std::cout << "Block height : " << block.height() << std::endl;            
}

void get_failure_tx_list_info()
{
    std::cout << "查询失败交易列表：" << std::endl;

    std::string addr;
    std::cout << "查询账号：";
    std::cin >> addr;

    std::string txHash;
    std::cout << "输入hash(第一次直接回车)：";
    std::cin.get();
    std::getline(std::cin, txHash);

    size_t count = 0;
    std::cout << "输入count：";
    std::cin >> count;

    std::shared_ptr<GetTxFailureListReq> req = std::make_shared<GetTxFailureListReq>();
    req->set_version(getVersion());
    req->set_addr(addr);
    req->set_txhash(txHash);
    req->set_count(count);

    GetTxFailureListAck ack;
    HandleGetTxFailureListReq(req, ack);
    if (ack.code() != 0)
    {
        ERRORLOG("get transaction failure failed!");
        std::cout << "get transaction failure failed!!" << std::endl;
        std::cout << "code: " << ack.code() << std::endl;
        std::cout << "description: " << ack.description() << std::endl;
        return;
    }

    std::cout << "failure total: " << ack.total() << std::endl;
    std::cout << "last hash: " << ack.lasthash() << std::endl;
    size_t size = (size_t)ack.list_size();
    for (size_t i = 0; i < size; i++)
    {
        std::cout << "----- failure list " << i << " start -----" << std::endl;
        const TxFailureItem& item = ack.list(i);
        std::cout << "hash: " << item.txhash() << std::endl;
        if (item.fromaddr_size() > 0)
        {
            std::cout << "from addr: " << item.fromaddr(0) << std::endl;
        }
        if (item.toaddr_size() > 0)
        {
            std::cout << "to addr: " << item.toaddr(0) << std::endl;
        }
        std::cout << "amout: " << item.amount() << std::endl;
    }
}

void get_nodeheight_isvalid()
{
    std::cout << " -- 附近节点id列表 -- " << std::endl;
    std::vector<Node> idInfos = Singleton<NodeCache>::get_instance()->get_nodelist();
    std::vector<std::string> ids;
    for (const auto & idInfo : idInfos)
    {
        ids.push_back(idInfo.base58address);
    }

    for (auto & id : ids)
    {
        std::cout << id << std::endl;
    }

    std::string id;
    std::cout << "查询ID：";
    std::cin >> id;

    CheckNodeHeightReq req;
    req.set_version(getVersion());


    net_register_callback<CheckNodeHeightAck>([] (const std::shared_ptr<CheckNodeHeightAck> & ack, const MsgData & msgdata){
        
        std::cout << std::endl;
        std::cout << "code: " << ack->code() << std::endl;
        std::cout << "total: " << ack->total() << std::endl;
        std::cout << "percent: " << ack->percent() << std::endl;
        std::cout << "height: " << ack->height() << std::endl;
        std::cout << std::endl;
        
        std::cout << "查询结束" << std::endl;
        return 0;
    });

    net_send_message<CheckNodeHeightReq>(id, req, net_com::Priority::kPriority_Middle_1);     
}

void get_pledge_redeem()
{
    std::string addr;
    std::cout << "查询账号：";
    std::cin >> addr;

    std::shared_ptr<GetPledgeRedeemReq> req = std::make_shared<GetPledgeRedeemReq>();
    req->set_version(getVersion());
    req->set_address(addr);

    GetPledgeRedeemAck ack;
    m_api::HandleGetPledgeRedeemReq(req, ack);
    std::cout << ack.code() << std::endl;
    std::cout << ack.description() << std::endl;
}

void get_qrcode()
{
    std::string fileName("account.txt");               

    ca_console redColor(kConsoleColor_Red, kConsoleColor_Black, true); 
    std::cout << redColor.color() << "输出文件为：" << fileName << " 请用Courier New字体查看" << redColor.reset() <<  std::endl;

    ofstream file;
    file.open(fileName);

    file << "请用Courier New字体查看" << std::endl << std::endl;

    for (auto & item : g_AccountInfo.AccountList)
    {
        file << "Base58 addr: " << item.first << std::endl;

        char out_data[1024] = {0};
        int data_len = sizeof(out_data);
        mnemonic_from_data((const uint8_t*)item.second.sPriStr.c_str(), item.second.sPriStr.size(), out_data, data_len);    
        file << "Mnemonic: " << out_data << std::endl;

        std::string strPriHex = Str2Hex(item.second.sPriStr);
        file << "Private key: " << strPriHex << std::endl;
        
        file << "QRCode:";

        QRCode qrcode;
        uint8_t qrcodeData[qrcode_getBufferSize(5)];
        qrcode_initText(&qrcode, qrcodeData, 5, ECC_MEDIUM, strPriHex.c_str());

        file << std::endl << std::endl;

        for (uint8_t y = 0; y < qrcode.size; y++) 
        {
            file << "        ";
            for (uint8_t x = 0; x < qrcode.size; x++) 
            {
                file << (qrcode_getModule(&qrcode, x, y) ? "\u2588\u2588" : "  ");
            }

            file << std::endl;
        }

        file << std::endl << std::endl << std::endl << std::endl << std::endl << std::endl;
    }
}

void get_node_cache()
{
    auto nodelist = Singleton<NodeCache>::get_instance()->get_nodelist();
    if (nodelist.size() == 0)
    {
        std::cout << "Node cache is empty!" << std::endl;
        return ;
    }
    for (size_t i = 0; i != nodelist.size(); ++i)
    {   
        auto & node = nodelist[i];
        std::cout << i << ". Base58(" << node.base58address << ")  height(" << node.height << ")  is_public(" << node.is_public_node << ")" << std::endl;
    }
    std::cout << "Node cache size:" << nodelist.size() << std::endl;
}

void get_sign_and_award()
{
    std::string addr;
    std::cout << "查询账号：";
    std::cin >> addr;
    int64_t pledge_time = ca_algorithm::GetPledgeTimeByAddr(addr, PledgeType::kPledgeType_Node);
    if(pledge_time <= 0)
    {
        std::cout << "获取质押时间返回：" << pledge_time << std::endl;
        return;
    }
    uint64_t tx_time = Singleton<TimeUtil>::get_instance()->getlocalTimestamp();
    uint64_t block_height = 0;
    uint64_t addr_award_total = 0;
    uint32_t sign_cnt = 0;
    if(DBStatus::DB_SUCCESS != DBReader().GetBlockTop(block_height))
    {
        std::cout << "获取节点高度失败"<< std::endl;
        return;
    }
    auto ret = ca_algorithm::GetAwardAmountAndSignCntByAddr(addr, block_height, pledge_time, tx_time, addr_award_total, sign_cnt);
    if(ret < 0)
    {
        std::cout << "账号奖励总数返回：" << ret << std::endl;
        return;
    }
    time_t s = (time_t)(pledge_time / 1000000);
    struct tm * gm_date;
    gm_date = localtime(&s);
    char ps[1024] = {0};
    sprintf(ps, "%d-%d-%d %d:%d:%d", gm_date->tm_year + 1900, gm_date->tm_mon + 1, gm_date->tm_mday, gm_date->tm_hour, gm_date->tm_min, gm_date->tm_sec);

    double pledge_time_in_days = (tx_time - pledge_time) / 1000.0 / 1000 / 60 / 60 / 24;
    if (pledge_time_in_days < 1.0)
    {
        pledge_time_in_days = 1.0;
    }
    std::cout << addr
              << std::endl << "\tpledge time:" << ps << "(" << pledge_time << ")"
              << std::endl << "\tpledge_time_in_days:" << pledge_time_in_days
              << std::endl << "\taward:" << addr_award_total
              << std::endl << "\tsign cnt:" << sign_cnt << std::endl;
}
void get_block_num_and_block_award_total()
{
    uint64_t block_height = 0;
    std::cout << "高度：";
    std::cin >> block_height;
    if( 0 == block_height)
    {
        if(DBStatus::DB_SUCCESS != DBReader().GetBlockTop(block_height))
        {
            std::cout << "获取节点高度失败"<< std::endl;
            return;
        }
    }
    int block_num = ca_algorithm::GetBlockNumInUnitTime(block_height);
    if (block_num < 0)
    {
        std::cout << "获取单位时间内的区块返回：" << block_num << std::endl;
        return;
    }
    int64_t total_award = ca_algorithm::GetBlockTotalAward(block_num);
    if(total_award < 0)
    {
        std::cout << "获取区块总奖励返回：" << total_award << std::endl;
        return;
    }
    std::cout << std::endl << "block num:" << block_num
              << std::endl << "total award:" << total_award << std::endl;
}

void get_utxo()
{
    std::string addr;
    std::cout << "查询账号: ";
    std::cin >> addr;

    std::string id;
    std::cout << "请输入id: ";
    std::cin >> id;

    GetUtxoReq req;
    req.set_version(getVersion());
    req.set_address(addr);

    net_register_callback<GetUtxoAck>([] (const std::shared_ptr<GetUtxoAck> & ack, const MsgData & msgdata){
        
        if (ack->code() != 0)
        {
            std::cout << "code: " << ack->code() << std::endl;
            return 0;
        }

        for(size_t i = 0; i != (size_t)ack->utxos_size(); ++i)
        {
            auto & item = ack->utxos(i);
            std::cout << "hash: " << item.hash() << " value: " << item.value() << " n:" << item.n() << std::endl;
        }

        std::cout << "addr: " << ack->address() << "utxo count: " << ack->utxos_size() << std::endl;
        return 0;
    });
    net_send_message<GetUtxoReq>(id, req);
}
void get_block_cache_txcount_and_award_summary()
{
    CCalBlockAward award;
    CCalBlockCount count;

    std::map<std::string , std::pair<uint64_t,uint64_t>> award_map;

    MagicSingleton<CBlockCache>::GetInstance()->Calculate(award);
    MagicSingleton<CBlockCache>::GetInstance()->Calculate(count);
    
    award.GetAwardMap(award_map);
    std::cout << "transaction count: " << count.GetCount() << std::endl;

    for(const auto & info : award_map)
    {
        std::cout << "identity: " << info.first << " sign count: " <<  info.second.first << " total award: " << info.second.second << std::endl;
    }
}

void get_block_cache_hash()
{
    std::string strStart, strEnd;
    std::regex pattern("^[1-9]\\d*$");

    std::cout << "pleace input cache start : ";
    std::cin >> strStart;
    if (!std::regex_match(strStart, pattern))
    {
        std::cout << "invalid input!(start height should '> 0')" << std::endl;
        return;
    }
    uint64_t start = std::stoull(strStart) - 1;

    std::cout << "pleace input cache end: ";
    std::cin >> strEnd;
    uint64_t end = 0; 
    if(!std::regex_match(strEnd, pattern) || (end = std::stoull(strEnd) - 1) < start) 
    {
        std::cout << "invalid input!(end height should be larger than start height)!" << std::endl;
        return;
    }

    std::map<uint64_t, std::set<CBlock, CBlockCompare>> block_cache;
    MagicSingleton<CBlockCache>::GetInstance()->GetCache(block_cache);
    
    auto Blockcacheout = [block_cache,start,end](ostream & stream){
        auto iter = block_cache.begin();
        int block_size = block_cache.size();
        for(int i = 0; i <= end && i < block_size ; ++i )
        {
            if(i < start)
            {
                iter++;
                continue;
            }
            stream <<"height: " << iter->first << std::endl;
            stream <<"block hash: " << std::endl;
            for(const auto& block : iter->second)
            {
                stream << block.hash() << std::endl;
            }
            iter++;
        }
    };

    if(end - start < 10) //已判断 end  大于  start
    {
        Blockcacheout(std::cout);
    }
    else
    {
        int64_t getLocTime = Singleton<TimeUtil>::get_instance()->getlocalTimestamp();
        std::string fileName = "blockcache_" + strStart + "_" + strEnd + "_" + Singleton<TimeUtil>::get_instance()->formatTimestamp(getLocTime) + ".txt";
        ofstream file(fileName);
        if( !file.is_open() )
        {
            ERRORLOG("Open file failed!");
            return;
        }
        Blockcacheout(file);
        file.close();
    }           
}
void get_abnormal_list()
{
    CCalAbnormalAwardList abnormal;

    std::vector<std::string> abnormal_list;

    MagicSingleton<CBlockCache>::GetInstance()->Calculate(abnormal);
    abnormal.GetAbnormalList(abnormal_list);
    auto Abnormalout = [abnormal_list](ostream & stream){
        
        stream << "异常账号总数： " << abnormal_list.size() << std::endl;
        stream << "异常账号地址： " << std::endl;
        for (const auto& addr : abnormal_list)
        {
          stream << addr << std::endl;
        }
    };
    if(abnormal_list.size() < 10)
    {
        Abnormalout(std::cout);
    }
    else
    {
        std::string fileName = "abnormal_list.txt";
        ofstream file(fileName);
        if( !file.is_open() )
        {
            ERRORLOG("Open file failed!");
            return;
        }
        Abnormalout(file);
        file.close();
    }           
}
#pragma endregion

#pragma region node菜单
void menu_node()
{
    while(true)
    {
        std::cout << std::endl;            
        std::cout << "1.get a node info" << std::endl;
        std::cout << "2.get all node info" << std::endl;
        std::cout << "0.exit node" << std::endl;

        std::string strKey;
        std::cout << "please input your choice:";
        std::cin >> strKey;

        std::regex pattern("^[0-2]$");
        if(!std::regex_match(strKey, pattern))
        {
            std::cout << "Input invalid." << std::endl;
            return;
        }
        int key = std::stoi(strKey);
        switch (key)
        {
            case 0:
                return;
            case 1:
                get_a_node_info();
                break;
            case 2:
                get_all_node_info();
                break;
            default:
                std::cout << "Invalid input." << std::endl;
                continue;
        }
    }
}

void get_a_node_info()
{
    string id;
    cout << "Please input id of the node:" ;
    cin >> id;
    
    SendDevInfoReq(id);
    std::cout << "Data are being obtained..." << std::endl;
    int i = 0;
    while(i < 10)
    {
        if(g_nodeinfo.size() <= 0)
        {
            sleep(1);
            i++;
        }
        else
        {
            break;
        }
    }

    if(i == 10)
    {
        std::cout << "get node info failed !" << std::endl;
        return;
    }

    g_NodeInfoLock.lock_shared();
    for(auto i : g_nodeinfo)
    {
        std::cout << "node id   :" << i.id() << std::endl;
        std::cout << "height    :" << i.height() <<std::endl;
        std::cout << "hash      :" << i.hash() <<std::endl;
        std::cout << "base58addr:" << i.base58addr() <<std::endl;
    }
    g_NodeInfoLock.unlock_shared();
    
    g_nodeinfo.clear();                    
}                  

void get_all_node_info()
{
    std::cout << "Data are being obtained..." << std::endl;

    std::vector<Node> nodeInfos = Singleton<NodeCache>::get_instance()->get_nodelist();
    std::vector<std::string> nodes;
    for (const auto & i : nodeInfos )
    {
        nodes.push_back(i.base58address);
    }
    if(nodes.size() <= 0)
    {
        std::cout << "未找到节点！" << std::endl;
        return;
    }

    std::ofstream fileStream;
    fileStream.open("node.txt");
    if(!fileStream)
    {
        std::cout << "Open file failed!" << std::endl;
        return;
    }

    for(auto i : nodes)
    {
        // 获取所有节点的块信息
        SendDevInfoReq(i);
    }

    for(int j = 0; j < 10; j++)
    {
        if( g_nodeinfo.size() < nodes.size() )
        {
            std::cout << "已获取到 ：" << g_nodeinfo.size() << std::endl;
            sleep(1);
        }
        else
        {
            break;
        }
    }

    std::cout << "节点总数 ：" << nodes.size() << std::endl;
    std::cout << "已获取到 ：" << g_nodeinfo.size() << std::endl;
    std::cout << "开始写文件！" << std::endl;

    g_NodeInfoLock.lock_shared();
    for(auto k : g_nodeinfo)
    {
        std::cout << "id ：" << k.id() << std::endl;
        std::cout << "height ：" << k.height() << std::endl;
        std::cout << "hash：" << k.hash() << std::endl;
        std::cout << "addr：" << k.base58addr() << std::endl << std::endl;

        // 所有节点写入node.txt 文件
        fileStream << "ID：";
        fileStream << k.id();
        fileStream << ",高度：";
        fileStream << k.height();
        fileStream << ",hash：";
        fileStream << k.hash();
        fileStream << ",base58addr：";
        fileStream << k.base58addr();
        fileStream << "\n";
    }
    g_NodeInfoLock.unlock_shared();

    std::cout << "结束！" << std::endl;
    g_nodeinfo.clear();
    fileStream.close();
}

#pragma endregion
#pragma endregion
