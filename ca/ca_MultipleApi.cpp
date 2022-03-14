#include "ca_MultipleApi.h"
#include "ca_txhelper.h"
#include "ca.h"
#include  "ca_timefuc.h"
#include  "ca_pwdattackchecker.h"
#include "ca_txvincache.h"
#include "ca_txfailurecache.h"
#include <regex>
#include "../net/node_cache.h"
#include "../include/logging.h"
#include "ca_trans_utils.h"
#include "db/db_api.h"

using std::string;

//namespace start
namespace m_api {

int is_version = -1;

void GetNodeServiceFee(const std::shared_ptr<GetNodeServiceFeeReq> &node_fee_req, GetNodeServiceFeeAck &node_fee_ack)
{
    int db_status = 0; //数据库返回状态 0为成功
    uint64_t max_fee = 0, min_fee = 0, service_fee = 0;
    db_status = DBReader().GetDeviceSignatureFee(service_fee);
    if (DBStatus::DB_SUCCESS != db_status)
    {
        ERRORLOG("rdb_ptr->GetDeviceSignatureFee ret:{}", db_status);
        return;
    }
    auto node_fee = net_get_node_ids_and_fees();
    if (node_fee.size() > 1) 
    {
        std::vector<uint64_t> node_fee_list;
        if (service_fee != 0) 
        {
            node_fee_list.push_back(service_fee);
        }
        for (auto v : node_fee)
        {
            if (v.second != 0 &&  v.second >= 1000  &&  v.second <= 100000) 
            {
                node_fee_list.push_back(v.second);
            }
        }

        sort(node_fee_list.begin(), node_fee_list.end());
        uint32_t fee_list_count = static_cast<uint32_t>(node_fee_list.size());


        //0.31 - 1
        uint32_t list_begin = fee_list_count * 0.51;
        uint32_t list_end = fee_list_count;

        list_begin = list_begin == 0 ? 1 : list_begin;
        list_end = list_end == 0 ? 1 : list_end;
        if (node_fee_list.size() == 1) 
        {
            min_fee = 0;
        } 
        else 
        {
            min_fee = node_fee_list[list_begin - 1];
        }
        max_fee = node_fee_list[list_end - 1];
        service_fee = (max_fee + min_fee)/2;

        if(min_fee == 0)
        {
            min_fee = 1000;
        }
        if( min_fee < 1|| max_fee > 100000)
        {
            node_fee_ack.set_code(-1);
            node_fee_ack.set_description("单节点签名费错误");

            return ;
        }
    }

    DEBUGLOG("node_fee.size() = {}", node_fee.size());

    auto servicep_fee = node_fee_ack.add_service_fee_info();
    servicep_fee->set_max_fee(to_string(((double)max_fee)/DECIMAL_NUM));
    servicep_fee->set_min_fee(to_string(((double)min_fee)/DECIMAL_NUM));

    servicep_fee->set_service_fee(to_string(((double)service_fee)/DECIMAL_NUM));
    node_fee_ack.set_version(getVersion());
    node_fee_ack.set_code(0);
    node_fee_ack.set_description("获取成功");

    return;
}

int SetServiceFee(const std::shared_ptr<SetServiceFeeReq> &fee_req, SetServiceFeeAck &fee_ack) 
{
    using namespace std;
    fee_ack.set_version(getVersion());
    std::string dev_pass = fee_req->password();
    std::string hashOriPass = generateDeviceHashPassword(dev_pass);
    std::string targetPassword = Singleton<DevicePwd>::get_instance()->GetDevPassword();
    auto pCPwdAttackChecker = MagicSingleton<CPwdAttackChecker>::GetInstance(); 
    uint32_t minutescount ;
    bool retval = pCPwdAttackChecker->IsOk(minutescount);
    if(retval == false)
    {
        ERRORLOG("Return time for more than four input errors.");
        std::string minutescountStr = std::to_string(minutescount);
        fee_ack.set_code(-31);
        fee_ack.set_description(minutescountStr);
        ERRORLOG("Because there are 3 consecutive errors, you can enter {} seconds later.", minutescount);
        return -1;
    }
    if(hashOriPass.compare(targetPassword))
    {
       DEBUGLOG("Begin to count, beacause password was entered incorrectly.");
       if(pCPwdAttackChecker->Wrong())
       {
            DEBUGLOG("The first two attempts to enter the password are incorrect.");
            fee_ack.set_code(-6);
            fee_ack.set_description("密码输入错误");
            return -2;
       } 
       else 
       {
            DEBUGLOG("Enter the wrong password for the third time and return to the countdown time.");
            fee_ack.set_code(-30);
            fee_ack.set_description("第三次输入密码错误");
            return -3;
       }
    }
    else 
    {
        DEBUGLOG("SetServiceFee password reset to 0");
        pCPwdAttackChecker->Right();
        fee_ack.set_code(0);
        fee_ack.set_description("密码输入正确");
    }

    if (hashOriPass != targetPassword) 
    {
        fee_ack.set_code(-6);
        fee_ack.set_description("密码错误");
        return -4;
    }

    DEBUGLOG("fee_req->service_fee() = {}", fee_req->service_fee());

    std::regex partten("-[0-9]+(.[0-9]+)?|[0-9]+(.[0-9]+)?");
    if (!std::regex_match(fee_req->service_fee(), partten))
    {
        fee_ack.set_code(-8);
        fee_ack.set_description("Fee输入不正确");
        ERRORLOG("invalid fee param");
        return -5;
    }

    double nodesignfee = stod(fee_req->service_fee());
    if(nodesignfee < 0.001 || nodesignfee > 0.1)
    {
        fee_ack.set_code(-7);
        fee_ack.set_description("滑动条数值显示错误");
        ERRORLOG("return num show nodesignfee = {}", nodesignfee);
        return -6;
    }
    uint64_t service_fee = (uint64_t)(stod(fee_req->service_fee()) * DECIMAL_NUM);
    DEBUGLOG("service_fee ", service_fee);
    //设置手续费 SET
    DBReadWriter db_readwriter;
    auto status = db_readwriter.SetDeviceSignatureFee(service_fee);
    if (DBStatus::DB_SUCCESS != status)
    {
        fee_ack.set_code(status);
        fee_ack.set_description("燃料费设置失败"); 
        return -7;
    }
    db_readwriter.TransactionCommit();
    fee_ack.set_code(status);
    fee_ack.set_description("燃料费设置成功");
    return 0;
}


void GetServiceInfo(const std::shared_ptr<GetServiceInfoReq>& msg,GetServiceInfoAck &response_ack)
{
    DBReader db_reader;
    uint64_t top = 0;
    auto db_status =  db_reader.GetBlockTop(top);
    if (DBStatus::DB_SUCCESS != db_status)
    {
        ERRORLOG("rdb_ptr->GetBlockTop failed !");
        return;
    }
    unsigned int height = top;

    //查询前100块数据
    std::vector<std::string> hash;
    db_status = db_reader.GetBlockHashsByBlockHeight(top, hash);
    if (DBStatus::DB_SUCCESS != db_status)
    {
        ERRORLOG("rdb_ptr->GetBlockHashsByBlockHeight failed !");
        return;
    }
    std::string block_hash = hash[0];

    //查找100范围内所有块

    uint64_t max_fee, min_fee, def_fee, avg_fee, count;
    uint64_t temp_fee {0};

    min_fee = 1000;
    max_fee = 100000;

    {
        CBlockHeader block;
        std::string serialize_block;

        top = top > 100 ? 100 : top;
        for (int32_t i = top; i > 0; --i) 
        {
            db_status = db_reader.GetBlockHeaderByBlockHash(block_hash, serialize_block);
            if (DBStatus::DB_SUCCESS != db_status)
            {
                ERRORLOG("rdb_ptr->GetBlockHeaderByBlockHash failed !");
                return;
            }
            block.ParseFromString(serialize_block);

            //解析块头
            string serialize_header;
            db_status =  db_reader.GetBlockByBlockHash(block.hash(), serialize_header);
            if (DBStatus::DB_SUCCESS != db_status)
            {
                ERRORLOG("rdb_ptr->GetBlockByBlockHash failed !");
                return;
            }
            CBlock cblock;
            cblock.ParseFromString(serialize_header);

            for (int32_t i_i = 0; i_i < cblock.txs_size(); i_i++) 
            {
                if (i_i == 1)
                 {
                    CTransaction tx = cblock.txs(i_i);
                    for (int32_t j = 0; j < tx.vout_size(); j++)
                    {
                        CTxout txout = tx.vout(j);
                        if (txout.value() > 0) 
                        {
                            temp_fee = txout.value();
                            break;
                        }
                    }
                }
            }
            if (i == (int32_t)top) 
            {
                max_fee = temp_fee;
                min_fee = temp_fee;
                count = temp_fee;
            } 
            else 
            {
                max_fee = max_fee > temp_fee ? max_fee : temp_fee;
                min_fee = min_fee < temp_fee ? min_fee : temp_fee;
                count += temp_fee;
            }
            block_hash = block.prevhash();
        }
    }

    uint64_t show_service_fee = 0;
    db_status = db_reader.GetDeviceSignatureFee(show_service_fee);
    if (db_status) 
    {
        ERRORLOG("rdb_ptr->GetDeviceSignatureFee failed !");
        return;
    }

    if (!show_service_fee) 
    {
        //def_fee = (max_fee + min_fee)/2;
        def_fee = 0;
    }
    else
    {
        def_fee = show_service_fee;
    }
    if (top == 100) 
    {
        avg_fee = count/100;
    } 
    else 
    {
        avg_fee = (max_fee + min_fee)/2;
    }

    min_fee = 1000;
    max_fee = 100000;

  
    response_ack.set_version(getVersion());
    response_ack.set_code(0);
    response_ack.set_description("获取设备成功");

    response_ack.set_mac_hash("test_hash"); 
    response_ack.set_device_version(getEbpcVersion()); 
    response_ack.set_height(height);
    response_ack.set_sequence(msg->sequence_number());

    auto service_fee = response_ack.add_service_fee_info();
    service_fee->set_max_fee(to_string(((double)max_fee)/DECIMAL_NUM));
    service_fee->set_min_fee(to_string(((double)min_fee)/DECIMAL_NUM));
    service_fee->set_service_fee(to_string(((double)def_fee)/DECIMAL_NUM));
    service_fee->set_avg_fee(to_string(((double)avg_fee)/DECIMAL_NUM));
    return;
}

int64_t getAvgFee()
{
    DBReader db_reader;

    uint64_t top = 0;
    auto status = db_reader.GetBlockTop(top);
    if (DBStatus::DB_SUCCESS != status)
    {
        ERRORLOG("(GetBlockInfoAck) GetBlockTop failed !");
        return -2;
    }
    //查询前100块数据
    std::vector<std::string> hash;
    status = db_reader.GetBlockHashsByBlockHeight(top, hash);
    if (DBStatus::DB_SUCCESS != status)
    {
        ERRORLOG("(GetBlockInfoAck) GetBlockHashsByBlockHeight failed !");
        return -3;
    }
    std::string block_hash = hash[0];

    //查找100范围内所有块

    uint64_t max_fee, min_fee, def_fee, avg_fee, count;
    uint64_t temp_fee {0};

    min_fee = 1000;
    max_fee = 100000;

    CBlockHeader block;
    std::string serialize_block;

    top = top > 100 ? 100 : top;
    for (int32_t i = top; i > 0; --i) 
    {
        status = db_reader.GetBlockHeaderByBlockHash(block_hash, serialize_block);
        if (DBStatus::DB_SUCCESS != status)
        {
            ERRORLOG("(GetBlockInfoAck) GetBlockHeaderByBlockHash failed !");
            return -4;
        }
        block.ParseFromString(serialize_block);

        //解析块头
        string serialize_header;
        status = db_reader.GetBlockByBlockHash(block.hash(), serialize_header);
        if (DBStatus::DB_SUCCESS != status)
        {
            ERRORLOG("(GetBlockInfoAck) GetBlockByBlockHash failed !");
            return -5;
        }
        CBlock cblock;
        cblock.ParseFromString(serialize_header);

        for (int32_t i_i = 0; i_i < cblock.txs_size(); i_i++) 
        {
            if (i_i == 1) 
            {
                CTransaction tx = cblock.txs(i_i);
                for (int32_t j = 0; j < tx.vout_size(); j++) 
                {
                    CTxout txout = tx.vout(j);
                    if (txout.value() > 0) 
                    {
                        temp_fee = txout.value();
                        break;
                    }
                }
            }
        }

        if (i == (int32_t)top) 
        {
            max_fee = temp_fee;
            min_fee = temp_fee;
            count = temp_fee;
        } 
        else 
        {
            max_fee = max_fee > temp_fee ? max_fee : temp_fee;
            min_fee = min_fee < temp_fee ? min_fee : temp_fee;
            count += temp_fee;
        }

        block_hash = block.prevhash();
    }

    uint64_t show_service_fee = 0;

    status = db_reader.GetDeviceSignatureFee(show_service_fee);
    if (DBStatus::DB_SUCCESS != status)
    {
        ERRORLOG("(GetBlockInfoAck) GetDeviceSignatureFee failed !");
        return -6;
    }
    if (!show_service_fee)
    {
        //def_fee = (max_fee + min_fee)/2;
        def_fee = 0;
    } 
    else 
    {
        def_fee = show_service_fee;
        DEBUGLOG("show_service_fee = {}", show_service_fee);
    }
    DEBUGLOG("max_fee = {}, min_fee = {}, top = {}, count = {}", max_fee, min_fee, top, count);
    if (top == 100)
    {
        avg_fee = count/100;
    } 
    else
    {
        avg_fee = (max_fee + min_fee)/2;
    }
    (void)def_fee;
    return avg_fee;
}


void HandleGetServiceInfoReq(const std::shared_ptr<GetServiceInfoReq>& msg,const MsgData& msgdata) 
{
    GetServiceInfoAck getInfoAck;
    if (is_version) 
    {
        getInfoAck.set_version(getVersion());
        getInfoAck.set_code(is_version);
        getInfoAck.set_description("版本错误");

        net_send_message<GetServiceInfoAck>(msgdata, getInfoAck, net_com::Priority::kPriority_Middle_1);
        return;
    }
   
    GetServiceInfo(msg,getInfoAck);
    net_send_message<GetServiceInfoAck>(msgdata, getInfoAck, net_com::Priority::kPriority_Middle_1);
    return;
}

void GetPackageFee(const std::shared_ptr<GetPackageFeeReq> &package_req, GetPackageFeeAck &package_ack) 
{

    uint64_t package_fee = 0;
    DBReader().GetDevicePackageFee(package_fee);
  
    package_ack.set_version(getVersion());
    package_ack.set_code(0);
    package_ack.set_description("获取成功");

    double d_fee = ((double)package_fee) / DECIMAL_NUM;
    std::string s_fee= to_string(d_fee);
    package_ack.set_package_fee(s_fee);

    return;
}

template <typename T>
void AddBlockInfo(T &block_info_ack, CBlockHeader &block) 
{
    //解析块头
    string serialize_header;
    DBReader().GetBlockByBlockHash(block.hash(), serialize_header);
    CBlock cblock;
    cblock.ParseFromString(serialize_header);

    //开始拼接block_info_list
    auto blocks = block_info_ack.add_block_info_list();

    blocks->set_height(block.height());
    blocks->set_hash_merkle_root(cblock.merkleroot());
    blocks->set_hash_prev_block(cblock.prevhash());
    blocks->set_block_hash(cblock.hash());
    blocks->set_ntime(cblock.time());

    for (int32_t i = 0; i < cblock.txs_size(); i++) 
    {
        CTransaction tx = cblock.txs(i);

        //添加tx_info_list
        auto tx_info = blocks->add_tx_info_list();
        tx_info->set_tx_hash(tx.hash());

        for (int32_t j = 0; j < tx.signprehash_size(); j++) 
        {
            CSignPreHash signPreHash = tx.signprehash(j);
            char buf[2048] = {0};
            size_t buf_len = sizeof(buf);
            GetBase58Addr(buf, &buf_len, 0x00, signPreHash.pub().c_str(), signPreHash.pub().size());
            tx_info->add_transaction_signer(buf);
        }

        //添加vin_info
        for (int32_t j = 0; j < tx.vin_size(); j++) 
        {
            auto vin_list = tx_info->add_vin_list();

            CTxin txin = tx.vin(j);
            CScriptSig scriptSig = txin.scriptsig();
            int scriptSigLen = scriptSig.sign().size();
            char * hexStr = new char[scriptSigLen * 2 + 2]{0};

            std::string sign_str(scriptSig.sign().data(), scriptSig.sign().size());
            if (sign_str == FEE_SIGN_STR || sign_str == EXTRA_AWARD_SIGN_STR) 
            {
                vin_list->set_script_sig(sign_str);
            } 
            else 
            {
                char * hexStr = new char[scriptSigLen * 2 + 2]{0};
                encode_hex(hexStr, scriptSig.sign().data(), scriptSigLen);
                vin_list->set_script_sig(hexStr);
            }
            vin_list->set_pre_vout_hash(txin.prevout().hash());
            vin_list->set_pre_vout_index((uint64_t)txin.prevout().n());

            delete [] hexStr;
        }

        //添加vout_info
        for (int32_t j = 0; j < tx.vout_size(); j++) 
        {
            auto vout_list = tx_info->add_vout_list();

            CTxout txout = tx.vout(j);

            vout_list->set_script_pubkey(txout.scriptpubkey());
            double ld_value = (double)txout.value() / DECIMAL_NUM;
            std::string s_value(to_string(ld_value));

            vout_list->set_amount(s_value);
        }

        tx_info->set_nlock_time(tx.time());
        tx_info->set_stx_owner(TxHelper::GetTxOwnerStr(tx));
        tx_info->set_stx_owner_index(tx.n());
        tx_info->set_version(tx.version());
    } 
}

void GetAmount(const std::shared_ptr<GetAmountReq> &amount_req, GetAmountAck &amount_ack) 
{
    amount_ack.set_version(getVersion());
    
    std::string addr = amount_req->address();
    if(addr.size() == 0)
    {
        amount_ack.set_code(-2);
        amount_ack.set_description("addr is empty");
        return;
    } 

    if (!CheckBase58Addr(addr))
    {
        amount_ack.set_code(-3);
        amount_ack.set_description("not base58 addr");
        return;
    }

    uint64_t balance = 0;
    if(GetBalanceByUtxo(addr,balance) != 0)
    {
        amount_ack.set_code(-4);
        amount_ack.set_description("search balance failed");
        return;
    }

    std::string s_balance = to_string(((double)balance) / DECIMAL_NUM);

    amount_ack.set_code(0);
    amount_ack.set_description("Get Amount Success");
    amount_ack.set_address(addr);
    amount_ack.set_balance(s_balance);

    return;
}

int BlockInfoReq(const std::shared_ptr<GetBlockInfoReq> &block_info_req, GetBlockInfoAck &block_info_ack) 
{
    int32_t height = block_info_req->height();
    int32_t count = block_info_req->count();

    DBReader db_reader;
    std::string bestChainHash;
    auto status = db_reader.GetBestChainHash(bestChainHash);
    DEBUGLOG("(BlockInfoReq) GetBestChainHash db_status:{}", status);
    if (bestChainHash.size() == 0) 
    { 
        //使用默认值
        block_info_ack.set_version(getVersion());
        block_info_ack.set_code(-2);
        block_info_ack.set_description("GetBestChainHash failed ");
        return -2;
    }
    uint64_t top;
    status = db_reader.GetBlockTop(top);
    if (DBStatus::DB_SUCCESS != status)
    {
        DEBUGLOG("(BlockInfoReq) GetBlockTop db_status:{}", status);
        block_info_ack.set_version(getVersion());
        block_info_ack.set_code(-3);
        block_info_ack.set_description("GetBlockTop failed ");
        return -3;
    }

    //如为负 则为最高高度和所有块数
    height = height > static_cast<int32_t>(top) ? top : height;
    height = height < 0 ? top : height;
    count = count > height ? height : count;
    count = count < 0 ? top : count; //FIXME

    //通过高度查块hash
    std::vector<std::string> hash;

    //查找count范围内所有块
    CBlockHeader block;
    std::string serialize_block;

    for (int64_t i = count, j = 0; i >= 0; --i, ++j) 
    {
        status = db_reader.GetBlockHashsByBlockHeight(height - j, hash);
        if (DBStatus::DB_SUCCESS != status)
        {
            DEBUGLOG("(BlockInfoReq) GetBlockHashsByBlockHeight db_status:{}", status);
            block_info_ack.set_version(getVersion());
            block_info_ack.set_code(-4);
            block_info_ack.set_description("GetBlockHashsByBlockHeight failed ");
            return -4;
        }
        DEBUGLOG("count: {}", hash.size());
        for (auto &hs : hash)
        {
            status = db_reader.GetBlockHeaderByBlockHash(hs, serialize_block);
            if (DBStatus::DB_SUCCESS != status)
            {
                DEBUGLOG("(BlockInfoReq) GetBlockHeaderByBlockHash db_status:{}", status);
                block_info_ack.set_version(getVersion());
                block_info_ack.set_code(-5);
                block_info_ack.set_description("GetBlockHeaderByBlockHash failed ");
                return -5;
            }
            block.ParseFromString(serialize_block);
            //add block_infos
            AddBlockInfo(block_info_ack, block);
        }
        hash.clear();
        //hash = block.prevhash();
        if (i == 0) 
        {
            break;
        }
    }
    
    block_info_ack.set_version(getVersion());
    block_info_ack.set_code(0);
    block_info_ack.set_description("success");
    block_info_ack.set_top(top);

    DEBUGLOG("block_info_ack.version():{}, block_info_ack.code():{}, block_info_ack.description():{}, block_info_ack.description():{}",
    block_info_ack.version(),block_info_ack.code(),block_info_ack.description(), block_info_ack.top());
    return 0;
}

void GainDevPasswordReq(const std::shared_ptr<GetDevPasswordReq> &pass_req, GetDevPasswordAck &pass_ack)
{
	std::string password = pass_req->password();
    std::string version = "1";
    int code {0};
    std::string description = "成功";

    Singleton<StringUtil>::get_instance()->Trim(password, true, true);
    std::string originPass = generateDeviceHashPassword(password);
    std::string targetPass = Singleton<DevicePwd>::get_instance()->GetDevPassword();
    DEBUGLOG("password = {}, originPass = {}, targetPass = {}", password, originPass, targetPass);
    auto pCPwdAttackChecker = MagicSingleton<CPwdAttackChecker>::GetInstance(); 
   
    uint32_t minutescount ;
    bool retval = pCPwdAttackChecker->IsOk(minutescount);
    if(retval == false)
    {
        std::string minutescountStr = std::to_string(minutescount);
        pass_ack.set_version(getVersion());
        pass_ack.set_code(-31);
        pass_ack.set_description(minutescountStr);
        ERRORLOG("Because there are 3 consecutive errors, you can enter {} seconds later.", minutescount);
        return;
    }

    if(originPass.compare(targetPass))
    {
        DEBUGLOG("Begin to count, beacause password was entered incorrectly.");
       if(pCPwdAttackChecker->Wrong())
       {
            ERRORLOG("Password Error.");
            pass_ack.set_version(getVersion());
            pass_ack.set_code(-2);
            pass_ack.set_description("密码输入错误");
            return;
       }
       else
       {
           ERRORLOG("Enter the password incorrectly for the third time.");
            pass_ack.set_version(getVersion());
            pass_ack.set_code(-30);
            pass_ack.set_description("第三次输入密码错误");
            return;
       } 
    }
    else 
    {
        DEBUGLOG("GainDevPasswordReq Password right, and reset to 0.");
        pCPwdAttackChecker->Right();
        pass_ack.set_version(getVersion());
        pass_ack.set_code(0);
        pass_ack.set_description("密码输入正确");
    }

    if (originPass != targetPass) 
    {
        pass_ack.set_version(getVersion());
        pass_ack.set_code(-2);
        pass_ack.set_description("密码错误");
        return;
    }

    pass_ack.set_version(getVersion());
    pass_ack.set_code(code);
    pass_ack.set_description(description);
    DEBUGLOG("g_AccountInfo.DefaultKeyBs58Addr = {}", g_AccountInfo.DefaultKeyBs58Addr); 
    pass_ack.set_address(g_AccountInfo.DefaultKeyBs58Addr);

    return;
}

void DevPasswordReq(const std::shared_ptr<SetDevPasswordReq> &pass_req, SetDevPasswordAck &pass_ack) 
{

    std::string old_pass = pass_req->old_pass();
    std::string new_pass = pass_req->new_pass();

    DEBUGLOG("old:{}, new:{}", old_pass, new_pass);

    pass_ack.set_version(getVersion());
    if (old_pass.empty() || new_pass.empty()) 
    {
        pass_ack.set_code(-2);
        pass_ack.set_description("The password cannot be empty");
        return;
    }

    if (!isDevicePasswordValid(old_pass)) 
    {
        pass_ack.set_code(-3);
        pass_ack.set_description("oriPass invalid");
        return;
    }

    if (!isDevicePasswordValid(new_pass)) 
    {
        pass_ack.set_code(-4);
        pass_ack.set_description("newPass invalid");
        return;
    }

    if (old_pass == new_pass) 
    {
        pass_ack.set_code(-5);
        pass_ack.set_description("The new password cannot be the same as the old one");
        return;
    }

    std::string hashOriPass = generateDeviceHashPassword(old_pass);
    std::string targetPassword = Singleton<DevicePwd>::get_instance()->GetDevPassword();
    auto pCPwdAttackChecker = MagicSingleton<CPwdAttackChecker>::GetInstance(); 
   
    uint32_t minutescount ;
    bool retval = pCPwdAttackChecker->IsOk(minutescount);
    if(retval == false)
    {
        std::string minutescountStr = std::to_string(minutescount);
        pass_ack.set_code(-31);
        pass_ack.set_description(minutescountStr);
        ERRORLOG("Because there are 3 consecutive errors, you can enter {} seconds later.", minutescount);
        return;
    }

    if(hashOriPass.compare(targetPassword))
    {
        DEBUGLOG("Begin to count, beacause password was entered incorrectly.");
       if(pCPwdAttackChecker->Wrong())
       {
            ERRORLOG("Password error");
            pass_ack.set_code(-6);
            pass_ack.set_description("密码输入错误");
            return;
       }
       else
       {
           ERRORLOG("Enter the wrong password for the third time and return to the countdown time.");
            pass_ack.set_code(-30);
            pass_ack.set_description("第三次输入密码错误");
            return;
       }
    }
    else 
    {
        DEBUGLOG("The cout reset to 0.");
        pCPwdAttackChecker->Right();
        pass_ack.set_code(0);
        pass_ack.set_description("密码输入正确");
    }


    if (hashOriPass != targetPassword) 
    {
        pass_ack.set_code(-6);
        pass_ack.set_description("Incorrect old password");
        return;
    }

    std::string hashNewPass = generateDeviceHashPassword(new_pass);
    if (!Singleton<DevicePwd>::get_instance()->SetDevPassword(hashNewPass)) 
    {
        pass_ack.set_code(-7);
        pass_ack.set_description("Unknown error");
    } 
    else 
    {
        pass_ack.set_code(0);
        pass_ack.set_description(" success");
    }

    return;
}

void GetTransactionInfo(const std::shared_ptr<GetAddrInfoReq> &addr_req, GetAddrInfoAck &addr_ack) 
{
    std::string addr = addr_req->address();
    uint32_t index = addr_req->index();
    uint32_t count = addr_req->count();

    DBReader db_reader;
    std::string bestChainHash;
    auto status = db_reader.GetBestChainHash(bestChainHash);
    if (DBStatus::DB_SUCCESS != status)
    {
        ERRORLOG("(GetTransactionInfo) GetBestChainHash failed db_status:{}!", status);
        return;
    }

    addr_ack.set_version(getVersion());
    if (!bestChainHash.size()) 
    {
        addr_ack.set_code(-1);
        addr_ack.set_description("Get tx info failed! No Data.");
        addr_ack.set_total(0);
        return;
    }

    std::vector<std::string> vTxHashs;
    status = db_reader.GetAllTransactionByAddreess(addr, vTxHashs);
    if (DBStatus::DB_SUCCESS != status)
    {
        ERRORLOG("(GetTransactionInfo) GetAllTransactionByAddreess failed db_status:{}!", status);
        return;
    }

    std::reverse(vTxHashs.begin(), vTxHashs.end());

    std::vector<std::string> vBlockHashs;
    for (auto &strTxHash : vTxHashs) 
    {
        std::string blockHash;
        status = db_reader.GetBlockHashByTransactionHash(strTxHash, blockHash);
        if (DBStatus::DB_SUCCESS != status)
        {
            ERRORLOG("(GetTransactionInfo) GetBlockHashByTransactionHash failed db_status:{}!", status);
            return;
        }
        vBlockHashs.push_back(blockHash);
    }

    vBlockHashs.erase(unique(vBlockHashs.begin(), vBlockHashs.end()), vBlockHashs.end());

    if (index >= vBlockHashs.size()) 
    {
        addr_ack.set_code(-2);
        addr_ack.set_description("Get tx info failed! Index out of range.");
        addr_ack.set_total(vBlockHashs.size());
        return;
    }

    size_t end = index + count;
    if (end > vBlockHashs.size()) 
    {
        end = vBlockHashs.size();
    }

    for (size_t i = index; i < end; i++) 
    {
        std::string hash = vBlockHashs[i];
        std::string serBlock;
        status = db_reader.GetBlockHeaderByBlockHash(hash, serBlock);
        if (DBStatus::DB_SUCCESS != status)
        {
            ERRORLOG("(GetTransactionInfo) GetBlockHeaderByBlockHash failed db_status:{}!", status);
            return;
        }
        CBlockHeader block;
        block.ParseFromString(serBlock);
        AddBlockInfo(addr_ack, block);
    }

    addr_ack.set_code(0);
    addr_ack.set_description("Get tx info success");
    addr_ack.set_total(vBlockHashs.size());
    return;
}

void GetNodInfo(GetNodeInfoAck& out_ack)
{
    GetNodeInfoAck node_ack;
    node_ack.set_version(getVersion());
    node_ack.set_code(0);
    node_ack.set_description("获取成功");
    //Singleton<Config>::get_instance()->InitFile();
    std::string node_string = Singleton<Config>::get_instance()->GetNodeInfo();
    if (node_string.empty()) 
    {
        node_ack.set_code(-1);
        node_ack.set_description("配置获取失败");
        return;
    }

    auto node_json = nlohmann::json::parse(node_string);

    for (auto it = node_json.begin(); it != node_json.end(); ++it) 
    {
        auto node_list = node_ack.add_node_list();
        auto info_json = it.value();
        for (auto info_it = info_json.begin(); info_it != info_json.end(); ++info_it) 
        {

            if (info_it.key() == "local") 
            {
                node_list->set_local(info_it.value());
            } 
            else 
            {
                auto param_json = info_it.value();
                for (auto param_it = param_json.begin(); param_it != param_json.end(); ++param_it) 
                {
                    auto node_info = node_list->add_node_info();
                    auto data_json = param_it.value();

                    for (auto data_it = data_json.begin(); data_it != data_json.end(); ++data_it) 
                    {

                        if (data_it.key() == "enable") 
                        {
                            node_info->set_enable(data_it.value());
                        } 
                        else if (data_it.key() == "ip") 
                        {
                            node_info->set_ip(data_it.value());
                        } 
                        else if (data_it.key() == "name") 
                        {
                            node_info->set_name(data_it.value());
                        } 
                        else if (data_it.key() == "port") 
                        {
                            node_info->set_port(data_it.value());
                        }

                        node_info->set_price("");
                        node_info->set_base58("");
                    }
                }
            }
        }
    }

    std::vector<Node> nodes = net_get_public_node();
    for (int i = 0; i != node_ack.node_list_size(); ++i)
    {
        NodeList * nodeList = node_ack.mutable_node_list(i);
        for (int j = 0; j != nodeList->node_info_size(); ++j)
        {
            NodeInfos * nodeInfos = nodeList->mutable_node_info(j);
            for (auto & node : nodes)
            {
                if (node.is_public_node && IpPort::ipsz(node.public_ip) == nodeInfos->ip())
                {
                    nodeInfos->set_price(to_string((double)(node.package_fee) / DECIMAL_NUM));
                    nodeInfos->set_base58(node.base58address);
                }
            }
        }
    }
    uint64_t packageFee = 0;
    DBReader().GetDevicePackageFee(packageFee);

    // 自身节点
    if (Singleton<Config>::get_instance()->GetIsPublicNode())
    {
        for (int i = 0; i != node_ack.node_list_size(); ++i)
        {
            NodeList * nodeList = node_ack.mutable_node_list(i);
            for (int j = 0; j != nodeList->node_info_size(); ++j)
            {
                NodeInfos * nodeInfos = nodeList->mutable_node_info(j);
                if (Singleton<Config>::get_instance()->GetLocalIP() == nodeInfos->ip())
                {
                    nodeInfos->set_price(to_string((double)(packageFee) / DECIMAL_NUM));
                    nodeInfos->set_base58(net_get_self_node_id());
                }
            }
        }
    }

    // Filter empty base58  20220105  Liu
    out_ack.set_version(node_ack.version());
    out_ack.set_code(node_ack.code());
    out_ack.set_description(node_ack.description());
    for (int i = 0; i < node_ack.node_list_size(); ++i)
    {
        NodeList newNodeList;
        const NodeList& nodelist = node_ack.node_list(i);
        for (int j = 0; j < nodelist.node_info_size(); ++j)
        {
            const NodeInfos& node = nodelist.node_info(j);
            if (!node.base58().empty())
            {
                NodeInfos* addInfo = newNodeList.add_node_info();
                *addInfo = node;
            }
        }
        if (newNodeList.node_info_size() > 0)
        {
            newNodeList.set_local(nodelist.local());
            NodeList* addNodeList = out_ack.add_node_list();
            *addNodeList = newNodeList;
        }
    }

    return;
}

void GetClientInfo(const std::shared_ptr<GetClientInfoReq> &clnt_req, GetClientInfoAck &clnt_ack) 
{
    using namespace std;

    auto phone_type = (ClientType)(clnt_req->phone_type());
    auto phone_lang = (ClientLanguage)clnt_req->phone_lang();
    std::string phone_version = clnt_req->phone_version();

    std::string sMinVersion = Singleton<Config>::get_instance()->GetClientVersion(phone_type);
    std::string sClientVersion = phone_version;
   
    std::vector<std::string> vMin;
    std::vector<std::string> vCleint;

    StringSplit(vMin, sMinVersion, ".");
    StringSplit(vCleint, sClientVersion, ".");

    bool isUpdate = true;
    int code {0};

    if (vMin.size() != vCleint.size()) 
    {
        code = -1;
        isUpdate = false;
    } 
    else 
    {
        for (size_t i = 0; i < vMin.size(); i++) 
        {
            std::string sMin = vMin[i];
            std::string sClient = vCleint[i];

            int nMin = atoi(sMin.c_str());
            int nClient = atoi(sClient.c_str());

            if (nMin > nClient) 
            {
                isUpdate = true;
                break;
            } 
            else if (nMin < nClient)
            {
                isUpdate = false;
                break;
            }
        }
    }

    clnt_ack.set_min_version(sMinVersion);

    std::string clientInfo = ca_clientInfo_read();

    clnt_ack.set_version(getVersion());
    clnt_ack.set_code(code);
    clnt_ack.set_description("Get Client Version Success");
    clnt_ack.set_is_update("0");

    if (isUpdate) 
    {
        clnt_ack.set_is_update("1");

        std::string sVersion;
        std::string sDesc;
        std::string sDownload;

        int r = ca_getUpdateInfo(clientInfo, phone_type, phone_lang, sVersion, sDesc, sDownload);
        if (!r) 
        {
            clnt_ack.set_code(1);
            clnt_ack.set_description("Get Client Version Success");

            clnt_ack.set_ver(sVersion);
            clnt_ack.set_desc(sDesc);
            clnt_ack.set_dl(sDownload);
        } 
        else 
        {
            clnt_ack.set_code(r);
            clnt_ack.set_description("Get Client Version failed");
        }

    }

    return;
}

void HandleGetDevPrivateKeyReq(const std::shared_ptr<GetDevPrivateKeyReq>& msg,  GetDevPrivateKeyAck& devprikey_ack )
{
    string passwd = msg->password();
    if(passwd.empty())
    {
        devprikey_ack.set_version(getVersion());
        devprikey_ack.set_code(-1);
        devprikey_ack.set_description("The password input error");
        return ;
    }
    string Bs58Addr = msg->bs58addr();
    if(Bs58Addr.empty())
    {
        devprikey_ack.set_version(getVersion());
        devprikey_ack.set_code(-2);
        devprikey_ack.set_description("The Bs58Addr input error");
        return;
    }

    std::string hashOriPass = generateDeviceHashPassword(passwd);
    std::string targetPassword = Singleton<DevicePwd>::get_instance()->GetDevPassword();
    auto pCPwdAttackChecker = MagicSingleton<CPwdAttackChecker>::GetInstance(); 
   
    uint32_t minutescount ;
    bool retval = pCPwdAttackChecker->IsOk(minutescount);
    if(retval == false)
    {
        std::string minutescountStr = std::to_string(minutescount);
        devprikey_ack.set_code(-31);
        devprikey_ack.set_description(minutescountStr);
        ERRORLOG("Because there are 3 consecutive errors, you can enter {} seconds later.", minutescount);
        return;
    }

    if(hashOriPass.compare(targetPassword))
    {
        DEBUGLOG("Begin to count, beacause password was entered incorrectly.");
       if(pCPwdAttackChecker->Wrong())
       {
            ERRORLOG("Password error.");
            devprikey_ack.set_code(-3);
            devprikey_ack.set_description("密码输入错误");
            return;
       }
       else
       {
           ERRORLOG("Enter the password incorrectly for the third time.");
            devprikey_ack.set_code(-30);
            devprikey_ack.set_description("第三次输入密码错误");
            return;
       }  
    }
    else 
    {
        DEBUGLOG("Reset to 0");
        pCPwdAttackChecker->Right();
        devprikey_ack.set_code(0);
        devprikey_ack.set_description("密码输入正确");
    }
    
    if (hashOriPass != targetPassword) 
    {
        devprikey_ack.set_version(getVersion());
        devprikey_ack.set_code(-3);
        devprikey_ack.set_description("Incorrect old password");
        return;
    }
  
    std::map<std::string, account>::iterator iter;
    bool flag = false;
    iter = g_AccountInfo.AccountList.begin();
    while(iter != g_AccountInfo.AccountList.end())
    {
        if(strlen(Bs58Addr.c_str()) == iter->first.length() &&!memcmp(iter->first.c_str(), Bs58Addr.c_str(), iter->first.length()))
        {
            flag = true;
            break;
        }
        iter++;
    }
   
    if(flag)
    {
        devprikey_ack.set_version(getVersion());
        devprikey_ack.set_code(0);
        devprikey_ack.set_description("success!");

        DevPrivateKeyInfo  *pDevPrivateKeyInfo = devprikey_ack.add_devprivatekeyinfo();
        pDevPrivateKeyInfo->set_base58addr(Bs58Addr);   
        char keystore_data[2400] = {0};
        int keystoredata_len = sizeof(keystore_data);
        g_AccountInfo.GetKeyStore(Bs58Addr.c_str(), passwd.c_str(),keystore_data, keystoredata_len);    
        pDevPrivateKeyInfo->set_keystore(keystore_data);
        char out_data[1024] = {0};
        int data_len = sizeof(out_data);
        g_AccountInfo.GetMnemonic(Bs58Addr.c_str(), out_data, data_len);
        pDevPrivateKeyInfo->set_mnemonic(out_data);
    }
    else
    {
        devprikey_ack.set_version(getVersion());
        devprikey_ack.set_code(-4);
        devprikey_ack.set_description("addr is not exist");
    }    
}

void HandleGetPledgeListReq(const std::shared_ptr<GetPledgeListReq>& req,  GetPledgeListAck& ack)
{
    ack.set_version(getVersion());
        
    std::string addr = req->addr();
    if (addr.length() == 0)
    {
        ack.set_code(-1);
        ack.set_description("The Bs58Addr input error");
        return;
    }

    std::vector<string> utxoes;
    DBReader db_reader;
    auto db_status = db_reader.GetPledgeAddressUtxo(addr, utxoes);
    if (DBStatus::DB_SUCCESS != db_status)
    {
        ack.set_code(-2);
        ack.set_description("Get pledge utxo error");
        return;
    }

    size_t size = utxoes.size();
    ack.set_total(size);
    if (size == 0)
    {
        ack.set_code(-3);
        ack.set_description("No pledge");
        return; 
    }

    reverse(utxoes.begin(), utxoes.end());

    uint32 index = 0;
    string txhash = req->txhash();
    if (txhash.empty())
    {
        index = 0;
    }
    else
    {
        size_t i = 0;
        for (; i < utxoes.size(); i++)
        {
            if (utxoes[i] == txhash)
            {
                break ;
            }
        }
        if (i == utxoes.size())
        {
            ack.set_code(-5);
            ack.set_description("Not found the hash");
            return ;
        }
        index = i + 1;
    }
    string lasthash;
    uint32 count = req->count();

    if (index > (size - 1))
    {
        ack.set_code(-4);
        ack.set_description("index out of range");
        return;
    }

    size_t range = index + count;
    if (range >= size)
    {
        range = size;
    }

    for (size_t i = index; i < range; i++)
    {
        std::string strUtxo = utxoes[i];
        
        std::string serTxRaw;
        db_status = db_reader.GetTransactionByHash(strUtxo, serTxRaw);
        if (DBStatus::DB_SUCCESS != db_status)
        {
            ERRORLOG("Get pledge tx error");
            continue;
        }

        CTransaction tx;
        tx.ParseFromString(serTxRaw);

        if(tx.vout_size() != 2)
        {
            ERRORLOG("invalid tx");
            continue;
        }

        if (tx.hash().length() == 0)
        {
            ERRORLOG("Get pledge tx error");
            continue;
        }

        std::string strBlockHash;
        db_status = db_reader.GetBlockHashByTransactionHash(tx.hash(), strBlockHash);
        if (DBStatus::DB_SUCCESS != db_status)
        {
            ERRORLOG("Get pledge block hash error");
            continue;
        }

        std::string serBlock;
        db_status = db_reader.GetBlockHeaderByBlockHash(strBlockHash, serBlock);
        if (db_status != 0)
        {
            ERRORLOG("Get pledge block error");
            continue;
        }

        CBlockHeader block;
        block.ParseFromString(serBlock);

        if (block.hash().empty())
        {
            ERRORLOG("Block error");
            continue;
        }
        
        std::vector<std::string> txOwnerVec;
		SplitString(tx.owner(), txOwnerVec, "_");

        if (txOwnerVec.size() == 0)
        {
            continue;
        }
        
        PledgeItem * pItem = ack.add_list();
        
        pItem->set_blockhash(block.hash());
        pItem->set_blockheight(block.height());
        pItem->set_utxo(strUtxo);
        pItem->set_time(tx.time());

        pItem->set_fromaddr(txOwnerVec[0]);

        for (int i = 0; i < tx.vout_size(); i++)
        {
            CTxout txout = tx.vout(i);
            if (txout.scriptpubkey() == VIRTUAL_ACCOUNT_PLEDGE)
            {
                pItem->set_toaddr(txout.scriptpubkey());
                pItem->set_amount(to_string((double_t)txout.value() / DECIMAL_NUM));
                break;
            }
        }

        nlohmann::json extra_json = nlohmann::json::parse(tx.extra());
        pItem->set_detail(extra_json["TransactionInfo"]["PledgeType"].get<std::string>());
        lasthash = strUtxo;
    }

    ack.set_lasthash(lasthash);
    ack.set_code(0);
    ack.set_description("success");
}


void HandleGetTxInfoListReq(const std::shared_ptr<GetTxInfoListReq>& req, GetTxInfoListAck & ack)
{
    ack.set_version(getVersion());
        
    std::string addr = req->addr();
    if (addr.length() == 0)
    {
        ack.set_code(-1);
        ack.set_description("The Bs58Addr input error");
        return;
    }
    DBReader db_reader;
    std::vector<std::string> txHashs;
    auto db_status = db_reader.GetAllTransactionByAddreess(addr, txHashs);
    if (db_status != 0)
    {
        ack.set_code(-2);
        ack.set_description("Get strTxHash error");
        return;
    }

    size_t size = txHashs.size();
    if (size == 0)
    {
        ack.set_code(-3);
        ack.set_description("No txhash");
        return; 
    }

    // 去重
    std::set<std::string> txHashsSet(txHashs.begin(), txHashs.end());
    
    txHashs.clear();
    for (auto & item : txHashsSet)
    {
        if (item.empty())
        {
            continue;
        }

        txHashs.push_back(item);
    }

    std::sort(txHashs.begin(), txHashs.end(), [](std::string aTxHash, std::string bTxHash){

        std::string serTxRaw;
        DBReader db_read;
        auto status = db_read.GetTransactionByHash(aTxHash, serTxRaw);
        if (DBStatus::DB_SUCCESS != status)
        {
            return true;
        }

        CTransaction aTx;
        aTx.ParseFromString(serTxRaw);

        status = db_read.GetTransactionByHash(bTxHash, serTxRaw);
        if (DBStatus::DB_SUCCESS != status)
        {
            return true;
        }

        CTransaction bTx;
        bTx.ParseFromString(serTxRaw);

        return aTx.time() > bTx.time();
    });

    size = txHashs.size();
    uint32 index = 0;
    string hash = req->txhash();
    if (hash.empty())
    {
        index = 0;
    }
    else
    {
        size_t i = 0;
        for (; i < txHashs.size(); i++)
        {
            if (txHashs[i] == hash)
            {
                break ;
            }
        }
        if (i == txHashs.size())
        {
            ack.set_code(-10);
            ack.set_description("Not found the hash!");
            return ;
        }
        index = i + 1;
    }
    string lastHash;
    uint32 count = 0;

    if (index > (size - 1))
    {
        ack.set_code(-4);
        ack.set_description("index out of range");
        return;
    }

    size_t total = size;
    size_t i = index;
    for ( ; i < size; i++)
    {
        std::string strTxHash = txHashs[i];

        std::string serTxRaw;
        db_status = db_reader.GetTransactionByHash(strTxHash, serTxRaw);
        if (DBStatus::DB_SUCCESS != db_status)
        {
            ack.set_code(-5);
            ack.set_description("Get tx raw error");
            return;
        }

        CTransaction tx;
        tx.ParseFromString(serTxRaw);

        if (tx.hash().length() == 0)
        {
            continue;
        }

        std::string strBlockHash;
        // db_status = pRocksDb->GetBlockHashByTransactionHash(txn, strTxHash, strBlockHash);
        db_status = db_reader.GetBlockHashByTransactionHash(strTxHash, strBlockHash);
        if (DBStatus::DB_SUCCESS != db_status)
        {
            continue;
        }

        std::string serBlockRaw;
        // db_status = pRocksDb->GetBlockByBlockHash(txn, strBlockHash, serBlockRaw);
        db_status = db_reader.GetBlockByBlockHash(strBlockHash, serBlockRaw);
        if (DBStatus::DB_SUCCESS != db_status)
        {
            continue;
        }

        CBlock cblock;
        cblock.ParseFromString(serBlockRaw);

        uint64_t amount = 0;
        CTxin txIn0 = tx.vin(0);
        if (txIn0.scriptsig().sign() == std::string(FEE_SIGN_STR) || 
            txIn0.scriptsig().sign() == std::string(EXTRA_AWARD_SIGN_STR))
        {
            // 奖励账号

            // 判断是否是奖励交易中的交易发起方账号
            bool isOriginator = false;
            uint64_t txOutAmount = 0;

            CTransaction tmpTx;
            for (auto & t : cblock.txs())
            {
                CTxin tmpTxIn0 = t.vin(0);
                if (tmpTxIn0.scriptsig().sign() != std::string(FEE_SIGN_STR) &&
                    tmpTxIn0.scriptsig().sign() != std::string(EXTRA_AWARD_SIGN_STR))
                {
                    tmpTx = t;
                }
            }

            for (auto & out : tx.vout())
            {
                if (out.scriptpubkey() == addr && out.value() == 0)
                {
                    isOriginator = true;
                    break;
                }
            }
            
            if (isOriginator)
            {
                // 如果是发起方的话不进入统计
                continue;
            }

            lastHash = txHashs[i];
            TxInfoItem * pItem = ack.add_list();
            pItem->set_txhash(tx.hash());
            pItem->set_time(tx.time());
            
            if (txIn0.scriptsig().sign() == std::string(FEE_SIGN_STR))
            {
                pItem->set_type(TxInfoType_Gas);
            }
            else if (txIn0.scriptsig().sign() == std::string(EXTRA_AWARD_SIGN_STR))
            {
                pItem->set_type(TxInfoType_Award);
            }

            for (auto & out : tx.vout())
            {
                if (out.scriptpubkey() == addr)
                {
                    txOutAmount = out.value();
                    break;
                }    
            }

            amount += txOutAmount;
            pItem->set_amount(to_string((double_t)amount / DECIMAL_NUM));
            count++;
            if (count >= req->count())
            {
                break;
            }
            
        }
        else
        {
            lastHash = txHashs[i];
            // 主交易
            TxInfoItem * pItem = ack.add_list();
            pItem->set_txhash(tx.hash());
            pItem->set_time(tx.time());

            std::vector<std::string> owners = TxHelper::GetTxOwner(tx);
            if (owners.size() == 1 && tx.vout_size() == 2)
            {
                // 质押和解除质押
                uint64_t gas = 0;
                for (auto & tmpTx : cblock.txs())
                {
                    CTxin txIn0 = tmpTx.vin(0);
                    if (txIn0.scriptsig().sign() == std::string(FEE_SIGN_STR))
                    {
                        for (auto & tmpTxOut : tmpTx.vout())
                        {
                            if (tmpTxOut.scriptpubkey() != addr)
                            {
                                gas += tmpTxOut.value();
                            }
                        }
                    }
                }

                std::vector<std::string> txOwnerVec;
                SplitString(tx.owner(), txOwnerVec, "_");
                if (txOwnerVec.size() == 0)
                {
                    continue;
                }

                if (owners[0] != txOwnerVec[0] && txOwnerVec[0].length() != 0)
                {
                    owners[0] = txOwnerVec[0];
                }

                if (owners[0] == addr && 
                    owners[0] == tx.vout(0).scriptpubkey() && 
                    tx.vout(0).scriptpubkey() == tx.vout(1).scriptpubkey())
                {
                    // 解除质押
                    pItem->set_type(TxInfoType_Redeem);
                    pItem->set_amount(to_string(((double_t)tx.vout(0).value() - gas) / DECIMAL_NUM));

                    count++;
                    if (count >= req->count())
                    {
                        break;
                    }
                    
                    continue;
                }
                else if (owners[0] == addr && 
                        (tx.vout(0).scriptpubkey() == VIRTUAL_ACCOUNT_PLEDGE || tx.vout(0).scriptpubkey() == VIRTUAL_ACCOUNT_PLEDGE))
                {
                    // 质押资产
                    pItem->set_type(TxInfoType_Pledge);
                    pItem->set_amount(to_string(((double_t)tx.vout(0).value() + gas) / DECIMAL_NUM));

                    count++;
                    if (count >= req->count())
                    {
                        break;
                    }
                    
                    continue;
                }
            }
            
            // 是否是发起方
            bool isOriginator = false;
            for (auto & tmpTxIn : tx.vin())
            {
                char buf[2048] = {0};
                size_t buf_len = sizeof(buf);
                GetBase58Addr(buf, &buf_len, 0x00, tmpTxIn.scriptsig().pub().c_str(), tmpTxIn.scriptsig().pub().size());
                if (std::string(buf) == addr)
                {
                    isOriginator = true;
                    break;
                }
            }
            pItem->set_type(isOriginator ? TxInfoType_Originator : TxInfoType_Receiver);

            for (auto & tmpTxOut : tx.vout())
            {
                if (isOriginator)
                {
                    //  发起方的话找非发起方的交易额
                    if (tmpTxOut.scriptpubkey() != addr)
                    {
                        amount += tmpTxOut.value();
                    }  
                }
                else
                {
                    // 非发起方的话直接用该交易额
                    if (tmpTxOut.scriptpubkey() == addr)
                    {
                        amount = tmpTxOut.value();
                    }    
                }
            }

            pItem->set_amount(to_string((double_t)amount / DECIMAL_NUM));

            if (isOriginator)
            {
                // 如果是主账号需要加上已付的手续费
                for (auto & tmpTx : cblock.txs())
                {
                    CTxin txIn0 = tmpTx.vin(0);
                    if (txIn0.scriptsig().sign() == std::string(FEE_SIGN_STR))
                    {
                        for (auto & tmpTxOut : tmpTx.vout())
                        {
                            if (tmpTxOut.scriptpubkey() != addr)
                            {
                                amount += tmpTxOut.value();
                                pItem->set_amount(to_string(((double_t)amount) / DECIMAL_NUM));
                            }
                        }
                    }
                }
            }
            count++;
            if (count >= req->count())
            {
                break;
            }
        }   
    }

    if (i == size)
    {
        i--; // 若一次性获得所有数据，索引需要减一
    }

    // 将已解除质押的交易设置为“质押但已解除”类型
    std::vector<std::string> redeemHash;
    for (auto & item : ack.list())
    {
        if (item.type() != TxInfoType_Redeem)
        {
            continue;
        }

        redeemHash.push_back(item.txhash());

    }

    for (auto & hash : redeemHash)
    {
        std::string serTxRaw;
        db_status = db_reader.GetTransactionByHash(hash, serTxRaw);
        if (db_status != 0)
        {
            continue;
        }

        CTransaction tx;
        tx.ParseFromString(serTxRaw);
        if (tx.hash().size() == 0)
        {
            continue;
        }

        auto extra = nlohmann::json::parse(tx.extra());
        auto txinfo = extra["TransactionInfo"];
        std::string utxo = txinfo["RedeemptionUTXO"];


        for (size_t i = 0; i != (size_t)ack.list_size(); ++i)
        {
            TxInfoItem * item = ack.mutable_list(i);
            if (item->type() == TxInfoType_Pledge && 
                item->txhash() == utxo)
            {
                item->set_type(TxInfoType_PledgedAndRedeemed);
            }
        }
    }

    ack.set_total(total);
    ack.set_lasthash(lastHash);
    
    ack.set_code(0);
    ack.set_description("success");

}

// 处理手机端块列表请求
void HandleGetBlockInfoListReq(const std::shared_ptr<GetBlockInfoListReq>& msg, const MsgData& msgdata)
{
    // 回执消息体
    GetBlockInfoListAck getBlockInfoListAck;
    getBlockInfoListAck.set_version( getVersion() );

    // 版本判断
    if( 0 != Util::IsVersionCompatible( msg->version() ) )
	{
        getBlockInfoListAck.set_code(-1);
        getBlockInfoListAck.set_description("version error! ");
        net_send_message<GetBlockInfoListAck>(msgdata, getBlockInfoListAck, net_com::Priority::kPriority_Middle_1);
		ERRORLOG("(HandleGetBlockInfoListReq) IsVersionCompatible error!");
		return ;
	}

    DBReader db_reader;
    uint64_t top = 0;
    if (DBStatus::DB_SUCCESS != db_reader.GetBlockTop(top) )
    {
        getBlockInfoListAck.set_code(-3);
		getBlockInfoListAck.set_description("GetBlockTop failed !");
		net_send_message<GetBlockInfoListAck>(msgdata, getBlockInfoListAck, net_com::Priority::kPriority_Middle_1);
		ERRORLOG("(HandleGetBlockInfoListReq) GetBlockTop failed !");
		return ;
    }

    getBlockInfoListAck.set_top(top);

    uint64_t txcount = 0;
    if (DBStatus::DB_SUCCESS != db_reader.GetTxCount(txcount) )
    {
        getBlockInfoListAck.set_code(-4);
		getBlockInfoListAck.set_description("GetTxCount failed!");
		net_send_message<GetBlockInfoListAck>(msgdata, getBlockInfoListAck, net_com::Priority::kPriority_Middle_1);
		ERRORLOG("(HandleGetBlockInfoListReq) GetTxCount failed!");
        return ;
    }

    getBlockInfoListAck.set_txcount(txcount);

    if (msg->count() < 0) 
    {
        getBlockInfoListAck.set_code(-5);
		getBlockInfoListAck.set_description("count cannot be less than 0!");
		net_send_message<GetBlockInfoListAck>(msgdata, getBlockInfoListAck, net_com::Priority::kPriority_Middle_1);
		ERRORLOG("(HandleGetBlockInfoListReq) count cannot be less than 0!");
        return ;
    }

    uint64_t maxHeight = 0;
    uint64_t minHeight = 0;

    if (msg->index() == 0)
    {
        maxHeight = top;
        minHeight = maxHeight > msg->count() ? maxHeight - msg->count() : 0;
    }
    else
    {
        maxHeight = msg->index() > top ? top : msg->index();
        minHeight = msg->index() > msg->count() ? msg->index() - msg->count() : 0;
    }

    for (uint32_t count = maxHeight; count > minHeight; --count)
    {
        std::vector<std::string> blockHashs;
        if (DBStatus::DB_SUCCESS != db_reader.GetBlockHashsByBlockHeight(count, blockHashs) )
        {
            getBlockInfoListAck.set_code(-7);
            getBlockInfoListAck.set_description("GetBlockHashsByBlockHeight failed !");
            net_send_message<GetBlockInfoListAck>(msgdata, getBlockInfoListAck, net_com::Priority::kPriority_Middle_1);
            ERRORLOG("(HandleGetBlockInfoListReq) GetBlockHashsByBlockHeight failed !");
            return ;
        }

        std::vector<CBlock> blocks;
        for (auto &blkHash : blockHashs)
        {
            std::string blockStr;
            if (DBStatus::DB_SUCCESS != db_reader.GetBlockByBlockHash(blkHash, blockStr) )
            {
                getBlockInfoListAck.set_code(-8);
                getBlockInfoListAck.set_description("GetBlockHeaderByBlockHash failed !");
                net_send_message<GetBlockInfoListAck>(msgdata, getBlockInfoListAck, net_com::Priority::kPriority_Middle_1);
                ERRORLOG("(HandleGetBlockInfoListReq) GetBlockHeaderByBlockHash failed !");
                return ;
            }

            CBlock cblock;
            cblock.ParseFromString(blockStr);
            if (cblock.hash().length() == 0)
            {
                continue;
            }
            blocks.push_back(cblock);
        }

        // 单层高度所有块按时间倒序
        std::sort(blocks.begin(), blocks.end(), [](CBlock & a, CBlock & b){
            return a.time() > b.time();
        });

        for (auto & cblock : blocks)
        {
            BlockInfoItem * pBlockInfoItem = getBlockInfoListAck.add_list();
            pBlockInfoItem->set_blockhash(cblock.hash());
            pBlockInfoItem->set_blockheight(cblock.height());
            pBlockInfoItem->set_time(cblock.time());

            CTransaction tx;
            for (auto & t : cblock.txs())
            {
                CTxin txin0 = t.vin(0);
                if (txin0.scriptsig().sign() != std::string(FEE_SIGN_STR) && 
                    txin0.scriptsig().sign() != std::string(EXTRA_AWARD_SIGN_STR))
                {
                    tx = t;
                    break;
                }
            }

            pBlockInfoItem->set_txhash(tx.hash());

            std::vector<std::string> owners = TxHelper::GetTxOwner(tx);
            for (auto &txOwner : owners)
            {
                pBlockInfoItem->add_fromaddr(txOwner);
            }

            uint64_t amount = 0;
            nlohmann::json extra = nlohmann::json::parse(tx.extra());
            std::string txType = extra["TransactionType"].get<std::string>();
            if (txType == TXTYPE_REDEEM)
            {
                nlohmann::json txInfo = extra["TransactionInfo"].get<nlohmann::json>();
                std::string redeemUtxo = txInfo["RedeemptionUTXO"];

                std::string txRaw;
                if (DBStatus::DB_SUCCESS != db_reader.GetTransactionByHash(redeemUtxo, txRaw) )
                {
                    getBlockInfoListAck.set_code(-9);
                    getBlockInfoListAck.set_description("GetTransactionByHash failed !");
                    net_send_message<GetBlockInfoListAck>(msgdata, getBlockInfoListAck, net_com::Priority::kPriority_Middle_1);
                    ERRORLOG("(HandleGetBlockInfoListReq) GetTransactionByHash failed !");
                }

                CTransaction utxoTx;
                std::string fromAddrTmp;
                utxoTx.ParseFromString(txRaw);

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

                pBlockInfoItem->add_toaddr(fromAddrTmp);
            }

            for (auto & txOut : tx.vout())
            {
                if (owners.end() != find(owners.begin(), owners.end(), txOut.scriptpubkey()))
                {
                    continue;
                }
                else
                {
                    pBlockInfoItem->add_toaddr(txOut.scriptpubkey());
                    amount += txOut.value();
                }
            }

            pBlockInfoItem->set_amount(to_string((double_t) amount / DECIMAL_NUM));
        }
    }

    getBlockInfoListAck.set_code(0);
    getBlockInfoListAck.set_description("successful");
    net_send_message<GetBlockInfoListAck>(msgdata, getBlockInfoListAck, net_com::Priority::kPriority_Middle_1);
}

// 处理手机端块详情请求
void HandleGetBlockInfoDetailReq(const std::shared_ptr<GetBlockInfoDetailReq>& msg, const MsgData& msgdata)
{
    DEBUGLOG("Receive data, version: {}, blockhash: {}", msg->version(), msg->blockhash());
    
    GetBlockInfoDetailAck getBlockInfoDetailAck;
    getBlockInfoDetailAck.set_version( getVersion() );
    getBlockInfoDetailAck.set_code(0);

    // 版本判断
    if( 0 != Util::IsVersionCompatible( msg->version() ) )
	{
        getBlockInfoDetailAck.set_code(-1);
        getBlockInfoDetailAck.set_description("version error! ");
        net_send_message<GetBlockInfoDetailAck>(msgdata, getBlockInfoDetailAck, net_com::Priority::kPriority_Middle_1);
		ERRORLOG("(HandleGetBlockInfoDetailReq) IsVersionCompatible error!");
		return ;
	}

    DBReader db_reader;
    std::string blockHeaderStr;
    if (DBStatus::DB_SUCCESS != db_reader.GetBlockByBlockHash(msg->blockhash(), blockHeaderStr) )
    {
        getBlockInfoDetailAck.set_code(-3);
		getBlockInfoDetailAck.set_description("GetBlockByBlockHash failed !");
		net_send_message<GetBlockInfoDetailAck>(msgdata, getBlockInfoDetailAck, net_com::Priority::kPriority_Middle_1);
		ERRORLOG("(HandleGetBlockInfoDetailReq) GetBlockByBlockHash failed !");
		return ;
    }

    CBlock cblock;
    cblock.ParseFromString(blockHeaderStr);

    getBlockInfoDetailAck.set_blockhash(cblock.hash());
    getBlockInfoDetailAck.set_blockheight(cblock.height());
    getBlockInfoDetailAck.set_merkleroot(cblock.merkleroot());
    getBlockInfoDetailAck.set_prevhash(cblock.prevhash());
    getBlockInfoDetailAck.set_time(cblock.time());

    CTransaction tx;
    CTransaction gasTx;
    CTransaction awardTx;
    for (auto & t : cblock.txs())
    {
        if (t.vin(0).scriptsig().sign() == std::string(FEE_SIGN_STR))
        {
            gasTx = t;
        }
        else if (t.vin(0).scriptsig().sign() == std::string(EXTRA_AWARD_SIGN_STR))
        {
            awardTx = t;
        }
        else
        {
            tx = t;
        }
    }

    int64_t totalAmount = 0;
    
    // 获取交易的发起方账号
    std::vector<std::string> txOwners;
    txOwners = TxHelper::GetTxOwner(tx);
    
    // 判断是否是解除质押情况
    bool isRedeem = false;
    if (tx.vout_size() == 2)
    {
        std::vector<std::string> txOwnerVec;
        SplitString(tx.owner(), txOwnerVec, "_");
        if (txOwnerVec.size() == 0)
        {
            getBlockInfoDetailAck.set_code(-4);
            getBlockInfoDetailAck.set_description("txowner empty !");
            net_send_message<GetBlockInfoDetailAck>(msgdata, getBlockInfoDetailAck, net_com::Priority::kPriority_Middle_1);
            
            return ;
        }

        if (tx.vout(0).scriptpubkey() == tx.vout(1).scriptpubkey() && 
            txOwnerVec[0] == tx.vout(0).scriptpubkey())
        {
            isRedeem = true;
        }
    }

    if (isRedeem)
    {
        int64_t amount = tx.vout(0).value();

        nlohmann::json extra = nlohmann::json::parse(tx.extra());
        nlohmann::json txInfo = extra["TransactionInfo"].get<nlohmann::json>();
        std::string redeemUtxo = txInfo["RedeemptionUTXO"];

        std::string serTxRaw;
        db_reader.GetTransactionByHash(redeemUtxo, serTxRaw);

        CTransaction pledgeTx;
        pledgeTx.ParseFromString(serTxRaw);
        if (pledgeTx.hash().length() == 0)
        {
            getBlockInfoDetailAck.set_code(-5);
            getBlockInfoDetailAck.set_description("get pledge transaction error");
            return;
        }

        for (auto & pledgeVout : pledgeTx.vout())
        {
            if (pledgeVout.scriptpubkey() == VIRTUAL_ACCOUNT_PLEDGE)
            {
                amount = pledgeVout.value();     
                break;
            }
        }
    
        int64_t gas = 0;
        for (auto & gasVout : gasTx.vout())
        {
            gas += gasVout.value();
        }
        
        if (gas == amount)
        {
            amount = 0;
        }
        else
        {
            amount -= gas;
        }

        BlockInfoOutAddr * pBlockInfoOutAddr = getBlockInfoDetailAck.add_blockinfooutaddr();
        pBlockInfoOutAddr->set_addr(tx.vout(0).scriptpubkey());
        pBlockInfoOutAddr->set_amount( std::to_string((double_t)amount / DECIMAL_NUM) );

        totalAmount  = amount;
    }
    else
    {
        // 计算交易总金额,不包括手续费
        for (int j = 0; j < tx.vout_size(); ++j)
        {
            CTxout txout = tx.vout(j);
            if (txOwners.end() == find (txOwners.begin(), txOwners.end(), txout.scriptpubkey() ) )
            {
                // 累计交易总值
                totalAmount += txout.value();

                // 分别记录每个账号的接收金额
                BlockInfoOutAddr * pBlockInfoOutAddr = getBlockInfoDetailAck.add_blockinfooutaddr();
                pBlockInfoOutAddr->set_addr(txout.scriptpubkey());

                int64_t value = txout.value();
                std::string amountStr = std::to_string( (double)value / DECIMAL_NUM );
                pBlockInfoOutAddr->set_amount( amountStr );
            }
        }
    }

    if (getBlockInfoDetailAck.blockinfooutaddr_size() > 5)
    {
        auto outAddr = getBlockInfoDetailAck.mutable_blockinfooutaddr();
        outAddr->erase(outAddr->begin() + 5, outAddr->end());
        getBlockInfoDetailAck.set_code(1);
    }

    for (int j = 0; j < gasTx.vout_size(); ++j)
    {
        CTxout txout = gasTx.vout(j);
        if (txOwners.end() == find (txOwners.begin(), txOwners.end(), txout.scriptpubkey() ) )
        {
            getBlockInfoDetailAck.add_signer(txout.scriptpubkey());
        }
    }

    std::string totalAmountStr = std::to_string( (double)totalAmount / DECIMAL_NUM );
    getBlockInfoDetailAck.set_tatalamount(totalAmountStr);

    net_send_message<GetBlockInfoDetailAck>(msgdata, getBlockInfoDetailAck, net_com::Priority::kPriority_Middle_1);
}


void HandleGetTxInfoDetailReq(const std::shared_ptr<GetTxInfoDetailReq>& req, GetTxInfoDetailAck & ack)
{
    ack.set_version(getVersion());
    ack.set_code(0);
        
    std::string strTxHash = req->txhash();
    if (strTxHash.length() == 0)
    {
        ack.set_code(-1);
        ack.set_description("The TxHash is empty");
        return;
    }
    ack.set_txhash(strTxHash);

    DBReader db_reader;
    std::string strBlockHash;
    auto db_status = db_reader.GetBlockHashByTransactionHash(strTxHash, strBlockHash);
    if (db_status != 0)
    {
        ack.set_code(-2);
        ack.set_description("The block hash is error");
        return;
    }

    std::string serBlockRaw;
    db_status = db_reader.GetBlockByBlockHash(strBlockHash, serBlockRaw);
    if (DBStatus::DB_SUCCESS != db_status)
    {
        ack.set_code(-3);
        ack.set_description("The block error");
        return;
    }

    CBlock cblock;
    cblock.ParseFromString(serBlockRaw);

    ack.set_blockhash(cblock.hash());
    ack.set_blockheight(cblock.height());

    CTransaction tx;
    CTransaction gasTx;
    CTransaction awardTx;
    for (auto & t : cblock.txs())
    {
        if (t.vin(0).scriptsig().sign() == std::string(FEE_SIGN_STR))
        {
            gasTx = t;
        }
        else if (t.vin(0).scriptsig().sign() == std::string(EXTRA_AWARD_SIGN_STR))
        {
            awardTx = t;
        }
        else
        {
            tx = t;
        }
    }

    if (tx.hash().length() == 0 || 
        gasTx.hash().length() == 0 ||
        awardTx.hash().length() == 0)
    {
        ack.set_code(-4);
        ack.set_description("get transaction error");
        return;
    }
    

    ack.set_time(tx.time());
    std::vector<string> owners = TxHelper::GetTxOwner(tx);
    for (auto & owner : owners)
    {
        ack.add_fromaddr(owner);
    }

    // 超过更多数据需要另行显示
    if (ack.fromaddr_size() > 5)
    {
        auto fromAddr = ack.mutable_fromaddr();
        fromAddr->erase(fromAddr->begin() + 5, fromAddr->end());
        ack.set_code(2);
    }

    int64_t amount = 0;

    // 判断是否是解除质押情况
    bool isRedeem = false;
    if (tx.vout_size() == 2)
    {
        std::vector<std::string> txOwnerVec;
        SplitString(tx.owner(), txOwnerVec, "_");

        if (txOwnerVec.size() == 0)
        {
            ack.set_code(-4);
            ack.set_description("txowner is empty");
            return;
        }

        if (tx.vout(0).scriptpubkey() == tx.vout(1).scriptpubkey() && 
            txOwnerVec[0] == tx.vout(0).scriptpubkey())
        {
            isRedeem = true;
        }
    }

    if (isRedeem)
    {
        amount = tx.vout(0).value();
        
        nlohmann::json extra = nlohmann::json::parse(tx.extra());
        nlohmann::json txInfo = extra["TransactionInfo"].get<nlohmann::json>();

        std::string redeemUtxo = txInfo["RedeemptionUTXO"];

        std::string serTxRaw;
        db_reader.GetTransactionByHash(redeemUtxo, serTxRaw);

        CTransaction pledgeTx;
        pledgeTx.ParseFromString(serTxRaw);
        if (pledgeTx.hash().length() == 0)
        {
            ack.set_code(-5);
            ack.set_description("get pledge transaction error");
            return;
        }

        for (auto & pledgeVout : pledgeTx.vout())
        {
            if (pledgeVout.scriptpubkey() == VIRTUAL_ACCOUNT_PLEDGE)
            {
                amount = pledgeVout.value();     
                break;
            }
        }
    
        ToAddr * toaddr = ack.add_toaddr();
        toaddr->set_toaddr(tx.vout(0).scriptpubkey());

        int64_t gas = 0;
        for (auto & gasVout : gasTx.vout())
        {
            gas += gasVout.value();
        }
        
        if (gas == amount)
        {
            amount = 0;
        }

        toaddr->set_amt(to_string( (double_t)amount / DECIMAL_NUM) );
    }
    else
    {
        std::map<std::string, int64_t> toAddrMap;
        for (auto owner : owners)
        {
            for (auto & txout : tx.vout())
            {
                std::string targetAddr = txout.scriptpubkey();
                if (owner != txout.scriptpubkey())
                {
                    toAddrMap.insert(std::make_pair(targetAddr, txout.value()));
                }
            }
        }

        // 排除vin中已有的
        for (auto iter = toAddrMap.begin(); iter != toAddrMap.end(); ++iter)
        {
            for (auto owner : owners)
            {
                if (owner ==  iter->first)
                {
                    iter = toAddrMap.erase(iter);
                }
            }
        }

        for (auto & item : toAddrMap)
        {
            amount += item.second;
            ToAddr * toaddr = ack.add_toaddr();
            toaddr->set_toaddr(item.first);
            toaddr->set_amt(to_string( (double_t)item.second / DECIMAL_NUM));
        }
        
        // 超过更多数据需要另行显示
        if (ack.toaddr_size() > 5)
        {
            auto toaddr = ack.mutable_toaddr();
            toaddr->erase(toaddr->begin() + 5, toaddr->end());
            ack.set_code(1);
        }
    }
 
    ack.set_amount(to_string((double_t)amount / DECIMAL_NUM));

    if (req->addr().size() != 0 || req->addr() != "0")
    {
        int64_t totalAward = 0; // 总奖励
        int64_t awardAmount = 0; // 个人奖励
        for (auto & txout : awardTx.vout())
        {
            std::string targetAddr = txout.scriptpubkey();
            if (targetAddr == req->addr())
            {
                awardAmount = txout.value();
            }
            totalAward += txout.value();
        }
        ack.set_awardamount(to_string( (double_t)awardAmount / DECIMAL_NUM) );
        ack.set_award(to_string( (double_t) totalAward / DECIMAL_NUM) );

        int64_t gas = 0;
        int64_t awardGas = 0;
        for (auto & txout : gasTx.vout())
        {
            gas += txout.value();
            std::string targetAddr = txout.scriptpubkey();
            if (targetAddr == req->addr())
            {
                awardGas = txout.value();
            }
        }
    
        ack.set_gas(to_string((double_t)gas / DECIMAL_NUM));
        ack.set_awardgas(to_string((double_t)awardGas / DECIMAL_NUM));
    }

    ack.set_description("success");
}

static void SetTxInfo(const CTransaction& tx, uint32_t height, tx_info* info, const string& address)
{
    std::string type;
    const std::string& extra = tx.extra();
    if (!extra.empty())
    {
        nlohmann::json extraJson = nlohmann::json::parse(extra);
        type = extraJson["TransactionType"].get<std::string>();        
        if (type == string(TXTYPE_TX))
        {
            info->set_type(TxType::TxTypeNormal);
        }
        else if (type == string(TXTYPE_PLEDGE))
        {
            info->set_type(TxType::TxTypePledge);
        }
        else if (type == string(TXTYPE_REDEEM))
        {
            info->set_type(TxType::TxTypeRedeem);
        }
        else
        {
            info->set_type(TxType::TxTypeNormal);
        }

        // calcuate gas
        uint64_t signFee = extraJson["SignFee"].get<uint64_t>();
        uint64_t needVerifyPreHashCount = extraJson["NeedVerifyPreHashCount"].get<uint64_t>();
        uint64_t packageFee = extraJson["PackageFee"].get<uint64_t>();
        uint64_t gas = signFee * (needVerifyPreHashCount - 1);
        gas += packageFee;
        info->set_gas(to_string(((double)gas)/DECIMAL_NUM));
    }
    info->set_hash(tx.hash());
    info->set_time(tx.time());
    info->set_height(height);

    // Set concrete type
    info->set_sub_type(TxSubType::TxSubTypeMain);
    if (tx.vin_size() > 0 && tx.vin(0).scriptsig().sign() == std::string(FEE_SIGN_STR))
    {
        info->set_sub_type(TxSubType::TxSubTypeGas);
    }
    else if (tx.vin_size() > 0 && tx.vin(0).scriptsig().sign() == std::string(EXTRA_AWARD_SIGN_STR))
    {
        info->set_sub_type(TxSubType::TxSubTypeAward);
    }

    std::vector<CTxout> txouts;
    std::vector<std::string> utxo_hashs;
    for (int i = 0; i < tx.vin_size(); i++)
    {
        CTxin vin = tx.vin(i);
        std::string utxo_hash = vin.prevout().hash();
        //遍历相关的交易是否已经添加
        bool flag = false;
        for (uint32_t j = 0; j < utxo_hashs.size(); j++)
        {
            if (utxo_hash == utxo_hashs.at(j))
                flag = true;
        }
        if (flag)
            continue;
        utxo_hashs.push_back(utxo_hash);
        std::string pub = vin.scriptsig().pub();
        std::string txaddr;
        if (!pub.empty())
        {
            txaddr = GetBase58Addr(pub);
        }

        tx_vin* pvin = info->add_in();
        pvin->set_address(txaddr);
        pvin->set_prev_hash(utxo_hash);
        uint64_t amount = TxHelper::GetUtxoAmount(utxo_hash, txaddr);
        //cout << "pub: " << pub << " amount " << amount << endl;
        pvin->set_amount(std::to_string((double)amount / DECIMAL_NUM));
    }
    for (int i = 0; i < tx.vout_size(); i++)
    {
        CTxout txout = tx.vout(i);
        //if (txout.scriptpubkey() == address && txout.value() == 0)
        if (txout.value() == 0)
        {
            //cout << "Filter: " << txout.scriptpubkey() << " value:" << txout.value() << endl;
            continue ;
        }
        tx_vout* pvout = info->add_out();
        pvout->set_address(txout.scriptpubkey());
        pvout->set_value(std::to_string((double)txout.value() / DECIMAL_NUM));
    }
}

// 处理获取质押和解质押请求 20210628  Liu
void HandleGetPledgeRedeemReq(const std::shared_ptr<GetPledgeRedeemReq>& req, GetPledgeRedeemAck& ack)
{
    ack.set_version(getVersion());
    ack.set_code(0);
    ack.set_description("success");

    string address = req->address();
    if (address.empty())
    {
        ack.set_code(-1);
        ack.set_description("The address is empty");
        return ;
    }

    std::vector<UtxoTx> utxt_out;
    if (!TransUtils::GetPledgeAndRedeemUtxoByAddr(address, utxt_out))
    {
        ack.set_code(-2);
        ack.set_description("Get pledge and redeem error.");
        return ;
    }
    if (utxt_out.empty())
    {
        ack.set_code(1);
        ack.set_description("list is empty!");
        return ;
    }

    for (auto it = utxt_out.cbegin(); it != utxt_out.cend(); it++)
    {
        tx_info* txinfo = ack.add_info();
        SetTxInfo(it->utxo, it->height, txinfo, address);
    }
}

// 处理获取指定账户的签名交易的请求 20210628  Liu
void HandleGetSignAndAwardTxReq(const std::shared_ptr<GetSignAndAwardTxReq>& req, GetSignAndAwardTxAck& ack)
{
    ack.set_version(getVersion());
    ack.set_code(0);
    ack.set_description("success");
    ack.set_total(0);

    string address = req->address();
    string txhash = req->txhash();
    uint32 count = req->count();
    uint32 index = 0;

    if (address.empty())
    {
        ack.set_code(-1);
        ack.set_description("The address is empty");
        return ;
    }

    std::vector<UtxoTx> utxt_out;
    if (!TransUtils::GetSignAndAwardUtxoByAddr(address, utxt_out))
    {
        ack.set_code(-2);
        ack.set_description("Get sign and award error");
        return ;
    }
    if (utxt_out.empty())
    {
        ack.set_code(1);
        ack.set_description("list is empty!");
        return ;
    }
    ack.set_total(utxt_out.size());

    if (txhash.empty())
    {
        index = 0;
    }
    else
    {
        uint32 i = 0;
        for (; i < utxt_out.size(); i++)
        {
            if (utxt_out[i].utxo.hash() == txhash)
            {
                break;
            }
        }
        if (i == utxt_out.size())
        {
            ack.set_code(-3);
            ack.set_description("not found start hash");
            return ;
        }
        index = i + 1;
    }
    if (index >= utxt_out.size())
    {
        ack.set_code(-4);
        ack.set_description("Index out of range.");
        return ;
    }

    size_t end = index + count;
    if (end > utxt_out.size())
    {
        end = utxt_out.size();
    }

    string lasthash;
    for (size_t i = index; i < end; i++)
    {
        tx_info* tx = ack.add_info();
        SetTxInfo(utxt_out[i].utxo, utxt_out[i].height, tx, address);
        lasthash = utxt_out[i].utxo.hash();
    }

    //cout << "lasthash=" << lasthash << " total=" << ack.total() << endl;
    ack.set_lasthash(lasthash);
}

void HandleGetUtxoReq(const std::shared_ptr<GetUtxoReq>& req, GetUtxoAck& ack)
{
    ack.set_version(getVersion());

    string address = req->address();
    if (address.empty() || !CheckBase58Addr(address))
    {
        ack.set_code(-1);
        ack.set_description("address is invalid!");
        ERRORLOG("(HandleGetUtxoReq) address is invalid!");
        return;
    }

    ack.set_address(address);

    std::vector<TxHelper::Utxo> utxos;
    if (GetUtxos(address, utxos) != 0)
    {
        ack.set_code(-3);
        ack.set_description("GetUtxoHashsByAddress failed");
        ERRORLOG("HandleGetUtxoReq GetUtxoHashsByAddress failed");
        return;    
    }

    for (auto & item : utxos)
    {
        Utxo* utxo = ack.add_utxos();
        utxo->set_hash(item.hash);
        utxo->set_value(item.value);
        utxo->set_n(item.n);
    }
    
    ack.set_code(0);
    ack.set_description("success");
}


void assign(const std::shared_ptr<Message> &msg, const MsgData &msgdata, const std::string msg_name) 
{
    DEBUGLOG("msg_name:{} ", msg_name);

    if (msg_name == "GetNodeServiceFeeReq") 
    {
        const std::shared_ptr<GetNodeServiceFeeReq>ack_msg = dynamic_pointer_cast<GetNodeServiceFeeReq>(msg);

        GetNodeServiceFeeAck fee_ack;
        if (!is_version) 
        {
            GetNodeServiceFee(ack_msg, fee_ack);
        } 
        else 
        {
            fee_ack.set_version(getVersion());
            fee_ack.set_code(is_version);
            fee_ack.set_description("版本错误");
        }

        net_send_message<GetNodeServiceFeeAck>(msgdata, fee_ack);
    } 
    else if (msg_name == "SetServiceFeeReq") 
    {
        const std::shared_ptr<SetServiceFeeReq>ack_msg = dynamic_pointer_cast<SetServiceFeeReq>(msg);

        int ret = 0;
        SetServiceFeeAck amount_ack;
        if (!is_version) 
        {
            ret = SetServiceFee(ack_msg, amount_ack);
        } 
        else 
        {
            amount_ack.set_version(getVersion());
            amount_ack.set_code(is_version);
            amount_ack.set_description("版本错误");
        }

        net_send_message<SetServiceFeeAck>(msgdata, amount_ack);

        if (ret == 0)
        {
            uint64_t service_fee = 0;
            auto status = DBReader().GetDeviceSignatureFee(service_fee);
            if (!status) 
            {
                net_update_fee_and_broadcast(service_fee);
                DEBUGLOG("update_fee_and_broadcast ServiceFee = {}", service_fee);
                return;
            }
        }

    } 
    else if (msg_name == "GetServiceInfoReq") 
    {
        const std::shared_ptr<GetServiceInfoReq>ack_msg = dynamic_pointer_cast<GetServiceInfoReq>(msg);

       HandleGetServiceInfoReq(ack_msg, msgdata);
    } 
    else if (msg_name == "GetPackageFeeReq") 
    {
        const std::shared_ptr<GetPackageFeeReq>ack_msg = dynamic_pointer_cast<GetPackageFeeReq>(msg);

        GetPackageFeeAck amount_ack;
        if (!is_version) 
        {
            GetPackageFee(ack_msg, amount_ack);
        } 
        else 
        {
            amount_ack.set_version(getVersion());
            amount_ack.set_code(is_version);
            amount_ack.set_description("版本错误");
        }

        net_send_message<GetPackageFeeAck>(msgdata, amount_ack);
    } 
    else if (msg_name == "GetAmountReq") 
    {
        const std::shared_ptr<GetAmountReq>ack_msg = dynamic_pointer_cast<GetAmountReq>(msg);

        GetAmountAck amount_ack;
        if (!is_version) 
        {
            GetAmount(ack_msg, amount_ack);
        } 
        else 
        {
            amount_ack.set_version(getVersion());
            amount_ack.set_code(is_version);
            amount_ack.set_description("版本错误");
        }

        net_send_message<GetAmountAck>(msgdata, amount_ack, net_com::Priority::kPriority_Middle_1);
    } 
    else if (msg_name == "GetBlockInfoReq") 
    {
        const std::shared_ptr<GetBlockInfoReq>ack_msg = dynamic_pointer_cast<GetBlockInfoReq>(msg);

        DEBUGLOG("GetBlockInfoReq: height:{}, count:{}", ack_msg->height(), ack_msg->count());

        GetBlockInfoAck block_info_ack;
        if (!is_version) 
        {
            BlockInfoReq(ack_msg, block_info_ack);
        } 
        else 
        {
            block_info_ack.set_version(getVersion());
            block_info_ack.set_code(is_version);
            block_info_ack.set_description("版本错误");
        }

        net_send_message<GetBlockInfoAck>(msgdata, block_info_ack, net_com::Priority::kPriority_Middle_1);
    } 
    else if (msg_name == "GetDevPasswordReq") 
    {
        const std::shared_ptr<GetDevPasswordReq>ack_msg = dynamic_pointer_cast<GetDevPasswordReq>(msg);
        DEBUGLOG("GetDevPasswordReq: pass {}", ack_msg->password());

        GetDevPasswordAck pass_ack;
        if (!is_version) 
        {
            GainDevPasswordReq(ack_msg, pass_ack);
        } 
        else 
        {
            pass_ack.set_version(getVersion());
            pass_ack.set_code(is_version);
            pass_ack.set_description("版本错误");
        }

        net_send_message<GetDevPasswordAck>(msgdata, pass_ack);
    } 
    else if (msg_name == "SetDevPasswordReq") 
    {
        const std::shared_ptr<SetDevPasswordReq>ack_msg = dynamic_pointer_cast<SetDevPasswordReq>(msg);
        DEBUGLOG("SetDevPasswordReq: old_pass:{}, new_pass:{} ", ack_msg->old_pass(), ack_msg->new_pass());

        SetDevPasswordAck pass_ack;
        if (!is_version) 
        {
            DevPasswordReq(ack_msg, pass_ack);
        } 
        else 
        {
            pass_ack.set_version(getVersion());
            pass_ack.set_code(is_version);
            pass_ack.set_description("版本错误");
        }

        net_send_message<SetDevPasswordAck>(msgdata, pass_ack);
    } 
    else if (msg_name == "GetAddrInfoReq") 
    {
        const std::shared_ptr<GetAddrInfoReq>ack_msg = dynamic_pointer_cast<GetAddrInfoReq>(msg);

        GetAddrInfoAck addr_ack;
        if (!is_version) 
        {
            GetTransactionInfo(ack_msg, addr_ack);
        } 
        else 
        {
            addr_ack.set_version(getVersion());
            addr_ack.set_code(is_version);
            addr_ack.set_description("版本错误");
        }

        net_send_message<GetAddrInfoAck>(msgdata, addr_ack);
    } 
    else if (msg_name == "GetNodeInfoReq") 
    {
        const std::shared_ptr<GetNodeInfoReq>ack_msg = dynamic_pointer_cast<GetNodeInfoReq>(msg);

        GetNodeInfoAck node_ack;
        if (!is_version) 
        {
            GetNodInfo(node_ack);
        } 
        else
        {
            node_ack.set_version(getVersion());
            node_ack.set_code(is_version);
            node_ack.set_description("版本错误");
        }

        net_send_message<GetNodeInfoAck>(msgdata, node_ack);
    } 
    else if (msg_name == "GetClientInfoReq") 
    {
        const std::shared_ptr<GetClientInfoReq>ack_msg = dynamic_pointer_cast<GetClientInfoReq>(msg);

        GetClientInfoAck info_ack;
        if (!is_version) 
        {
            GetClientInfo(ack_msg, info_ack);
        } 
        else 
        {
            info_ack.set_version(getVersion());
            info_ack.set_code(is_version);
            info_ack.set_description("版本错误");
        }

        net_send_message<GetClientInfoAck>(msgdata, info_ack);
    }
    else if(msg_name == "GetDevPrivateKeyReq")
    {
        const std::shared_ptr<GetDevPrivateKeyReq>ack_msg = dynamic_pointer_cast<GetDevPrivateKeyReq>(msg);
        GetDevPrivateKeyAck devprikey_ack ;
        if (!is_version) 
        {
            HandleGetDevPrivateKeyReq(ack_msg, devprikey_ack);
        } 
        else 
        {
            devprikey_ack.set_version(getVersion());
            devprikey_ack.set_code(is_version);
            devprikey_ack.set_description("版本错误");
        }
        net_send_message<GetDevPrivateKeyAck>(msgdata, devprikey_ack);
    }
    else if (msg_name == "GetPledgeListReq")
    {
        const std::shared_ptr<GetPledgeListReq> req = dynamic_pointer_cast<GetPledgeListReq>(msg);
        GetPledgeListAck ack;
        if (!is_version)
        {
            HandleGetPledgeListReq(req, ack);
        }
        else
        {
            ack.set_version(getVersion());
            ack.set_code(is_version);
            ack.set_description("版本错误");
        }

        net_send_message<GetPledgeListAck>(msgdata, ack, net_com::Priority::kPriority_Middle_1);
    }
    else if (msg_name == "GetTxInfoListReq")
    {
        const std::shared_ptr<GetTxInfoListReq> req = dynamic_pointer_cast<GetTxInfoListReq>(msg);
        GetTxInfoListAck ack;
        if (!is_version)
        {
            HandleGetTxInfoListReq(req, ack);
        }
        else
        {
            ack.set_version(getVersion());
            ack.set_code(is_version);
            ack.set_description("版本错误");
        }

        net_send_message<GetTxInfoListAck>(msgdata, ack, net_com::Priority::kPriority_Middle_1);
        
    }
    else if (msg_name == "GetBlockInfoListReq")
    {
        const std::shared_ptr<GetBlockInfoListReq> req = dynamic_pointer_cast<GetBlockInfoListReq>(msg);

        HandleGetBlockInfoListReq(req, msgdata);
    }
    else if (msg_name == "GetBlockInfoDetailReq")
    {
        const std::shared_ptr<GetBlockInfoDetailReq> req = dynamic_pointer_cast<GetBlockInfoDetailReq>(msg);

        HandleGetBlockInfoDetailReq(req, msgdata);
    }
    else if (msg_name == "GetTxInfoDetailReq")
    {
        const std::shared_ptr<GetTxInfoDetailReq> req = dynamic_pointer_cast<GetTxInfoDetailReq>(msg);
        GetTxInfoDetailAck ack;
        if (!is_version)
        {
            HandleGetTxInfoDetailReq(req, ack);
        }
        else
        {
            ack.set_version(getVersion());
            ack.set_code(is_version);
            ack.set_description("版本错误");
        }
        
        net_send_message<GetTxInfoDetailAck>(msgdata, ack, net_com::Priority::kPriority_Middle_1);
    }
    else if (msg_name == "TestConnectReq")
    {
        TestConnectAck ack;
        ack.set_version(getVersion());
        ack.set_code(0);
        net_send_message<TestConnectAck>(msgdata, ack);
    }
    else if (msg_name == "GetTxPendingListReq")
    {
        const std::shared_ptr<GetTxPendingListReq> req = dynamic_pointer_cast<GetTxPendingListReq>(msg);

        HandleGetTxPendingListReq(req, msgdata);
    }
    else if (msg_name == "GetTxFailureListReq")
    {
        const std::shared_ptr<GetTxFailureListReq> req = dynamic_pointer_cast<GetTxFailureListReq>(msg);
        GetTxFailureListAck ack;
        HandleGetTxFailureListReq(req, ack);
        net_send_message<GetTxFailureListAck>(msgdata, ack, net_com::Priority::kPriority_Middle_1);
    }
    else if (msg_name == "GetTxByHashReq")
    {
        const std::shared_ptr<GetTxByHashReq> req = dynamic_pointer_cast<GetTxByHashReq>(msg);

       HandleGetTxByHashReq(req, msgdata);
    }
    else if (msg_name == "CheckNodeHeightReq")
    {
        const std::shared_ptr<CheckNodeHeightReq> req = dynamic_pointer_cast<CheckNodeHeightReq>(msg);

       handleCheckNodeHeightReq(req, msgdata);
    }
    else if (msg_name == "GetPledgeRedeemReq")
    {
        const std::shared_ptr<GetPledgeRedeemReq> req = dynamic_pointer_cast<GetPledgeRedeemReq>(msg);
        GetPledgeRedeemAck ack;
        HandleGetPledgeRedeemReq(req, ack);
        net_send_message<GetPledgeRedeemAck>(msgdata, ack, net_com::Priority::kPriority_Middle_1);
    }
    else if (msg_name == "GetSignAndAwardTxReq")
    {
        const std::shared_ptr<GetSignAndAwardTxReq> req = dynamic_pointer_cast<GetSignAndAwardTxReq>(msg);
        GetSignAndAwardTxAck ack;
        HandleGetSignAndAwardTxReq(req, ack);
        net_send_message<GetSignAndAwardTxAck>(msgdata, ack, net_com::Priority::kPriority_Middle_1);
    }
    else if (msg_name == "GetUtxoReq")
    {
        const std::shared_ptr<GetUtxoReq> req = dynamic_pointer_cast<GetUtxoReq>(msg);
        GetUtxoAck ack;
        HandleGetUtxoReq(req, ack);
        net_send_message<GetUtxoAck>(msgdata, ack, net_com::Priority::kPriority_Middle_1);
    }
    // TODO

}

//version 4-3.0.14
//效验
int verify(const std::shared_ptr<Message> &msg, const MsgData &msgdata)
{
    const google::protobuf::Descriptor *descriptor = msg->GetDescriptor();
    const google::protobuf::Reflection *reflection = msg->GetReflection();

    const google::protobuf::FieldDescriptor* field = descriptor->field(0);

    std::string version = reflection->GetString(*msg, field);
    is_version = Util::IsVersionCompatible(version);
    assign(msg, msgdata, descriptor->name());
    return 0;
}

}//namespace end


void MuiltipleApi() 
{
    /** new s **/
    //获取全网节点矿费 std::map<std::string, uint64_t> net_get_node_ids_and_fees()
    net_register_callback<GetNodeServiceFeeReq>([](const std::shared_ptr<GetNodeServiceFeeReq> &msg, 
    const MsgData &msgdata) 
    {
        return m_api::verify(msg, msgdata);
    });

    net_register_callback<SetServiceFeeReq>([](const std::shared_ptr<SetServiceFeeReq>& msg, 
    const MsgData& msgdata) 
    {
        return m_api::verify(msg, msgdata);
    });

    //获取矿机信息(矿机端 需要请求服务节点接口 异步返回给矿机端 矿机端返回给手机端)
    net_register_callback<GetServiceInfoReq>([](const std::shared_ptr<GetServiceInfoReq>& msg, 
    const MsgData& msgdata) 
    {
        return m_api::verify(msg, msgdata);
    });

    net_register_callback<GetPackageFeeReq>([](const std::shared_ptr<GetPackageFeeReq>& msg, 
    const MsgData& msgdata) 
    {
        return m_api::verify(msg, msgdata);
    });
    /** new e **/
    net_register_callback<GetAmountReq>([](const std::shared_ptr<GetAmountReq>& msg, 
    const MsgData& msgdata) 
    {
        return m_api::verify(msg, msgdata);
    });

    net_register_callback<GetBlockInfoReq>([](const std::shared_ptr<GetBlockInfoReq>& msg, 
    const MsgData& msgdata) 
    {
        return m_api::verify(msg, msgdata);
    });

    net_register_callback<GetDevPasswordReq>([](const std::shared_ptr<GetDevPasswordReq>& msg, 
    const MsgData& msgdata) 
    {
        return m_api::verify(msg, msgdata);
    });

    net_register_callback<SetDevPasswordReq>([](const std::shared_ptr<SetDevPasswordReq>& msg, 
    const MsgData& msgdata) 
    {
        return m_api::verify(msg, msgdata);
    });

    net_register_callback<GetAddrInfoReq>([](const std::shared_ptr<GetAddrInfoReq>& msg, 
    const MsgData& msgdata) 
    {
        return m_api::verify(msg, msgdata);
    });

    net_register_callback<GetNodeInfoReq>([](const std::shared_ptr<GetNodeInfoReq>& msg, 
    const MsgData& msgdata) 
    {
        return m_api::verify(msg, msgdata);
    });

    net_register_callback<GetClientInfoReq>([](const std::shared_ptr<GetClientInfoReq>& msg, 
    const MsgData& msgdata) 
    {
        return m_api::verify(msg, msgdata);
    });

    net_register_callback<GetDevPrivateKeyReq>([](const std::shared_ptr<GetDevPrivateKeyReq>& msg, 
    const MsgData& msgdata) 
    {
        return m_api::verify(msg, msgdata);
    });

    
    net_register_callback<GetPledgeListReq>([](const std::shared_ptr<GetPledgeListReq>& msg, 
    const MsgData& msgdata) 
    {
        return m_api::verify(msg, msgdata);
    });

    net_register_callback<GetTxInfoListReq>([](const std::shared_ptr<GetTxInfoListReq>& msg, 
    const MsgData& msgdata) 
    {

        return m_api::verify(msg, msgdata);
    });

    net_register_callback<GetBlockInfoListReq>([](const std::shared_ptr<GetBlockInfoListReq>& msg, 
    const MsgData& msgdata) 
    {
        return m_api::verify(msg, msgdata);
    });

    net_register_callback<GetBlockInfoDetailReq>([](const std::shared_ptr<GetBlockInfoDetailReq>& msg, 
    const MsgData& msgdata) 
    {
        return m_api::verify(msg, msgdata);
    });
    
    net_register_callback<GetTxInfoDetailReq>([](const std::shared_ptr<GetTxInfoDetailReq>& msg, 
    const MsgData& msgdata) 
    {

        return m_api::verify(msg, msgdata);
    });

    net_register_callback<TestConnectReq>([](const std::shared_ptr<TestConnectReq>& msg, 
    const MsgData& msgdata) 
    {
        return m_api::verify(msg, msgdata);
    });

    net_register_callback<GetTxPendingListReq>([](const std::shared_ptr<GetTxPendingListReq>& msg, 
    const MsgData& msgdata) 
    {
        return m_api::verify(msg, msgdata);
    });

    net_register_callback<GetTxFailureListReq>([](const std::shared_ptr<GetTxFailureListReq>& msg, 
    const MsgData& msgdata) 
    {
        return m_api::verify(msg, msgdata);
    });
    
    net_register_callback<GetTxByHashReq>([](const std::shared_ptr<GetTxByHashReq>& msg, 
    const MsgData& msgdata) 
    {
        return m_api::verify(msg, msgdata);
    });

    net_register_callback<CheckNodeHeightReq>([](const std::shared_ptr<CheckNodeHeightReq>& msg, 
    const MsgData& msgdata) 
    {
        return m_api::verify(msg, msgdata);
    });

    net_register_callback<GetPledgeRedeemReq>([](const std::shared_ptr<GetPledgeRedeemReq>& msg, 
    const MsgData& msgdata) 
    {
        return m_api::verify(msg, msgdata);
    });

    net_register_callback<GetSignAndAwardTxReq>([](const std::shared_ptr<GetSignAndAwardTxReq>& msg, 
    const MsgData& msgdata) 
    {
        return m_api::verify(msg, msgdata);
    });

    net_register_callback<GetUtxoReq>([](const std::shared_ptr<GetUtxoReq>& msg, 
    const MsgData& msgdata) 
    {
        return m_api::verify(msg, msgdata);
    });


}

static void InsertTxPendingToAckList(std::vector<TxVinCache::Tx>& vectTxs, GetTxPendingListAck& ack)
{
    for (auto iter = vectTxs.begin(); iter != vectTxs.end(); ++iter)
    {
        TxPendingItem* txItem = ack.add_list();
        txItem->set_txhash(iter->txHash);
        for (const auto& in : iter->vins)
        {
            txItem->add_vins(in);
        }
        for (const auto& from : iter->from)
        {
            txItem->add_fromaddr(from);
        }
        for (const auto& to : iter->to)
        {
            txItem->add_toaddr(to);
        }
        txItem->set_amount(iter->amount);
        txItem->set_time(iter->timestamp);
        txItem->set_detail("");
        txItem->set_gas(iter->gas);
        for (const auto& amount : iter->toAmount)
        {
            txItem->add_toamount(amount);
        }

        if (iter->type == 0)
            txItem->set_type(TxTypeNormal);
        else if (iter->type == 1)
            txItem->set_type(TxTypePledge);
        else if (iter->type == 2)
            txItem->set_type(TxTypeRedeem);
        else
            txItem->set_type(TxTypeNormal);
    }
}

void HandleGetTxPendingListReq(const std::shared_ptr<GetTxPendingListReq>& req, const MsgData& msgdata)
{
    GetTxPendingListAck ack;
    ack.set_version(getVersion());
    ack.set_code(0);
    ack.set_description("TxPending");

    if (req->addr_size() == 0)
    {
        std::vector<TxVinCache::Tx> vectTxs;
        MagicSingleton<TxVinCache>::GetInstance()->GetAllTx(vectTxs);
        InsertTxPendingToAckList(vectTxs, ack);
    }
    else
    {
        for (int i = 0; i < req->addr_size(); i++)
        {
            string fromAddr = req->addr(i);
            std::vector<TxVinCache::Tx> vectTxs;
            MagicSingleton<TxVinCache>::GetInstance()->Find(fromAddr, vectTxs);
            InsertTxPendingToAckList(vectTxs, ack);

            ack.add_addr(fromAddr);
        }
    }

    net_send_message<GetTxPendingListAck>(msgdata, ack, net_com::Priority::kPriority_Middle_1);
}

void HandleGetTxFailureListReq(const std::shared_ptr<GetTxFailureListReq>& req, GetTxFailureListAck& ack)
{
    ack.set_version(getVersion());
    ack.set_code(0);
    ack.set_description("Failure Transaction");

    string addr = req->addr();
    uint32 index = 0;
    string txhash = req->txhash();
    uint32 count = req->count();
    DEBUGLOG("In HandleGetTxFailureListReq addr:{}, txhash:{}, count:{}", addr, txhash, count);

    if (addr.empty())
    {
        ack.set_code(-1);
        ack.set_description("The addr is empty");
        return ;
    }
    
    std::vector<TxVinCache::Tx> vectTx;
    MagicSingleton<TxFailureCache>::GetInstance()->Find(addr, vectTx);

    ack.set_total(vectTx.size());
    size_t size = vectTx.size();
    if (size == 0)
    {
        ack.set_code(-2);
        ack.set_description("No failure of the transaction.");
        return ;
    }

    if (txhash.empty())
    {
        index = 0;
    }
    else
    {
        size_t i = 0;
        for (; i < vectTx.size(); i++)
        {
            if (vectTx[i].txHash == txhash)
            {
                break ;
            }
        }
        if (i == vectTx.size())
        {
            ack.set_code(-4);
            ack.set_description("Not found the txhash.");
            return ;
        }
        index = i + 1;
    }
    string lasthash;    

    if (index > (size - 1))
    {
        //index = std::max(size - 1, 0);
        ack.set_code(-3);
        ack.set_description("Index out of range.");
        return ;
    }

    size_t end = index + count;
    if (end > size)
    {
        end = size;
    }

    for (size_t i = index; i < end; i++)
    {
        TxVinCache::Tx& iter = vectTx[i];

        TxFailureItem* txItem = ack.add_list();
        txItem->set_txhash(iter.txHash);
        DEBUGLOG("In HandleGetTxFailureListReq {}", iter.txHash);
        for (const auto& in : iter.vins)
        {
            txItem->add_vins(in);
        }
        for (const auto& from : iter.from)
        {
            txItem->add_fromaddr(from);
        }
        for (const auto& to : iter.to)
        {
            txItem->add_toaddr(to);
        }
        txItem->set_amount(iter.amount);
        txItem->set_time(iter.timestamp);
        txItem->set_detail("");
        txItem->set_gas(iter.gas);
        for (const auto& amount : iter.toAmount)
        {
            txItem->add_toamount(amount);
        }

        if (iter.type == 0)
            txItem->set_type(TxTypeNormal);
        else if (iter.type == 1)
            txItem->set_type(TxTypePledge);
        else if (iter.type == 2)
            txItem->set_type(TxTypeRedeem);
        else
            txItem->set_type(TxTypeNormal);

        lasthash = iter.txHash;
    }

    ack.set_lasthash(lasthash);

} 

void HandleGetTxByHashReq(const std::shared_ptr<GetTxByHashReq>& req, const MsgData& msgdata)
{
    GetTxByHashAck  TxByHashAck;
	TxByHashAck.set_version(getVersion());
    vector<string>TransactionHash;
    for(int i = 0;i < req->txhash_size(); i++)
    {
        TransactionHash.push_back(req->txhash(i));
        TxByHashAck.add_echotxhash(req->txhash(i));
    }

    vector<CTransaction>  Tx;
    bool result =  GetTxByTxHashFromRocksdb(TransactionHash,Tx);
    if(!result)
    {
        ERRORLOG("(HandleGetTxByHashReq) GetTxByTxHashFromRocksdb failed !");
        TxByHashAck.set_code(-1);
        TxByHashAck.set_description("GetTxByTxHashFromRocksdb failed!");
        net_send_message<GetTxByHashAck>(msgdata, TxByHashAck, net_com::Priority::kPriority_Middle_1);
        return;  
    }
    TxItem *pTxItem =  TxByHashAck.add_list();
    CTransaction  utxoTx;
    for(unsigned int i = 0;i < Tx.size();i++)
    {
        utxoTx = Tx[i];
        pTxItem->set_txhash(utxoTx.hash());
		pTxItem->set_time(utxoTx.time());
        DEBUGLOG("set_txhash = {}, set_time = {}", utxoTx.hash(), utxoTx.time());

        for (int i = 0; i < utxoTx.vin_size(); i++)
		{
			CTxin vin = utxoTx.vin(i);
			std::string  utxo_hash = vin.prevout().hash();
			std::string  pub = vin.scriptsig().pub();
			std::string addr = GetBase58Addr(pub); 
			pTxItem->add_fromaddr(addr);
		}
        std::vector<std::string> txOwners;
    	txOwners = TxHelper::GetTxOwner(utxoTx);
        std::map<std::string, int64_t> toAddr;
        int64_t amount = 0;
		for (int i = 0; i < utxoTx.vout_size(); i++)
		{
			CTxout txout = utxoTx.vout(i);	
            if (txOwners.end() == find (txOwners.begin(), txOwners.end(), txout.scriptpubkey() ) )
            {
                toAddr.insert(std::make_pair(txout.scriptpubkey(), txout.value()));
            }
		}
        
        for (auto & item : toAddr)
        {
             amount += item.second;
            pTxItem->add_toaddr(item.first);;
            pTxItem->set_amount(std::to_string( (double)amount/DECIMAL_NUM ));
        }
        
		string blockhash;
		unsigned height;
        DBReader db_reader;
		auto status = db_reader.GetBlockHashByTransactionHash(utxoTx.hash(), blockhash);
		if(DBStatus::DB_SUCCESS != status)
		{
			ERRORLOG("(HandleGetTxByHashReq) GetBlockHashByTransactionHash failed !");
			TxByHashAck.set_code(status);
			TxByHashAck.set_description("GetBlockHashByTransactionHash failed!");
            net_send_message<GetTxByHashAck>(msgdata, TxByHashAck, net_com::Priority::kPriority_Middle_1);
			return ;
		}

		status = db_reader.GetBlockHeightByBlockHash(blockhash, height);
		if(DBStatus::DB_SUCCESS != status)
		{
			ERRORLOG("(HandleGetTxByHashReq) GetBlockHeightByBlockHash failed !");
			TxByHashAck.set_code(status);
			TxByHashAck.set_description("GetBlockHeightByBlockHash failed!");
            net_send_message<GetTxByHashAck>(msgdata, TxByHashAck, net_com::Priority::kPriority_Middle_1);
			return ;
		}
		pTxItem->set_blockhash(blockhash);
		pTxItem->set_blockheight(height);
       
		std::string blockHeaderStr;
		status = db_reader.GetBlockByBlockHash(blockhash, blockHeaderStr);
		if(DBStatus::DB_SUCCESS != status)
		{
			ERRORLOG("(HandleGetTxByHashReq) GetBlockByBlockHash failed !");
			TxByHashAck.set_code(status);
			TxByHashAck.set_description("GetBlockByBlockHash failed!");
            net_send_message<GetTxByHashAck>(msgdata, TxByHashAck, net_com::Priority::kPriority_Middle_1);
			return ;
		}
		
		CBlock cblock;
    	cblock.ParseFromString(blockHeaderStr);

        CTransaction  tx;
		CTransaction FeeTx;
		CTransaction AwardTx;
		for (auto & t : cblock.txs())
		{
			if (t.vin(0).scriptsig().sign() == std::string(FEE_SIGN_STR))
			{
				FeeTx = t;
			}
			else if (t.vin(0).scriptsig().sign() == std::string(EXTRA_AWARD_SIGN_STR))
			{
				AwardTx = t;
			}
			else
			{
				tx = t;
			}
		}

		// std::vector<std::string> txOwners;
    	// txOwners = TxHelper::GetTxOwner(tx);
    	for (int j = 0; j < FeeTx.vout_size(); ++j)
		{
			CTxout txout = FeeTx.vout(j);
			//if (txOwners.end() == find (txOwners.begin(), txOwners.end(), txout.scriptpubkey() ) )
			{
				pTxItem->add_signer(txout.scriptpubkey());
			}
		}

		int64_t totalFee = 0;
        for (auto & FeeVout : FeeTx.vout())
        {
            totalFee += FeeVout.value();
        }
		pTxItem->set_totalfee(std::to_string(totalFee));
		int64_t totalAward = 0;
        for (auto & AwardVout : AwardTx.vout())
        {
            totalAward += AwardVout.value();
        }
		pTxItem->set_totalaward(std::to_string(totalAward));
        TxByHashAck.set_code(0);
        TxByHashAck.set_description("Tx info success");
        net_send_message<GetTxByHashAck>(msgdata, TxByHashAck, net_com::Priority::kPriority_Middle_1);
    }
    return; 
}

bool GetTxByTxHashFromRocksdb(vector<string>txhash,vector<CTransaction> & outTx)
{
	if(txhash.size() == 0)
	{
		return false;
	}
    DBStatus status;
    DBReader db_reader;
	for(unsigned int i =0; i < txhash.size(); i++)
	{
		std::string strTxRaw;
		status = db_reader.GetTransactionByHash(txhash[i], strTxRaw);
		if(DBStatus::DB_SUCCESS != status)
		{
			ERRORLOG("(GetTxByTxHashFromRocksdb) GetTransactionByHash failed !");
			return false;
		}
		CTransaction utxoTx;
		utxoTx.ParseFromString(strTxRaw);

        if(utxoTx.hash().compare(txhash[i]) == 0)
        {
            outTx.push_back(utxoTx);
            return true;
        }	
    }
	return false;
}

void handleCheckNodeHeightReq(const std::shared_ptr<CheckNodeHeightReq>& req, const MsgData& msgdata)
{
    CheckNodeHeightAck ack;
    ack.set_version(getVersion());

    // 版本判断
    if( 0 != Util::IsVersionCompatible( req->version() ) )
	{
        ack.set_code(-1);
        net_send_message<CheckNodeHeightAck>(msgdata, ack, net_com::Priority::kPriority_Middle_1);
		return ;
	}

    uint64_t top = 0;
    if (DBStatus::DB_SUCCESS != DBReader().GetBlockTop(top) )
    {
        ack.set_code(-3);
		net_send_message<CheckNodeHeightAck>(msgdata, ack, net_com::Priority::kPriority_Middle_1);
		return ;
    }

    ack.set_height(top);

    std::vector<Node> allNodeList;
	if (Singleton<PeerNode>::get_instance()->get_self_node().is_public_node)
	{
		allNodeList = Singleton<PeerNode>::get_instance()->get_nodelist();
	}
	else
	{
		allNodeList = Singleton<NodeCache>::get_instance()->get_nodelist();
	}

    static const int kCheckNodeHeightMax = 100;
    std::vector<Node> nodelist;
    if (allNodeList.size() > kCheckNodeHeightMax)
    {
        std::random_device rd;
        std::default_random_engine rng {rd()};
        std::uniform_int_distribution<int> dist {0, (int)allNodeList.size() - 1};

        std::vector<int> rand;
        for ( size_t i = 0; i != kCheckNodeHeightMax; ++i )
        {
            rand.push_back(dist(rng));
        }

        for (size_t i = 0; i != rand.size(); ++i)
        {
            nodelist.push_back(allNodeList[i]);
        }
    }
    else
    {
        nodelist = allNodeList;
    }

    if(nodelist.size() == 0)
    {
        ack.set_code(-4);
		net_send_message<CheckNodeHeightAck>(msgdata, ack, net_com::Priority::kPriority_Middle_1);
        return;
    }

    int minHeight = top - 5;
    if (minHeight < 0)
    {
        minHeight = 0;
    }

    int maxHeight = top + 5;

    uint32 count = 0;
    for (auto & node : nodelist)
    {
        int height = node.height;
        if (height >= minHeight && height <= maxHeight)
        {
            count++;
        }
    }
    
    ack.set_total(nodelist.size());
    ack.set_percent((double)count / nodelist.size());
    ack.set_code(0);
    
    net_send_message<CheckNodeHeightAck>(msgdata, ack, net_com::Priority::kPriority_Middle_1);
}

