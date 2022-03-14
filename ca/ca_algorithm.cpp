#include "ca/ca_algorithm.h"
#include "ca/ca_transaction.h"
#include <crypto/cryptopp/base64.h>
#include <sys/time.h>
#include "ca/ca_blockcache.h"
#include "MagicSingleton.h"
#include "ca/ca_txhelper.h"

static void Base64Encode(const std::string &bytes, std::string &base64)
{
    base64.clear();
    CryptoPP::Base64Encoder base64_encode(nullptr, false);
    base64_encode.Attach(new CryptoPP::StringSink(base64));
    base64_encode.Put((const CryptoPP::byte *)bytes.data(), bytes.length());
    base64_encode.MessageEnd();
    return;
}

static void Bytes2Hex(const std::string &bytes, std::string &hex, bool to_uppercase)
{
    hex.clear();
    CryptoPP::HexEncoder hex_encoder(nullptr, to_uppercase);
    hex_encoder.Attach(new CryptoPP::StringSink(hex));
    hex_encoder.Put((const CryptoPP::byte *)bytes.data(), bytes.length());
    hex_encoder.MessageEnd();
}

static bool GetPublicKeyFromBytes(const std::string &bytes, CryptoPP::ECDSA<CryptoPP::ECP, CryptoPP::SHA1>::PublicKey &public_key)
{
    try
    {
        char c1 = bytes.at(0);
        char c2 = bytes.at(1);
        if (bytes.empty())
        {
            return false;
        }
        std::string hex;
        Bytes2Hex(bytes, hex, false);
        HexDecoder hex_decoder;
        hex_decoder.Put((CryptoPP::byte *)hex.c_str() + 4, hex.size() - 4);
        hex_decoder.MessageEnd();
        CryptoPP::ECP::Point pt;
        size_t len = hex_decoder.MaxRetrievable();
        if ((size_t)(c1 + c2) == len)
        {
            pt.identity = false;
            pt.x.Decode(hex_decoder, c1);
            pt.y.Decode(hex_decoder, c2);
            public_key.Initialize(CryptoPP::ASN1::secp256r1(), pt);
        }
        CryptoPP::AutoSeededRandomPool prng;
        return public_key.Validate(prng, 3);
    }
    catch (...)
    {
        return false;
    }
}

static bool PublicKeyVerifySign(const CryptoPP::ECDSA<CryptoPP::ECP, CryptoPP::SHA1>::PublicKey &public_key, const std::string &bytes, const std::string &signature)
{
    bool result = false;
    CryptoPP::StringSource(signature + bytes, true,
                           new CryptoPP::SignatureVerificationFilter(CryptoPP::ECDSA<CryptoPP::ECP, CryptoPP::SHA1>::Verifier(public_key),
                                                                     new CryptoPP::ArraySink((CryptoPP::byte *)&result, sizeof(result))));
    return result;
}

static uint64_t GetLocalTimestampUsec()
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return tv.tv_sec * 1000000 + tv.tv_usec;
}

static TransactionType GetTransactionType(const CTransaction &tx)
{
    if (tx.time() == 0 || tx.hash().empty() || tx.vin_size() <= 0 || tx.vout_size() <= 0)
    {
        return kTransactionType_Unknown;
    }
    CTxin txin = tx.vin(0);
    if (txin.scriptsig().sign() == std::string(FEE_SIGN_STR))
    {
        return kTransactionType_Fee;
    }
    else if (txin.scriptsig().sign() == std::string(EXTRA_AWARD_SIGN_STR))
    {
        return kTransactionType_Award;
    }
    return kTransactionType_Tx;
}

int ca_algorithm::GetAbnormalAwardAddrList(uint64_t block_height, std::vector<std::string> &abnormal_addr_list, DBReader *db_reader_ptr)
{
    if (block_height <= 1)
    {
        return 0;
    }
    // https://blog.csdn.net/saltriver/article/details/78847863
    abnormal_addr_list.clear();
    uint64_t highest_height = block_height;
    uint64_t lowest_height = block_height > 1000 ? block_height - 1000 : 1;
    std::vector<std::string> block_hashes;
    DBReader db_reader;
    if (nullptr == db_reader_ptr)
    {
        db_reader_ptr = &db_reader;
    }
    if (DBStatus::DB_SUCCESS != db_reader_ptr->GetBlockHashesByBlockHeight(lowest_height, highest_height, block_hashes))
    {
        return -1;
    }
    std::vector<std::string> blocks;
    if (DBStatus::DB_SUCCESS != db_reader_ptr->GetBlocksByBlockHash(block_hashes, blocks))
    {
        return -2;
    }
    std::unordered_map<std::string, std::pair<uint64_t, uint64_t>> addr_sign_cnt_and_award_amount;

    CBlock block;
    for (auto &block_raw : blocks)
    {
        if (!block.ParseFromString(block_raw))
        {
            return -3;
        }
        for (auto &tx : block.txs())
        {
            if (GetTransactionType(tx) != kTransactionType_Award)
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
        return 0;
    }

    std::vector<uint64_t> sign_cnt;     // 存放签名次数
    std::vector<uint64_t> award_amount; // 存放所有奖励值
    for (auto &item : addr_sign_cnt_and_award_amount)
    {
        sign_cnt.push_back(item.second.first);
        award_amount.push_back(item.second.second);
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

    for (auto &item : addr_sign_cnt_and_award_amount)
    {
        if (item.second.first > sign_cnt_upper_limit_value || item.second.second > award_amount_upper_limit_value)
        {
            abnormal_addr_list.push_back(item.first);
        }
    }
    return 0;
}

int64_t ca_algorithm::GetPledgeTimeByAddr(const std::string &addr, PledgeType pledge_type, DBReader *db_reader_ptr)
{
    if (PledgeType::kPledgeType_Node != pledge_type && PledgeType::kPledgeType_Public != pledge_type)
    {
        return -1;
    }
    DBReader db_reader;
    if (nullptr == db_reader_ptr)
    {
        db_reader_ptr = &db_reader;
    }
    std::vector<std::string> pledge_utxos;
    auto ret = db_reader_ptr->GetPledgeAddressUtxo(addr, pledge_utxos);
    if (DBStatus::DB_NOT_FOUND == ret)
    {
        return 0;
    }
    else if (DBStatus::DB_SUCCESS != ret)
    {
        return -2;
    }

    std::vector<CTransaction> txs;
    CTransaction tx;
    std::string tx_raw;
    std::string tmp_pledge_type;
    for (auto &pledge_utxo : pledge_utxos)
    {
        if (DBStatus::DB_SUCCESS != db_reader_ptr->GetTransactionByHash(pledge_utxo, tx_raw))
        {
            return -3;
        }
        tx.Clear();
        if (!tx.ParseFromString(tx_raw))
        {
            return -4;
        }
        try
        {
            nlohmann::json extra_json = nlohmann::json::parse(tx.extra());
            std::string tx_type = extra_json["TransactionType"].get<std::string>();
            if (TXTYPE_PLEDGE != tx_type)
            {
                continue;
            }
            nlohmann::json tx_info = extra_json["TransactionInfo"].get<nlohmann::json>();
            tmp_pledge_type.clear();
            tx_info["PledgeType"].get_to(tmp_pledge_type);

            if (PledgeType::kPledgeType_Node == pledge_type)
            {
                if (tmp_pledge_type != PLEDGE_NET_LICENCE)
                {
                    continue;
                }
            }
            else
            {
                if (tmp_pledge_type != PLEDGE_PUBLIC_NET_LICENCE)
                {
                    continue;
                }
            }
            txs.push_back(tx);
        }
        catch (...)
        {
            return -5;
        }
    }
    std::sort(txs.begin(), txs.end(),
              [](const CTransaction &tx1, const CTransaction &tx2)
              {
                  return tx1.time() < tx2.time();
              });

    uint64_t total_pledge_amount = 0;
    uint64_t last_time = 0;

    for (auto &tx : txs)
    {
        for (auto &vout : tx.vout())
        {
            if (vout.scriptpubkey() == VIRTUAL_ACCOUNT_PLEDGE)
            {
                total_pledge_amount += vout.value();
                last_time = tx.time();
                break;
            }
        }
        if (PledgeType::kPledgeType_Node == pledge_type)
        {
            if (total_pledge_amount >= g_TxNeedPledgeAmt)
            {
                break;
            }
        }
        else
        {
            if (total_pledge_amount >= g_TxNeedPublicPledgeAmt)
            {
                break;
            }
        }
    }
    if (PledgeType::kPledgeType_Node == pledge_type)
    {
        if (total_pledge_amount < g_TxNeedPledgeAmt)
        {
            return 0;
        }
    }
    else
    {
        if (total_pledge_amount < g_TxNeedPublicPledgeAmt)
        {
            return 0;
        }
    }
    return last_time;
}

int ca_algorithm::GetBlockNumInUnitTime(uint64_t block_height, DBReader *db_reader_ptr, uint64_t unit_time)
{
    if (block_height <= 500)
    {
        return 1;
    }
    block_height -= 500;
    // 计算奖励起始时间
    DBReader db_reader;
    if (nullptr == db_reader_ptr)
    {
        db_reader_ptr = &db_reader;
    }
    std::vector<std::string> block_hashs;
    if (DBStatus::DB_SUCCESS != db_reader_ptr->GetBlockHashsByBlockHeight(block_height, block_hashs))
    {
        return -1;
    }
    std::vector<std::string> blocks;
    if (DBStatus::DB_SUCCESS != db_reader_ptr->GetBlocksByBlockHash(block_hashs, blocks))
    {
        return -2;
    }
    uint64_t start_time = 0;
    CBlock block;
    for (auto &block_raw : blocks)
    {
        if (!block.ParseFromString(block_raw))
        {
            return -3;
        }
        if (start_time < block.time())
        {
            start_time = block.time();
        }
    }
    bool flag = false;
    int block_count = 0;
    for (; block_height >= 0; --block_height)
    {
        block_hashs.clear();
        if (DBStatus::DB_SUCCESS != db_reader_ptr->GetBlockHashsByBlockHeight(block_height, block_hashs))
        {
            return -4;
        }
        blocks.clear();
        if (DBStatus::DB_SUCCESS != db_reader_ptr->GetBlocksByBlockHash(block_hashs, blocks))
        {
            return -5;
        }
        for (auto &block_raw : blocks)
        {
            if (!block.ParseFromString(block_raw))
            {
                return -6;
            }
            if (block.time() > start_time - unit_time)
            {
                if (block.time() <= start_time)
                {
                    ++block_count;
                }
            }
            else
            {
                flag = true;
            }
        }
        if (flag)
        {
            break;
        }
    }
    return block_count;
}

int64_t ca_algorithm::GetBlockTotalAward(uint32_t block_num, DBReader *db_reader_ptr)
{
    if (block_num < 5)
    {
        block_num = 5;
    }
    DBReader db_reader;
    if (nullptr == db_reader_ptr)
    {
        db_reader_ptr = &db_reader;
    }
    uint64_t use_sum_award = 0;
    auto status = db_reader_ptr->GetAwardTotal(use_sum_award);
    if (DBStatus::DB_SUCCESS != status && DBStatus::DB_NOT_FOUND != status)
    {
        return -1;
    }
    // 获得基础区块奖励
    // 70*POWER((1-50%),LOG(blockNum,2))+0.025 计算初始奖励总值的函数
    uint64_t base_amount = (70 * std::pow((1 - 0.5), std::log(block_num) / std::log(2)) + 0.025) * DECIMAL_NUM;
    // 总奖励值
    uint64_t award_total = 80000000lu * DECIMAL_NUM;
    if (use_sum_award < award_total)
    {
        uint64_t half_award_total = award_total / 2;       // 边界值增长幅度
        uint64_t next_half_award_total = half_award_total; // 下一次减半边界值
        for (; use_sum_award > next_half_award_total;)
        {
            base_amount /= 2;                          // 奖励减半
            half_award_total /= 2;                     // 调整边界值增长幅度
            next_half_award_total += half_award_total; // 调整下次减半边界值
        }
    }
    return base_amount;
}

int ca_algorithm::GetAwardAmountAndSignCntByAddr(const std::string &addr, uint64_t block_height, uint64_t pledge_time, uint64_t tx_time, uint64_t &addr_award_total, uint32_t &sign_cnt, DBReader *db_reader_ptr)
{

    addr_award_total = 0;
    sign_cnt = 0;
    if (block_height <= 500)
    {
        return 0;
    }
    block_height -= 500;
    DBReader db_reader;
    if (nullptr == db_reader_ptr)
    {
        db_reader_ptr = &db_reader;
    }
    std::vector<std::string> block_raws;
    {
        // 通过地址获取所有交易
        std::vector<std::string> tx_hashs;
        auto ret = db_reader_ptr->GetAllTransactionByAddreess(addr, tx_hashs);
        if (DBStatus::DB_NOT_FOUND == ret)
        {
            return 0;
        }
        else if (DBStatus::DB_SUCCESS != ret)
        {
            return -1;
        }

        std::set<std::string> block_hashs;
        std::string block_hash;
        for (auto &hash : tx_hashs)
        {
            if (DBStatus::DB_SUCCESS != db_reader_ptr->GetBlockHashByTransactionHash(hash, block_hash))
            {
                return -2;
            }
            block_hashs.insert(block_hash);
        }
        std::vector<std::string> hashs(block_hashs.cbegin(), block_hashs.cend());
        if (DBStatus::DB_SUCCESS != db_reader_ptr->GetBlocksByBlockHash(hashs, block_raws))
        {
            return -3;
        }
    }
    CBlock block;
    for (auto &block_raw : block_raws)
    {
        if (!block.ParseFromString(block_raw))
        {
            return -4;
        }
        if (block.height() > block_height)
        {
            continue;
        }
        for (auto &tx : block.txs())
        {
            if (GetTransactionType(tx) != kTransactionType_Award)
            {
                continue;
            }
            if (tx.time() < pledge_time || tx.time() > tx_time)
            {
                continue;
            }
            for (auto &vout : tx.vout())
            {
                if (vout.scriptpubkey() == addr && vout.value() > 0)
                {
                    addr_award_total += vout.value();
                    ++sign_cnt;
                    break;
                }
            }
        }
    }
    return 0;
}

int ca_algorithm::GetAwardList(uint64_t block_height, uint64_t tx_time, const std::vector<std::string> &award_addrs,
                               std::vector<std::pair<std::string, uint64_t>> &award_list, DBReader *db_reader_ptr)
{
    award_list.clear();
    {
        std::set<std::string> addrs(award_addrs.cbegin(), award_addrs.cend());
        if (addrs.size() < (g_MinNeedVerifyPreHashCount - 2) || addrs.size() != award_addrs.size())
        {
            return -1;
        }
    }
    DBReader db_reader;
    if (nullptr == db_reader_ptr)
    {
        db_reader_ptr = &db_reader;
    }
    int block_num = GetBlockNumInUnitTime(block_height, db_reader_ptr);
    if (block_num < 0)
    {
        ERRORLOG("get block num in unit time ret:{}", block_num);
        return block_num - 100;
    }

    int64_t total_award = GetBlockTotalAward(block_num, db_reader_ptr);
    if (total_award < 0)
    {
        return -2;
    }
    int64_t rate_total_award = total_award;
    uint64_t average_award = total_award / award_addrs.size();

    uint64_t award_total = 0;
    int64_t pledge_time = 0;
    uint32_t sign_cnt = 0;
    uint32_t pledge_time_in_minute = 0;
    uint32_t rate = 0;
    uint32_t sum_rate = 0;
    std::vector<std::pair<std::string, double>> addr_no_zero_rate;

    for (auto addr : award_addrs)
    {
        award_total = 0;
        sign_cnt = 0;
        pledge_time = 0;
        pledge_time_in_minute = 0;
        rate = 0;
        pledge_time = GetPledgeTimeByAddr(addr, PledgeType::kPledgeType_Node, db_reader_ptr);
        if (pledge_time < 0)
        {
            ERRORLOG("get plede num by addr ret:{}", pledge_time);
            return pledge_time - 200;
        }
        if (0 != pledge_time)
        {
            pledge_time_in_minute = (tx_time - pledge_time) / 1000 / 1000 / 60;
        }
        if (pledge_time_in_minute <= 0)
        {
            pledge_time_in_minute = 1;
        }
        auto ret = GetAwardAmountAndSignCntByAddr(addr, block_height, pledge_time, tx_time, award_total, sign_cnt, db_reader_ptr);
        if (0 != ret)
        {
            ERRORLOG("get award_total and sign_cnt by addr error ret:{}", ret);
            return ret - 300;
        }
        if (0 != pledge_time_in_minute && 0 != award_total && 0 != sign_cnt)
        {
            rate = award_total / pledge_time_in_minute / sign_cnt;
        }
        if (0 == rate)
        {
            rate_total_award -= average_award;
            award_list.push_back(std::make_pair(addr, average_award));
        }
        else
        {
            addr_no_zero_rate.push_back(std::make_pair(addr, rate));
            sum_rate += rate;
        }
        TRACELOG("addr:{} : block_height:{} : block_num:{} : award_total:{} : sign_cnt:{} : pledge_time:{} : tx_time:{} : pledge_time_in_minute:{} : rate:{} : sum_rate:{}",
                 addr, block_height, block_num, award_total, sign_cnt, pledge_time, tx_time, pledge_time_in_minute, rate, sum_rate);
    }
    std::sort(addr_no_zero_rate.begin(), addr_no_zero_rate.end(),
              [](const std::pair<std::string, double> &rate1, const std::pair<std::string, double> &rate2)
              {
                  return rate1.second < rate2.second;
              });
    uint64_t award_amount = 0;
    for (int i = 0, j = addr_no_zero_rate.size() - 1; i < addr_no_zero_rate.size(); ++i, --j)
    {
        award_amount = rate_total_award * (addr_no_zero_rate.at(j).second) / sum_rate;
        if (award_amount > rate_total_award)
        {
            return -3;
        }
        award_list.push_back(std::make_pair(addr_no_zero_rate.at(i).first, award_amount));
        TRACELOG("addr_no_zero_rate.at(i).first:{} : award_amount:{} : rate_total_award:{} : addr_no_zero_rate.at(j).second:{} : sum_rate:{}",
            addr_no_zero_rate.at(i).first, award_amount, rate_total_award, addr_no_zero_rate.at(j).second, sum_rate);
    }
    std::sort(award_list.begin(), award_list.end(),
              [](const std::pair<std::string, uint64_t> &award1, const std::pair<std::string, uint64_t> &award2)
              {
                  return award1.second < award2.second;
              });
    uint64_t tmp_total = 0;
    for (auto &item : award_list)
    {
        tmp_total += item.second;
    }
    TRACELOG("total_award - tmp_total:{}", total_award - tmp_total);
    award_list.back().second += (total_award - tmp_total);
    for (auto &item : award_list)
    {
        if (item.second <= 0)
        {
            return -4;
        }
        if (item.second > 14025000)
        {
            return -5;
        }
    }
    return 0;
}

std::string ca_algorithm::CalcTransactionHash(CTransaction tx)
{
    tx.clear_hash();
    tx.clear_signprehash();
    std::string base64;
    Base64Encode(tx.SerializeAsString(), base64);
    return getsha256hash(base64);
}
std::string ca_algorithm::CalcBlockHash(CBlock block)
{
    block.clear_hash();
    return getsha256hash(block.SerializeAsString());
}

int ca_algorithm::MemVerifyTransaction(const CTransaction &tx)
{
    // 1.交易版本号必须为1
    // 2.交易类型是否为正常交易
    // 3.交易hash长度是否为64
    // 4.校验交易hash是否一致
    // 5.扩展字段不能为空
    // 6.签名费大于0.01小于0.1
    // 7.包含打包费字段
    // 8.交易类型只能为交易 质押 解质押, 质押 解质押输出数量只能为2,质押类型只能为入网质押和公网质押
    // 9.vin的个数必须小于等于100个（以后会调整）
    // 10.txowner不允许重复 质押 解质押大小必须为1
    // 11.校验txowner和vin签名者是否一致
    // 12.除了解质押交易中的质押utxo以外，vin不允许重复
    // 13.质押金额与输出要相符
    // 14.输出中除了发起账号外金额必须大于0
    // 15.质押输出必须为 VIRTUAL_ACCOUNT_PLEDGE
    // 16.解质押交易输出只有两个相同的输出，其他交易不能有两个相同的输出,
    // 17.校验vin的sequence
    // 18.校验vin签名
    // 19.校验矿工签名（后面一个矿工的签名对前面的所有信息进行校验，发起矿工要对vin的签名者进行校验）
    // 20.交易的发起方与接收方不允许挖矿签名
    // 21.校验交易标识是否是合法的base58地址

    // 1.
    if (1 != tx.version())
    {
        return -1;
    }
    // 2.
    if (GetTransactionType(tx) != kTransactionType_Tx)
    {
        return -2;
    }
    // 3.
    if (tx.hash().size() != 64)
    {
        return -3;
    }
    // 4.
    if (tx.hash() != ca_algorithm::CalcTransactionHash(tx))
    {
        return -4;
    }
    // 5.
    if (tx.extra().empty())
    {
        return -5;
    }
    bool is_pledge = false;
    uint64_t plede_amount = 0;
    bool is_redeem = false;
    std::string redeem_utxo_hash;
    bool is_deploy = false;
    uint64_t deploy_amount = 0;
    bool is_execute = false;
    uint32_t need_sign_node_num = 0;
    uint64_t sign_fee = 0;
    uint64_t package_fee = 0;
    try
    {
        nlohmann::json extra_json = nlohmann::json::parse(tx.extra());
        std::string tx_type = extra_json["TransactionType"].get<std::string>();
        need_sign_node_num = extra_json["NeedVerifyPreHashCount"].get<uint32_t>();
        sign_fee = extra_json["SignFee"].get<uint64_t>();
        // 6.
        if (sign_fee < g_minSignFee || sign_fee > g_maxSignFee)
        {
            return -6;
        }
        // 7.
        extra_json["PackageFee"].get_to(package_fee);
        // 8.
        if (TXTYPE_PLEDGE == tx_type)
        {
            nlohmann::json tx_info = extra_json["TransactionInfo"].get<nlohmann::json>();
            is_pledge = true;
            tx_info["PledgeAmount"].get_to(plede_amount);
            if (tx.vout_size() != 2)
            {
                return -7;
            }
            std::string pledge_type;
            tx_info["PledgeType"].get_to(pledge_type);
            if (PLEDGE_NET_LICENCE != pledge_type && PLEDGE_PUBLIC_NET_LICENCE != pledge_type)
            {
                return -8;
            }
        }
        else if (TXTYPE_REDEEM == tx_type)
        {
            nlohmann::json tx_info = extra_json["TransactionInfo"].get<nlohmann::json>();
            is_redeem = true;
            redeem_utxo_hash = tx_info["RedeemptionUTXO"].get<std::string>();
            if (tx.vout_size() != 2)
            {
                return -9;
            }
        }
        else if (TXTYPE_TX == tx_type)
        {
           
        }
        else if (TXTYPE_CONTRACT_DEPLOY == tx_type)
        {
            nlohmann::json tx_info = extra_json["TransactionInfo"].get<nlohmann::json>();
            is_deploy = true;
            tx_info["DeployAmount"].get_to(deploy_amount);
        }
        else if (TXTYPE_CONTRACT_EXECUTE == tx_type)
        {
            is_execute = true;
        }
        else
        {
            return -10;
        }
    }
    catch (...)
    {
        return -11;
    }
    // 9.
    if (tx.vin_size() > 100 || tx.vin_size() <= 0)
    {
        return -12;
    }
    std::set<std::string> owner_addrs;
    {
        std::vector<std::string> tmp_addrs;
        StringUtil::SplitString(tx.owner(), tmp_addrs, "_");
        std::set<std::string>(tmp_addrs.cbegin(), tmp_addrs.cend()).swap(owner_addrs);
        // 10.
        if (tmp_addrs.size() != owner_addrs.size())
        {
            return -13;
        }
        if ((is_pledge || is_redeem) && (1 != owner_addrs.size()))
        {
            return -14;
        }
    }
    std::string addr;
    std::set<std::string> vin_addrs;
    std::set<std::string> vin_utxos;
    for (auto &vin : tx.vin())
    {
        addr = GetBase58Addr(vin.scriptsig().pub());
        if (!CheckBase58Addr(addr))
        {
            return -15;
        }
        vin_addrs.insert(addr);
        vin_utxos.insert(vin.prevout().hash() + "_" + std::to_string(vin.prevout().n()));
    }
    // 11.
    {
        std::vector<std::string> v_diff;
        std::set_difference(owner_addrs.cbegin(), owner_addrs.cend(), vin_addrs.cbegin(), vin_addrs.cend(), std::back_inserter(v_diff));
        if (!v_diff.empty())
        {
            return -16;
        }
    }
    // 12.
    if (is_redeem)
    {
        if (vin_utxos.size() != tx.vin_size() && (vin_utxos.size() != (tx.vin_size() - 1)))
        {
            return -17;
        }
    }
    else
    {
        if (vin_utxos.size() != tx.vin_size())
        {
            return -18;
        }
    }
    std::set<std::string> vout_addrs;
    bool pledge_vout_flag = false;
    int i = 0;
    for (auto &vout : tx.vout())
    {
        if (is_pledge)
        {
            // 13.
            if (VIRTUAL_ACCOUNT_PLEDGE == vout.scriptpubkey() && 0 == i)
            {
                if (plede_amount != vout.value())
                {
                    return -19;
                }
                pledge_vout_flag = true;
                ++i;
            }
            else if (!CheckBase58Addr(vout.scriptpubkey()))
            {
                return -20;
            }
        }
        else if (is_deploy)
        {
            if (VIRTUAL_ACCOUNT_DEPLOY == vout.scriptpubkey() && 0 == i)
            {

            }
        }
        else if (is_execute)
        {
           
        }
        else
        {
            if (!CheckBase58Addr(vout.scriptpubkey()))
            {
                return -21;
            }
        }
        // 14.
        if (vin_addrs.cend() == std::find(vin_addrs.cbegin(), vin_addrs.cend(), vout.scriptpubkey()))
        {
            if (vout.value() <= 0)
            {
                return -22;
            }
        }
        else
        {
            if (vout.value() < 0)
            {
                return -23;
            }
        }
        vout_addrs.insert(vout.scriptpubkey());
    }

    // 不允许多对多交易
    if (vin_addrs.size() > 1)
    {
        if (2 != vout_addrs.size())
        {
            return -99;
        }
    }

    // 15.
    if (is_pledge && (!pledge_vout_flag))
    {
        return -24;
    }
    // 16.
    if (is_redeem)
    {
        if (vout_addrs.size() != (tx.vout_size() - 1))
        {
            return -25;
        }
    }
    else if(is_execute)
    {

    }
    else
    {
        if (vout_addrs.size() != tx.vout_size())
        {
            return -26;
        }
    }
    // 17.
    for (int i = 0; i < tx.vin_size(); i++)
    {
        if (i != tx.vin(i).sequence())
        {
            return -27;
        }
    }
    // 18.
    std::string vin_verify_str;
    {
        CTransaction tmp_tx = tx;
        for (int i = 0; i != tmp_tx.vin_size(); ++i)
        {
            CTxin *txin = tmp_tx.mutable_vin(i);
            txin->clear_scriptsig();
        }
        tmp_tx.clear_signprehash();
        tmp_tx.clear_hash();
        std::string base64;
        Base64Encode(tmp_tx.SerializeAsString(), base64);
        vin_verify_str = getsha256hash(base64);
    }
    for (auto &vin : tx.vin())
    {
        CryptoPP::ECDSA<CryptoPP::ECP, CryptoPP::SHA1>::PublicKey public_key;
        if (!GetPublicKeyFromBytes(vin.scriptsig().pub(), public_key))
        {
            return -28;
        }

        if (!PublicKeyVerifySign(public_key, vin_verify_str, vin.scriptsig().sign()))
        {
            return -29;
        }
    }
    // 19.
    CTransaction sign_tx = tx;
    std::string base64;
    for (auto &tx_sign_pre_hash1 : tx.signprehash())
    {
        sign_tx.clear_signprehash();
        for (auto &tx_sign_pre_hash2 : tx.signprehash())
        {
            if (tx_sign_pre_hash1.pub() == tx_sign_pre_hash2.pub())
            {
                break;
            }

            auto sign_pre_hash = sign_tx.add_signprehash();
            *sign_pre_hash = tx_sign_pre_hash2;
        }
        CryptoPP::ECDSA<CryptoPP::ECP, CryptoPP::SHA1>::PublicKey public_key;
        if (!GetPublicKeyFromBytes(tx_sign_pre_hash1.pub(), public_key))
        {
            return -30;
        }
        base64.clear();
        Base64Encode(sign_tx.SerializeAsString(), base64);
        if (!PublicKeyVerifySign(public_key, getsha256hash(base64), tx_sign_pre_hash1.sign()))
        {
            return -31;
        }
    }
    // 20.
    {
        std::vector<std::string> v_union;
        std::set_union(vin_addrs.cbegin(), vin_addrs.cend(), vout_addrs.cbegin(), vout_addrs.cend(), std::back_inserter(v_union));
        std::vector<std::string> v_sign_addr;
        std::string sign_addr;
        for (int i = 1; i < tx.signprehash_size(); ++i)
        {
            auto &tx_sign_pre_hash = tx.signprehash(i);
            sign_addr = GetBase58Addr(tx_sign_pre_hash.pub());
            if (!CheckBase58Addr(sign_addr))
            {
                return -32;
            }
            v_sign_addr.push_back(sign_addr);
        }
        std::vector<std::string> v_intersection;
        std::sort(v_union.begin(), v_union.end());
        std::sort(v_sign_addr.begin(), v_sign_addr.end());
        std::set_intersection(v_union.cbegin(), v_union.cend(), v_sign_addr.cbegin(), v_sign_addr.cend(), std::back_inserter(v_intersection));
        if (!v_intersection.empty())
        {
            return -33;
        }
    }
    // 21.
    if (!CheckBase58Addr(tx.identity()))
    {
        return -34;
    }
    return 0;
}

int ca_algorithm::VerifyTransaction(const CTransaction &tx, uint64_t tx_height, bool verify_abnormal, DBReader *db_reader_ptr)
{
    // 1.校验所使用的utxo是否存在并使用
    // 2.前置utxo数组下标不允许出错
    // 3.交易的金额必须一致（输入 = 输出 + 打包费 + 手续费）
    // 4.解质押时间为质押时间30天后
    // 5.质押金额与解质押金额要相符
    // 6.50高度以内的质押交易与创世账号创建的交易可以任意发起
    // 7.第一个签名账号如果不是交易所属账号则必须质押，第二个签名账号必须为公网质押类型，并且质押数必须超过5000
    // 8.校验签名账号是否质押并超过500
    // 9.校验签名账号是否为异常账号

    auto ret = MemVerifyTransaction(tx);
    if (0 != ret)
    {
        ERRORLOG("MemVerifyTransaction error return:{}", ret);
        return ret - 1000;
    }
    DBReader db_reader;
    if (nullptr == db_reader_ptr)
    {
        db_reader_ptr = &db_reader;
    }
    nlohmann::json extra_json = nlohmann::json::parse(tx.extra());
    uint64_t sign_fee = extra_json["SignFee"].get<uint64_t>();
    uint64_t package_fee = extra_json["PackageFee"].get<uint64_t>();
    uint32_t need_sign_node_num = extra_json["NeedVerifyPreHashCount"].get<uint32_t>();
    std::string tx_type = extra_json["TransactionType"].get<std::string>();

    bool is_redeem = false;
    std::string redeem_utxo_hash;
    if (TXTYPE_REDEEM == tx_type)
    {
        nlohmann::json tx_info = extra_json["TransactionInfo"].get<nlohmann::json>();
        is_redeem = true;
        redeem_utxo_hash = tx_info["RedeemptionUTXO"].get<std::string>();
    }
    uint64_t vin_amount = 0;
    {
        std::string addr;
        std::string utxo;
        uint32_t index;
        std::vector<std::string> utxo_hashs;
        std::string tx_raw;
        CTransaction utxo_tx;
        bool flag = true;
        for (auto &vin : tx.vin())
        {
            addr = GetBase58Addr(vin.scriptsig().pub());
            utxo = vin.prevout().hash();
            index = vin.prevout().n();
            // 1.
            if (DBStatus::DB_SUCCESS != db_reader_ptr->GetUtxoHashsByAddress(addr, utxo_hashs))
            {
                return -1;
            }
            if ((TXTYPE_REDEEM == tx_type) && redeem_utxo_hash == utxo && 0 == index)
            {
                if (DBStatus::DB_SUCCESS != db_reader_ptr->GetPledgeAddressUtxo(addr, utxo_hashs))
                {
                    return -2;
                }
                if (utxo_hashs.cend() == std::find(utxo_hashs.cbegin(), utxo_hashs.cend(), utxo))
                {
                    return -3;
                }
            }
            else
            {
                if (utxo_hashs.cend() == std::find(utxo_hashs.cbegin(), utxo_hashs.cend(), utxo))
                {
                    return -4;
                }
            }
            // 2.
            if (DBStatus::DB_SUCCESS != db_reader_ptr->GetTransactionByHash(utxo, tx_raw))
            {
                return -5;
            }
            if (!utxo_tx.ParseFromString(tx_raw))
            {
                return -6;
            }
            if (index >= utxo_tx.vout_size())
            {
                return -7;
            }
            std::string utxo_tx_type;
            TransactionType transaction_type = GetTransactionType(utxo_tx);
            try
            {
                nlohmann::json extra_json = nlohmann::json::parse(utxo_tx.extra());
                utxo_tx_type = extra_json["TransactionType"].get<std::string>();
            }
            catch (...)
            {
                ;
            }
            if (flag && TXTYPE_REDEEM == tx_type && TXTYPE_PLEDGE == utxo_tx_type && utxo == redeem_utxo_hash && 0 == index && TransactionType::kTransactionType_Tx == transaction_type)
            {
                if (utxo_tx.vout(index).scriptpubkey() != VIRTUAL_ACCOUNT_PLEDGE)
                {
                    return -8;
                }
                flag = false;
            }
            else
            {
                if (utxo_tx.vout(index).scriptpubkey() != addr)
                {
                    return -9;
                }
            }
            if (TXTYPE_REDEEM == utxo_tx_type && TransactionType::kTransactionType_Tx == transaction_type)
            {
                for (auto &vout : utxo_tx.vout())
                {
                    vin_amount += vout.value();
                }
            }
            else
            {
                vin_amount += utxo_tx.vout(index).value();
            }
        }
    }

    uint64_t vout_amount = 0;
    for (auto &vout : tx.vout())
    {
        vout_amount += vout.value();
    }
    // 3.
    if ((vout_amount + package_fee + sign_fee * (need_sign_node_num - 1)) != vin_amount)
    {
        return -10;
    }
    std::string redeem_utxo_raw;
    CTransaction redeem_utxo;
    if (TXTYPE_REDEEM == tx_type)
    {
        nlohmann::json tx_info = extra_json["TransactionInfo"].get<nlohmann::json>();
        std::string redeem_utxo_hash = tx_info["RedeemptionUTXO"].get<std::string>();
        CTransaction utxo_tx;
        if (DBStatus::DB_SUCCESS != db_reader_ptr->GetTransactionByHash(redeem_utxo_hash, redeem_utxo_raw))
        {
            return -11;
        }
        if (!redeem_utxo.ParseFromString(redeem_utxo_raw))
        {
            return -12;
        }
        // 4.
        uint64_t now = GetLocalTimestampUsec();
        if (now < redeem_utxo.time())
        {
            return -13;
        }
#ifndef DEVCHAIN
        uint64_t days30 = 30ll * 24 * 60 * 60 * 1000 * 1000;
#else
        uint64_t days30 = 1 * 60 * 1000 * 1000;
#endif
        if ((tx.time() - redeem_utxo.time()) < (days30))
        {
            return -14;
        }
        uint64_t pledge_amount = 0;
        if (redeem_utxo.vin_size() <= 0)
        {
            return -15;
        }
        std::string pledge_addr = GetBase58Addr(redeem_utxo.vin(0).scriptsig().pub());
        for (auto &vout : redeem_utxo.vout())
        {
            if (vout.scriptpubkey() == VIRTUAL_ACCOUNT_PLEDGE)
            {
                pledge_amount = vout.value();
                break;
            }
        }
        if (0 == pledge_amount)
        {
            return -16;
        }
        // 5.
        if (tx.vout(0).scriptpubkey() != pledge_addr)
        {
            return -17;
        }
        if (tx.vout(0).value() != pledge_amount)
        {
            return -18;
        }
    }

    std::vector<std::string> award_addrs;
    std::string award_addr;
    std::vector<std::string> pledge_addrs;
    auto status = db_reader_ptr->GetPledgeAddress(pledge_addrs);
    if (DBStatus::DB_SUCCESS != status && DBStatus::DB_NOT_FOUND != status)
    {
        return -19;
    }
    // 6.
    if (tx_height <= g_minUnpledgeHeight && (tx.owner() == g_InitAccount || TXTYPE_PLEDGE == tx_type))
    {
        int i = 0;
        for (auto &tx_sign_pre_hash : tx.signprehash())
        {
            award_addr = GetBase58Addr(tx_sign_pre_hash.pub());
            if (!CheckBase58Addr(award_addr))
            {
                return -20;
            }
            if (i > 1)
            {
                award_addrs.push_back(award_addr);
            }
            ++i;
        }
    }
    else
    {
        // 7.
        if (tx.signprehash_size() > 0 && package_fee > 0)
        {
            auto sign_pre_hash = tx.signprehash(0);
            award_addr = GetBase58Addr(sign_pre_hash.pub());
            if (!CheckBase58Addr(award_addr))
            {
                return -21;
            }
            std::vector<std::string> tmp_owner_addrs;
            StringUtil::SplitString(tx.owner(), tmp_owner_addrs, "_");
            if (tmp_owner_addrs.cend() == std::find(tmp_owner_addrs.cbegin(), tmp_owner_addrs.cend(), award_addr))
            {
                if (0 >= GetPledgeTimeByAddr(award_addr, PledgeType::kPledgeType_Public, db_reader_ptr))
                {
                    return -22;
                }
            }
        }
        if (tx.signprehash_size() > 1)
        {
            auto sign_pre_hash = tx.signprehash(1);
            award_addr = GetBase58Addr(sign_pre_hash.pub());
            if (!CheckBase58Addr(award_addr))
            {
                return -23;
            }
            int64_t pledge_time = GetPledgeTimeByAddr(award_addr, PledgeType::kPledgeType_Public, db_reader_ptr);
            if (pledge_time <= 0)
            {
                return -24;
            }
        }

        // 8.
        for (int i = 2; i < tx.signprehash_size(); ++i)
        {
            auto &tx_sign_pre_hash = tx.signprehash(i);
            award_addr = GetBase58Addr(tx_sign_pre_hash.pub());
            if (!CheckBase58Addr(award_addr))
            {
                return -25;
            }
            award_addrs.push_back(award_addr);
            if (0 >= GetPledgeTimeByAddr(award_addr, PledgeType::kPledgeType_Node, db_reader_ptr))
            {
                return -26;
            }
        }
    }
    // 9.
    if(verify_abnormal)
    {
        std::vector<std::string> abnormal_addr_list;
        ret = GetAbnormalAwardAddrList(tx_height, abnormal_addr_list, db_reader_ptr);
        if (0 != ret)
        {
            return ret - 2000;
        }
        {
            std::vector<std::string> v_intersection;
            std::sort(award_addrs.begin(), award_addrs.end());
            std::sort(abnormal_addr_list.begin(), abnormal_addr_list.end());
            std::set_intersection(award_addrs.cbegin(), award_addrs.cend(), abnormal_addr_list.cbegin(), abnormal_addr_list.cend(), std::back_inserter(v_intersection));
            if (!v_intersection.empty())
            {
                return -27;
            }
        }
    }
    return 0;
}

int ca_algorithm::MemVerifyBlock(const CBlock &block)
{
    // 1.区块版本号必须为1
    // 2.区块序列化后的大小小于1000000
    // 3.验证区块hash是否相同
    // 4.验证merkleroot是否相同
    // 5.区块中交易的数量必须大于0并且为3（正常交易，签名交易，挖矿交易）的倍数
    // 6.校验区块中交易必须为3个一组
    // 7.交易版本号必须为1
    // 8.签名交易输入为正常交易的hash
    // 9.挖矿交易的输入为空
    // 10.校验3种交易的签名地址是否一致
    // 11.签名个数必须大于等于6小于等于15，并且与扩展字段相同
    // 12.验证手续费和打包费是否匹配

    // 1.
    if (1 != block.version())
    {
        return -1;
    }
    // 2.
    if (block.SerializeAsString().length() > 1000000)
    {
        return -2;
    }
    // 3.
    if (block.hash() != CalcBlockHash(block))
    {
        return -3;
    }
    // 4.
    if (block.merkleroot() != CalcBlockHeaderMerkle(block))
    {
        return -4;
    }
    // 5.
    if (block.txs_size() < 3)
    {
        return -5;
    }
    //if ((block.txs_size() / 3 * 3) != block.txs_size())
    if(block.txs_size() != 3)
    {
        return -6;
    }
    // 6.
    for (int i = 0; i < block.txs_size(); i += 3)
    {
        auto &tx = block.txs(i);
        if (GetTransactionType(tx) != kTransactionType_Tx)
        {
            return -7;
        }
        auto &sign_tx = block.txs(i + 1);
        if (GetTransactionType(sign_tx) != kTransactionType_Fee)
        {
            return -8;
        }
        auto &award_tx = block.txs(i + 2);
        if (GetTransactionType(award_tx) != kTransactionType_Award)
        {
            return -9;
        }
        // 7.
        if (1 != tx.version() || 1 != sign_tx.version() || 1 != award_tx.version())
        {
            return -10;
        }
        // 8.
        if (sign_tx.vin_size() != 1)
        {
            return -11;
        }
        if (sign_tx.vin(0).prevout().hash() != tx.hash())
        {
            return -12;
        }
        if (sign_tx.vin(0).prevout().n() != 0)
        {
            return -13;
        }
        if (sign_tx.vin(0).sequence() != 0)
        {
            return -14;
        }
        // 9.
        if (award_tx.vin_size() != 1)
        {
            return -15;
        }
        if (!award_tx.vin(0).prevout().hash().empty())
        {
            return -16;
        }
        if (award_tx.vin(0).prevout().n() != 0)
        {
            return -17;
        }
        if (award_tx.vin(0).sequence() != 0)
        {
            return -18;
        }

        std::string addr;
        std::vector<std::string> tx_sign_addr;
        for (auto &tx_sign_pre_hash : tx.signprehash())
        {
            addr = GetBase58Addr(tx_sign_pre_hash.pub());
            if (!CheckBase58Addr(addr))
            {
                return -19;
            }
            tx_sign_addr.push_back(addr);
        }
        std::vector<std::string> sign_tx_sign_addr;
        for (auto &vout : sign_tx.vout())
        {
            if (!CheckBase58Addr(vout.scriptpubkey()))
            {
                return -20;
            }
            sign_tx_sign_addr.push_back(vout.scriptpubkey());
        }
        std::vector<std::string> award_tx_sign_addr;
        for (auto &vout : award_tx.vout())
        {
            if (!CheckBase58Addr(vout.scriptpubkey()))
            {
                return -21;
            }
            award_tx_sign_addr.push_back(vout.scriptpubkey());
        }
        // 10.
        {
            std::vector<std::string> v_diff;
            std::sort(tx_sign_addr.begin(), tx_sign_addr.end());
            std::sort(sign_tx_sign_addr.begin(), sign_tx_sign_addr.end());
            std::set_difference(tx_sign_addr.begin(), tx_sign_addr.end(), sign_tx_sign_addr.begin(), sign_tx_sign_addr.end(), std::back_inserter(v_diff));
            if (!v_diff.empty())
            {
                return -22;
            }
            std::sort(award_tx_sign_addr.begin(), award_tx_sign_addr.end());
            std::set_difference(sign_tx_sign_addr.begin(), sign_tx_sign_addr.end(), award_tx_sign_addr.begin(), award_tx_sign_addr.end(), std::back_inserter(v_diff));
            if (!v_diff.empty())
            {
                return -23;
            }
        }
        try
        {
            nlohmann::json extra_json = nlohmann::json::parse(tx.extra());
            uint64_t package_fee = extra_json["PackageFee"].get<uint64_t>();
            uint64_t sign_fee = extra_json["SignFee"].get<uint64_t>();
            uint32_t need_sign_node_num = extra_json["NeedVerifyPreHashCount"].get<uint32_t>();
            // 11.
            if (need_sign_node_num != tx_sign_addr.size() || need_sign_node_num < g_MinNeedVerifyPreHashCount || need_sign_node_num > g_MaxNeedVerifyPreHashCount)
            {
                return -24;
            }
            if (sign_tx.vout_size() <= 0)
            {
                return -25;
            }
            // 12.
            if (sign_tx.vout(0).value() != package_fee)
            {
                return -26;
            }
            for (int i = 1; i < sign_tx.vout_size(); ++i)
            {
                auto &vout = sign_tx.vout(i);
                if (vout.value() != sign_fee)
                {
                    return -27;
                }
            }
        }
        catch (...)
        {
            return -28;
        }
    }
    return 0;
}

int ca_algorithm::VerifyBlock(const CBlock &block, bool verify_abnormal, DBReader *db_reader_ptr)
{
    // 1.验证区块在本地是否存在
    // 2.验证前置区块是否存在
    // 3.区块高度必须为前置区块的高度加一
    // 4.验证区块时间是否大于10个高度之前的的最大区块时间
    // 5.验证交易时间是否大于10个高度之前的的最大区块时间
    // 6.签名顺序与奖励交易的前两个输出顺序相同
    // 7.计算奖励是否符合规则（第一个签名节点与第二个签名节点不得奖励）
    // 8.签名顺序与签名交易的输出顺序相同
    DBReader db_reader;
    if (nullptr == db_reader_ptr)
    {
        db_reader_ptr = &db_reader;
    }
    {
        // 1.
        std::string block_raw;
        auto status = db_reader_ptr->GetBlockByBlockHash(block.hash(), block_raw);
        if (DBStatus::DB_SUCCESS == status)
        {
            return 0;
        }
        if (DBStatus::DB_NOT_FOUND != status)
        {
            return -1;
        }

        // 2.
        block_raw.clear();
        if (DBStatus::DB_SUCCESS != db_reader_ptr->GetBlockByBlockHash(block.prevhash(), block_raw))
        {
            return -2;
        }
        CBlock pre_block;
        if (!pre_block.ParseFromString(block_raw))
        {
            return -3;
        }
        // 3.
        if (block.height() - pre_block.height() != 1)
        {
            return -4;
        }
    }
    auto ret = MemVerifyBlock(block);
    if (0 != ret)
    {
        ERRORLOG("MemVerifyBlock error return:{}", ret);
        return ret - 10000;
    }
    // 4.
    uint64_t start_time = 0;
    uint64_t end_time = GetLocalTimestampUsec() + 10 * 60 * 1000 * 1000;
    {
        uint64_t block_height = 0;
        if (block.height() > 10)
        {
            block_height = block.height() - 10;
        }
        std::vector<std::string> block_hashs;
        if (DBStatus::DB_SUCCESS != db_reader_ptr->GetBlockHashsByBlockHeight(block_height, block_hashs))
        {
            return -5;
        }
        std::vector<std::string> blocks;
        if (DBStatus::DB_SUCCESS != db_reader_ptr->GetBlocksByBlockHash(block_hashs, blocks))
        {
            return -6;
        }
        CBlock block;
        for (auto &block_raw : blocks)
        {
            if (!block.ParseFromString(block_raw))
            {
                return -7;
            }
            if (start_time < block.time())
            {
                start_time = block.time();
            }
        }
    }
    if (block.time() > end_time || block.time() < start_time)
    {
        return -8;
    }
    // 5.
    std::vector<std::string> award_addrs;
    std::vector<std::pair<std::string, uint64_t>> award_list;
    for (int i = 0; i < block.txs_size(); i += 3)
    {
        auto &tx = block.txs(i);
        if (GetTransactionType(tx) != kTransactionType_Tx)
        {
            return -9;
        }
        ret = VerifyTransaction(tx, block.height(), verify_abnormal, db_reader_ptr);
        if (0 != ret)
        {
            return ret - 20000;
        }
        auto &sign_tx = block.txs(i + 1);
        if (GetTransactionType(sign_tx) != kTransactionType_Fee)
        {
            return -10;
        }
        auto &award_tx = block.txs(i + 2);
        if (GetTransactionType(award_tx) != kTransactionType_Award)
        {
            return -11;
        }
        for (int i = 2; i < tx.signprehash_size(); ++i)
        {
            auto &pub = tx.signprehash(i).pub();
            award_addrs.push_back(GetBase58Addr(tx.signprehash(i).pub()));
        }
        ret = GetAwardList(block.height(), tx.time(), award_addrs, award_list, db_reader_ptr);
        if (0 != ret)
        {
            return ret - 30000;
        }
        std::map<std::string, uint64_t> awards(award_list.cbegin(), award_list.cend());
        // 6.
        std::string addr;
        for (int i = 0; i < 2; ++i)
        {
            auto &sign_pre_hash = tx.signprehash(i);
            addr = GetBase58Addr(sign_pre_hash.pub());
            if (award_tx.vout(i).scriptpubkey() != addr)
            {
                return -12;
            }
        }
        // 7.
        uint64_t tmp_award_total = 0;
        for (int i = 2; i < award_tx.vout_size(); ++i)
        {
            auto &vout = award_tx.vout(i);
            auto it = awards.find(vout.scriptpubkey());
            if (awards.cend() == it)
            {
                return -13;
            }
            if (vout.value() != it->second)
            {
                return -14;
            }
        }
        // 8.
        if (tx.signprehash_size() != sign_tx.vout_size())
        {
            return -15;
        }
        for (int i = 0; i < tx.signprehash_size(); ++i)
        {
            auto &sign_pre_hash = tx.signprehash(i);
            addr = GetBase58Addr(sign_pre_hash.pub());
            if (sign_tx.vout(i).scriptpubkey() != addr)
            {
                return -16;
            }
        }
    }
    return 0;
}

//保存区块
int ca_algorithm::SaveBlock(DBReadWriter &db_writer, const CBlock &block)
{
    // 1.判断本地是否存在区块
    // 2.更新节点高度以及bestChain
    // 3.添加块hash对应的高度
    // 4.更新高度上的块hash
    // 5.添加区块hash对应的区块头
    // 6.添加区块hash对应的区块数据
    // 7.质押交易更新质押地址以及质押地址的utxo
    // 8.解质押交易更新质押地址以及质押地址的utxo
    // 9.添加使用的质押utxo对应的解质押交易hash
    // 10.所有交易更新删除使用的utxo以及减去交易地址使用的utxo的余额
    // 11.所有交易添加使用的utxo对应的交易hash
    // 12.所有交易添加新的utxo以及增加交易地址使用的utxo的余额
    // 13.添加交易hash对应的交易体数据
    // 14.添加交易hash对应的区块hash
    // 15.更新交易地址的交易hash
    // 16.更新账号的额外奖励总值
    // 17.更新账号的签名次数
    // 18.更新额外奖励总数
    // 19.交易统计增加 TxCount GasCount AwardCount

    // 1.
    std::string block_raw;
    auto ret = db_writer.GetBlockByBlockHash(block.hash(), block_raw);
    if (DBStatus::DB_SUCCESS == ret)
    {
        return 0;
    }
    else if (DBStatus::DB_NOT_FOUND != ret)
    {
        return -1;
    }
    // 2.
    uint64_t node_height = 0;
    if (DBStatus::DB_SUCCESS != db_writer.GetBlockTop(node_height))
    {
        return -2;
    }
    bool is_best_chain_hash = false;
    if (block.height() > node_height)
    {
        is_best_chain_hash = true;
        if (DBStatus::DB_SUCCESS != db_writer.SetBlockTop(block.height()))
        {
            return -3;
        }
        if (DBStatus::DB_SUCCESS != db_writer.SetBestChainHash(block.hash()))
        {
            return -4;
        }
    }
    else
    {
        std::string main_hash;
        if (block.height() == node_height)
        {
            if (DBStatus::DB_SUCCESS != db_writer.GetBestChainHash(main_hash))
            {
                return -5;
            }
        }
        else
        {
            if (DBStatus::DB_SUCCESS != db_writer.GetBlockHashByBlockHeight(block.height(), main_hash))
            {
                return -6;
            }
        }
        std::string main_block_raw;
        if (DBStatus::DB_SUCCESS != db_writer.GetBlockByBlockHash(main_hash, main_block_raw))
        {
            return -7;
        }
        CBlock best_chain_block;
        if (!best_chain_block.ParseFromString(main_block_raw))
        {
            return -8;
        }

        if (block.time() < best_chain_block.time())
        {
            is_best_chain_hash = true;
            if (block.height() == node_height)
            {
                if (DBStatus::DB_SUCCESS != db_writer.SetBestChainHash(block.hash()))
                {
                    return -9;
                }
            }
        }
    }
    // 3.
    if (DBStatus::DB_SUCCESS != db_writer.SetBlockHeightByBlockHash(block.hash(), block.height()))
    {
        return -10;
    }
    // 4.
    if (DBStatus::DB_SUCCESS != db_writer.SetBlockHashByBlockHeight(block.height(), block.hash(), is_best_chain_hash))
    {
        return -11;
    }

    // 5.
    {
        CBlockHeader block_header;
        block_header.set_hash(block.hash());
        block_header.set_prevhash(block.prevhash());
        block_header.set_time(block.time());
        block_header.set_height(block.height());
        if (DBStatus::DB_SUCCESS != db_writer.SetBlockHeaderByBlockHash(block.hash(), block_header.SerializeAsString()))
        {
            return -12;
        }
    }
    // 6.
    if (DBStatus::DB_SUCCESS != db_writer.SetBlockByBlockHash(block.hash(), block.SerializeAsString()))
    {
        return -13;
    }
    std::set<std::string> block_addr;
    std::set<std::string> all_addr;
    for (auto &tx : block.txs())
    {
        auto transaction_type = CheckTransactionType(tx);
        block_addr.insert(all_addr.cbegin(), all_addr.cend());
        all_addr.clear();
        if (kTransactionType_Tx == transaction_type)
        {
            bool is_redeem = false;
            std::string redeem_utxo_hash;
            try
            {
                nlohmann::json extra_json = nlohmann::json::parse(tx.extra());
                std::string tx_type = extra_json["TransactionType"].get<std::string>();

                // 7.
                if (TXTYPE_PLEDGE == tx_type)
                {
                    for (auto &vin : tx.vin())
                    {
                        std::string addr = GetBase58Addr(vin.scriptsig().pub());
                        if (!CheckBase58Addr(addr))
                        {
                            return -14;
                        }
                        if (DBStatus::DB_SUCCESS != db_writer.SetPledgeAddressUtxo(addr, tx.hash()))
                        {
                            return -15;
                        }
                        std::vector<std::string> pledge_addrs;
                        ret = db_writer.GetPledgeAddress(pledge_addrs);
                        if (DBStatus::DB_SUCCESS != ret && DBStatus::DB_NOT_FOUND != ret)
                        {
                            return -16;
                        }
                        if (pledge_addrs.cend() == std::find(pledge_addrs.cbegin(), pledge_addrs.cend(), addr))
                        {
                            if (DBStatus::DB_SUCCESS != db_writer.SetPledgeAddresses(addr))
                            {
                                return -17;
                            }
                        }
                        break;
                    }
                }
                // 8.
                else if (TXTYPE_REDEEM == tx_type)
                {
                    bool flag = false;
                    nlohmann::json tx_info = extra_json["TransactionInfo"].get<nlohmann::json>();
                    is_redeem = true;
                    redeem_utxo_hash = tx_info["RedeemptionUTXO"].get<std::string>();
                    for (auto &vin : tx.vin())
                    {
                        if (redeem_utxo_hash != vin.prevout().hash())
                        {
                            continue;
                        }
                        flag = true;
                        std::string addr = GetBase58Addr(vin.scriptsig().pub());
                        if (!CheckBase58Addr(addr))
                        {
                            return -18;
                        }
                        if (DBStatus::DB_SUCCESS != db_writer.RemovePledgeAddressUtxo(addr, redeem_utxo_hash))
                        {
                            return -19;
                        }
                        if (DBStatus::DB_SUCCESS != db_writer.SetTxHashByUsePledeUtxoHash(redeem_utxo_hash, tx.hash()))
                        {
                            return -20;
                        }
                        std::vector<std::string> pledge_utxo_hashs;
                        ret = db_writer.GetPledgeAddressUtxo(addr, pledge_utxo_hashs);
                        if (DBStatus::DB_NOT_FOUND == ret || pledge_utxo_hashs.empty())
                        {
                            if (DBStatus::DB_SUCCESS != db_writer.RemovePledgeAddresses(addr))
                            {
                                return -21;
                            }
                        }
                        else if (DBStatus::DB_SUCCESS != ret)
                        {
                            return -22;
                        }
                        break;
                    }
                    if (!flag)
                    {
                        return -23;
                    }
                }
            }
            catch (...)
            {
                return -24;
            }
            std::string addr;
            std::string utxo_hash;
            CTransaction utxo_tx;
            std::string utxo_tx_raw;
            std::string utxo_n;
            std::vector<std::string> vin_hashs;
            for (auto &vin : tx.vin())
            {
                addr = GetBase58Addr(vin.scriptsig().pub());
                all_addr.insert(addr);
                utxo_hash = vin.prevout().hash();
                utxo_n = utxo_hash + "_" + std::to_string(vin.prevout().n());
                if (is_redeem && redeem_utxo_hash == utxo_hash && 0 == vin.prevout().n())
                {
                    continue;
                }
                if (vin_hashs.cend() != std::find(vin_hashs.cbegin(), vin_hashs.cend(), utxo_n))
                {
                    continue;
                }
                vin_hashs.push_back(utxo_n);
                // 10.
                if (DBStatus::DB_SUCCESS != db_writer.RemoveUtxoHashsByAddress(addr, utxo_hash))
                {
                    return -25;
                }
                // vin减utxo
                if (DBStatus::DB_SUCCESS != db_writer.GetTransactionByHash(utxo_hash, utxo_tx_raw))
                {
                    return -26;
                }
                if (!utxo_tx.ParseFromString(utxo_tx_raw))
                {
                    return -27;
                }

                uint64_t amount = 0;
                for (int j = 0; j < utxo_tx.vout_size(); j++)
                {
                    CTxout txout = utxo_tx.vout(j);
                    if (txout.scriptpubkey() == addr)
                    {
                        amount += txout.value();
                    }
                }
                int64_t balance = 0;
                ret = db_writer.GetBalanceByAddress(addr, balance);
                if (DBStatus::DB_SUCCESS != ret)
                {
                    if (DBStatus::DB_NOT_FOUND != ret)
                    {
                        return -28;
                    }
                    else
                    {
                        balance = 0;
                    }
                }
                balance -= amount;
                if (balance < 0)
                {
                    ERRORLOG("SaveBlock vin height:{} hash:{} addr:{} balance:{}", block.height(), block.hash(), addr, balance);
                }
                if (0 == balance)
                {
                    if (DBStatus::DB_SUCCESS != db_writer.DeleteBalanceByAddress(addr))
                    {
                        return -29;
                    }
                }
                else
                {
                    if (DBStatus::DB_SUCCESS != db_writer.SetBalanceByAddress(addr, balance))
                    {
                        return -30;
                    }
                }
                // 11.
                if (DBStatus::DB_SUCCESS != db_writer.SetTxHashByUseUtxoHash(utxo_hash, tx.hash()))
                {
                    return -31;
                }
            }
        }
        // 12.
        for (auto &vout : tx.vout())
        {
            if (VIRTUAL_ACCOUNT_PLEDGE != vout.scriptpubkey())
            {
                all_addr.insert(vout.scriptpubkey());
            }
            ret = db_writer.SetUtxoHashsByAddress(vout.scriptpubkey(), tx.hash());
            if (DBStatus::DB_SUCCESS != ret && DBStatus::DB_NOT_FOUND != ret)
            {
                return -32;
            }
            int64_t balance = 0;
            ret = db_writer.GetBalanceByAddress(vout.scriptpubkey(), balance);
            if (DBStatus::DB_SUCCESS != ret)
            {
                if (DBStatus::DB_NOT_FOUND != ret)
                {
                    return -33;
                }
                else
                {
                    balance = 0;
                }
            }
            balance += vout.value();
            if (balance < 0)
            {
                ERRORLOG("SaveBlock vout height:{} hash:{} addr:{} balance:{}", block.height(), block.hash(), vout.scriptpubkey(), balance);
            }
            if (DBStatus::DB_SUCCESS != db_writer.SetBalanceByAddress(vout.scriptpubkey(), balance))
            {
                return -34;
            }
        }
        // 13.
        if (DBStatus::DB_SUCCESS != db_writer.SetTransactionByHash(tx.hash(), tx.SerializeAsString()))
        {
            return -35;
        }
        // 14.
        if (DBStatus::DB_SUCCESS != db_writer.SetBlockHashByTransactionHash(tx.hash(), block.hash()))
        {
            return -36;
        }
        // 15.
        for (auto &tmp_addr : all_addr)
        {
            if (DBStatus::DB_SUCCESS != db_writer.SetAllTransactionByAddress(tmp_addr, tx.hash()))
            {
                return -37;
            }
        }
        if (kTransactionType_Award == transaction_type)
        {
            uint64_t tmp_award_total = 0;
            for (auto &vout : tx.vout())
            {
                if (vout.value() <= 0)
                {
                    continue;
                }
                tmp_award_total += vout.value();

                // 16.
                uint64_t addr_award_total = 0;
                ret = db_writer.GetAwardTotalByAddress(vout.scriptpubkey(), addr_award_total);
                if (DBStatus::DB_SUCCESS != ret)
                {
                    if (DBStatus::DB_NOT_FOUND != ret)
                    {
                        return -38;
                    }
                    else
                    {
                        addr_award_total = 0;
                    }
                }
                addr_award_total += vout.value();
                if (DBStatus::DB_SUCCESS != db_writer.SetAwardTotalByAddress(vout.scriptpubkey(), addr_award_total))
                {
                    return -39;
                }
                // 17.
                uint64_t sign_num = 0;
                ret = db_writer.GetSignNumByAddress(vout.scriptpubkey(), sign_num);
                if (DBStatus::DB_SUCCESS != ret)
                {
                    if (DBStatus::DB_NOT_FOUND != ret)
                    {
                        return -40;
                    }
                    else
                    {
                        sign_num = 0;
                    }
                }
                ++sign_num;
                if (DBStatus::DB_SUCCESS != db_writer.SetSignNumByAddress(vout.scriptpubkey(), sign_num))
                {
                    return -41;
                }
            }
            // 18.
            uint64_t award_total = 0;
            ret = db_writer.GetAwardTotal(award_total);
            if (DBStatus::DB_SUCCESS != ret)
            {
                if (DBStatus::DB_NOT_FOUND != ret)
                {
                    return -42;
                }
                else
                {
                    award_total = 0;
                }
            }
            award_total += tmp_award_total;
            if (DBStatus::DB_SUCCESS != db_writer.SetAwardTotal(award_total))
            {
                return -43;
            }
        }
        TxHelper::DeployContractToDB(tx, db_writer);
        TxHelper::ExecuteContractToDB(tx, db_writer);
    }
    // 19.
    uint64_t counts = 0;
    ret = db_writer.GetTxCount(counts);
    if (DBStatus::DB_SUCCESS != ret)
    {
        if (DBStatus::DB_NOT_FOUND != ret)
        {
            return -44;
        }
        else
        {
            counts = 0;
        }
    }
    counts++;
    if (DBStatus::DB_SUCCESS != db_writer.SetTxCount(counts))
    {
        return -45;
    }
    // 燃料费
    counts = 0;
    ret = db_writer.GetGasCount(counts);
    if (DBStatus::DB_SUCCESS != ret)
    {
        if (DBStatus::DB_NOT_FOUND != ret)
        {
            return -46;
        }
        else
        {
            counts = 0;
        }
    }
    counts++;
    if (DBStatus::DB_SUCCESS != db_writer.SetGasCount(counts))
    {
        return -47;
    }
    // 额外奖励
    counts = 0;
    ret = db_writer.GetAwardCount(counts);
    if (DBStatus::DB_SUCCESS != ret)
    {
        if (DBStatus::DB_NOT_FOUND != ret)
        {
            return -48;
        }
        else
        {
            counts = 0;
        }
    }
    counts++;
    if (DBStatus::DB_SUCCESS != db_writer.SetAwardCount(counts))
    {
        return -49;
    }
    return 0;
    std::vector<std::string> addr_utxo_hashs;
    for (auto &addr : block_addr)
    {
        if (DBStatus::DB_SUCCESS != db_writer.GetUtxoHashsByAddress(addr, addr_utxo_hashs))
        {
            return -50;
        }
        uint64_t balance = 0;
        std::string txRaw;
        CTransaction balance_tx;
        for (auto utxo_hash : addr_utxo_hashs)
        {
            if (DBStatus::DB_SUCCESS != db_writer.GetTransactionByHash(utxo_hash, txRaw))
            {
                return -51;
            }
            if (!balance_tx.ParseFromString(txRaw))
            {
                return -52;
            }
            for (auto &vout : balance_tx.vout())
            {
                if (vout.scriptpubkey() == addr)
                {
                    balance += vout.value();
                }
            }
        }
        if (DBStatus::DB_SUCCESS != db_writer.SetBalanceByAddress(addr, balance))
        {
            return -53;
        }
    }

    return 0;
}

static int DeleteBlockByHash(DBReadWriter &db_writer, const std::string &block_hash)
{
    // 1.判断区块所在高度以及是否要更新节点高度以及bestChain
    // 2.删除块hash对应的高度
    // 3.删除高度上的块hash
    // 4.删除区块hash对应的区块头
    // 5.删除区块hash对应的区块数据
    // 6.质押交易更新质押地址以及质押地址的utxo
    // 7.解质押交易更新质押地址以及质押地址的utxo
    // 8.删除使用的质押utxo对应的解质押交易hash
    // 9.所有交易更新使用的utxo以及交易地址使用的utxo的余额
    // 10.所有交易删除使用的utxo对应的交易hash
    // 11.所有交易删除新的utxo以及交易地址使用的utxo的余额
    // 12.删除交易hash对应的交易体数据
    // 13.删除交易hash对应的区块hash
    // 14.更新交易地址的交易hash
    // 15.更新账号的额外奖励总值
    // 16.更新账号的签名次数
    // 17.更新额外奖励总数
    // 18.交易统计回滚 TxCount GasCount AwardCount

    CBlock block;
    std::string block_raw;
    auto ret = db_writer.GetBlockByBlockHash(block_hash, block_raw);
    if (DBStatus::DB_NOT_FOUND == ret)
    {
        return 0;
    }
    else if (DBStatus::DB_SUCCESS != ret)
    {
        return -1;
    }
    if (!block.ParseFromString(block_raw))
    {
        return -2;
    }
    uint64_t top = 0;
    if (DBStatus::DB_SUCCESS != db_writer.GetBlockTop(top))
    {
        return -3;
    }
    // 1.
    if (block.height() == top)
    {
        std::vector<std::string> tmp_block_hashs;
        if (DBStatus::DB_SUCCESS != db_writer.GetBlockHashsByBlockHeight(block.height(), tmp_block_hashs))
        {
            return -4;
        }
        if (1 == tmp_block_hashs.size())
        {
            std::string hash;
            if (DBStatus::DB_SUCCESS != db_writer.GetBlockHashByBlockHeight(block.height() - 1, hash))
            {
                return -5;
            }
            if (DBStatus::DB_SUCCESS != db_writer.SetBlockTop(block.height() - 1))
            {
                return -6;
            }
            if (DBStatus::DB_SUCCESS != db_writer.SetBestChainHash(hash))
            {
                return -7;
            }
        }
        else
        {
            std::string tmp_block_hash;
            if (DBStatus::DB_SUCCESS != db_writer.GetBestChainHash(tmp_block_hash))
            {
                return -8;
            }
            if (block.hash() == tmp_block_hash)
            {
                std::vector<std::string> block_raws;
                if (DBStatus::DB_SUCCESS != db_writer.GetBlocksByBlockHash(tmp_block_hashs, block_raws))
                {
                    return -9;
                }
                CBlock tmp_block;
                std::string bast_chain_hash;
                uint64_t start_time = 0xffffffffffffffff;
                for (auto &block_raw : block_raws)
                {
                    tmp_block.Clear();
                    if (!tmp_block.ParseFromString(block_raw))
                    {
                        return -10;
                    }
                    if (start_time > block.time())
                    {
                        bast_chain_hash = tmp_block.hash();
                    }
                }
                if (DBStatus::DB_SUCCESS != db_writer.SetBestChainHash(bast_chain_hash))
                {
                    return -11;
                }
            }
        }
    }

    // 2.
    if (DBStatus::DB_SUCCESS != db_writer.DeleteBlockHeightByBlockHash(block.hash()))
    {
        return -12;
    }
    // 3.
    if (DBStatus::DB_SUCCESS != db_writer.RemoveBlockHashByBlockHeight(block.height(), block.hash()))
    {
        return -13;
    }
    // 4.
    if (DBStatus::DB_SUCCESS != db_writer.DeleteBlockHeaderByBlockHash(block.hash()))
    {
        return -14;
    }
    // 5.
    if (DBStatus::DB_SUCCESS != db_writer.DeleteBlockByBlockHash(block.hash()))
    {
        ERRORLOG("DeleteBlockByBlockHash failed");
        return -15;
    }

    std::set<std::string> block_addr;
    std::set<std::string> all_addr;
    for (auto &tx : block.txs())
    {
        auto transaction_type = CheckTransactionType(tx);
        block_addr.insert(all_addr.cbegin(), all_addr.cend());
        all_addr.clear();
        if (kTransactionType_Tx == transaction_type)
        {
            bool is_redeem = false;
            std::string redeem_utxo_hash;
            try
            {
                nlohmann::json extra_json = nlohmann::json::parse(tx.extra());
                std::string tx_type = extra_json["TransactionType"].get<std::string>();
                // 6.
                if (TXTYPE_PLEDGE == tx_type)
                {
                    for (auto &vin : tx.vin())
                    {
                        std::string addr = GetBase58Addr(vin.scriptsig().pub());
                        if (!CheckBase58Addr(addr))
                        {
                            return -16;
                        }
                        if (DBStatus::DB_SUCCESS != db_writer.RemovePledgeAddressUtxo(addr, tx.hash()))
                        {
                            return -17;
                        }
                        std::vector<std::string> pledge_utxo_hashs;
                        auto ret = db_writer.GetPledgeAddressUtxo(addr, pledge_utxo_hashs);
                        if (DBStatus::DB_NOT_FOUND == ret || pledge_utxo_hashs.empty())
                        {
                            if (DBStatus::DB_SUCCESS != db_writer.RemovePledgeAddresses(addr))
                            {
                                return -18;
                            }
                        }
                        break;
                    }
                }
                // 7.
                else if (TXTYPE_REDEEM == tx_type)
                {
                    nlohmann::json tx_info = extra_json["TransactionInfo"].get<nlohmann::json>();
                    is_redeem = true;
                    redeem_utxo_hash = tx_info["RedeemptionUTXO"].get<std::string>();
                    for (auto &vin : tx.vin())
                    {
                        std::string addr = GetBase58Addr(vin.scriptsig().pub());
                        if (!CheckBase58Addr(addr))
                        {
                            return -19;
                        }
                        if (DBStatus::DB_SUCCESS != db_writer.SetPledgeAddressUtxo(addr, redeem_utxo_hash))
                        {
                            return -20;
                        }
                        std::vector<std::string> pledge_addrs;
                        auto ret = db_writer.GetPledgeAddress(pledge_addrs);
                        if (DBStatus::DB_SUCCESS != ret && DBStatus::DB_NOT_FOUND != ret)
                        {
                            return -21;
                        }
                        if (pledge_addrs.cend() == std::find(pledge_addrs.cbegin(), pledge_addrs.cend(), addr))
                        {
                            if (DBStatus::DB_SUCCESS != db_writer.SetPledgeAddresses(addr))
                            {
                                return -22;
                            }
                        }
                        break;
                    }
                    // 8.
                    if (DBStatus::DB_SUCCESS != db_writer.DeleteTxHashByUsePledeUtxoHash(redeem_utxo_hash))
                    {
                        return -23;
                    }
                }
                else if (TXTYPE_CONTRACT_DEPLOY == tx_type) 
                {
                    std::string owneraddr  = extra_json["owneraddr"].get<std::string>();
                    std::string ContractName = extra_json["ContractName"].get<std::string>();
                    if (DBStatus::DB_SUCCESS != db_writer.DeleteDeployContractTxHashByAddress(owneraddr,ContractName))
                    {
                        return -2003;
                    }
                    if (DBStatus::DB_SUCCESS != db_writer.DeleteAddressByContractName(ContractName))
                    {
                        return -2004;
                    }

                    if (DBStatus::DB_SUCCESS != db_writer.DeleteContractNameAbiName())
                    {
                        return -2005;
                    }
                }
                else if(TXTYPE_CONTRACT_EXECUTE == tx_type)
                {
                    std::string addr  = extra_json["addr"].get<std::string>();
                    std::string contract = extra_json["Contract"].get<std::string>();
                    if (DBStatus::DB_SUCCESS != db_writer.DeleteExecuteContractTxHashByAddress(addr,  contract))
                    {
                        return -2006;
                    }
                }
            }
            catch (...)
            {
                return -24;
            }
            std::string addr;
            std::string utxo_hash;
            CTransaction utxo_tx;
            std::string utxo_tx_raw;
            std::vector<std::string> vin_hashs;
            std::string utxo_n;
            for (auto &vin : tx.vin())
            {
                addr = GetBase58Addr(vin.scriptsig().pub());
                all_addr.insert(addr);
                utxo_hash = vin.prevout().hash();
                utxo_n = utxo_hash + "_" + std::to_string(vin.prevout().n());
                if (is_redeem && redeem_utxo_hash == utxo_hash && 0 == vin.prevout().n())
                {
                    continue;
                }
                if (vin_hashs.cend() != std::find(vin_hashs.cbegin(), vin_hashs.cend(), utxo_n))
                {
                    continue;
                }
                vin_hashs.push_back(utxo_n);
                // 9.
                if (DBStatus::DB_SUCCESS != db_writer.SetUtxoHashsByAddress(addr, utxo_hash))
                {
                    return -25;
                }
                if (DBStatus::DB_SUCCESS != db_writer.GetTransactionByHash(utxo_hash, utxo_tx_raw))
                {
                    return -26;
                }
                if (!utxo_tx.ParseFromString(utxo_tx_raw))
                {
                    return -27;
                }

                uint64_t amount = 0;
                for (int j = 0; j < utxo_tx.vout_size(); j++)
                {
                    CTxout txout = utxo_tx.vout(j);
                    if (txout.scriptpubkey() == addr)
                    {
                        amount += txout.value();
                    }
                }
                int64_t balance = 0;
                ret = db_writer.GetBalanceByAddress(addr, balance);
                if (DBStatus::DB_SUCCESS != ret)
                {
                    if (DBStatus::DB_NOT_FOUND != ret)
                    {
                        return -28;
                    }
                    else
                    {
                        balance = 0;
                    }
                }
                balance += amount;
                if (balance < 0)
                {
                    ERRORLOG("DeleteBlockByHash vin height:{} hash:{} addr:{} balance:{}", block.height(), block.hash(), addr, balance);
                }
                if (DBStatus::DB_SUCCESS != db_writer.SetBalanceByAddress(addr, balance))
                {
                    return -29;
                }
                // 10.
                if (DBStatus::DB_SUCCESS != db_writer.DeleteTxHashByUseUtxoHash(utxo_hash))
                {
                    return -30;
                }
            }
        }
        // 11.
        for (auto &vout : tx.vout())
        {
            all_addr.insert(vout.scriptpubkey());
            ret = db_writer.RemoveUtxoHashsByAddress(vout.scriptpubkey(), tx.hash());
            if (DBStatus::DB_SUCCESS != ret && DBStatus::DB_NOT_FOUND != ret)
            {
                return -31;
            }
            int64_t balance = 0;
            ret = db_writer.GetBalanceByAddress(vout.scriptpubkey(), balance);
            if (DBStatus::DB_SUCCESS != ret)
            {
                if (DBStatus::DB_NOT_FOUND != ret)
                {
                    return -32;
                }
                else
                {
                    balance = 0;
                }
            }
            balance -= vout.value();
            if (balance < 0)
            {
                ERRORLOG("DeleteBlockByHash vout height:{} hash:{} addr:{} balance:{}", block.height(), block.hash(), vout.scriptpubkey(), balance);
            }
            if (0 == balance)
            {
                if (DBStatus::DB_SUCCESS != db_writer.DeleteBalanceByAddress(vout.scriptpubkey()))
                {
                    return -33;
                }
            }
            else
            {
                if (DBStatus::DB_SUCCESS != db_writer.SetBalanceByAddress(vout.scriptpubkey(), balance))
                {
                    return -34;
                }
            }
        }
        // 12.
        if (DBStatus::DB_SUCCESS != db_writer.DeleteTransactionByHash(tx.hash()))
        {
            return -35;
        }
        // 13.
        if (DBStatus::DB_SUCCESS != db_writer.DeleteBlockHashByTransactionHash(tx.hash()))
        {
            return -36;
        }
        // 14.
        for (auto &tmp_addr : all_addr)
        {
            if (DBStatus::DB_SUCCESS != db_writer.RemoveAllTransactionByAddress(tmp_addr, tx.hash()))
            {
                return -37;
            }
        }

        if (kTransactionType_Award == transaction_type)
        {
            uint64_t tmp_award_total = 0;
            for (auto &vout : tx.vout())
            {
                if (vout.value() <= 0)
                {
                    continue;
                }
                tmp_award_total += vout.value();

                // 15.
                uint64_t addr_award_total = 0;
                ret = db_writer.GetAwardTotalByAddress(vout.scriptpubkey(), addr_award_total);
                if (DBStatus::DB_SUCCESS != ret)
                {
                    return -38;
                }
                addr_award_total -= vout.value();
                if (DBStatus::DB_SUCCESS != db_writer.SetAwardTotalByAddress(vout.scriptpubkey(), addr_award_total))
                {
                    return -39;
                }
                // 16.
                uint64_t sign_num = 0;
                ret = db_writer.GetSignNumByAddress(vout.scriptpubkey(), sign_num);
                if (DBStatus::DB_SUCCESS != ret)
                {
                    if (DBStatus::DB_NOT_FOUND != ret)
                    {
                        return -40;
                    }
                    else
                    {
                        sign_num = 0;
                    }
                }
                --sign_num;
                if (DBStatus::DB_SUCCESS != db_writer.SetSignNumByAddress(vout.scriptpubkey(), sign_num))
                {
                    return -41;
                }
                if (0 == sign_num)
                {
                    if (DBStatus::DB_SUCCESS != db_writer.DeleteAwardTotalByAddress(vout.scriptpubkey()))
                    {
                        return -42;
                    }
                    if (DBStatus::DB_SUCCESS != db_writer.DeleteSignNumByAddress(vout.scriptpubkey()))
                    {
                        return -43;
                    }
                }
            }
            // 17.
            uint64_t award_total = 0;
            ret = db_writer.GetAwardTotal(award_total);
            if (DBStatus::DB_SUCCESS != ret)
            {
                if (DBStatus::DB_NOT_FOUND != ret)
                {
                    return -44;
                }
                else
                {
                    award_total = 0;
                }
            }
            award_total -= tmp_award_total;
            if (DBStatus::DB_SUCCESS != db_writer.SetAwardTotal(award_total))
            {
                return -45;
            }
        }
    }
    // 18.
    uint64_t counts = 0;
    ret = db_writer.GetTxCount(counts);
    if (DBStatus::DB_SUCCESS != ret)
    {
        if (DBStatus::DB_NOT_FOUND != ret)
        {
            return -46;
        }
        else
        {
            counts = 0;
        }
    }
    --counts;
    if (DBStatus::DB_SUCCESS != db_writer.SetTxCount(counts))
    {
        return -47;
    }
    // 燃料费
    counts = 0;
    ret = db_writer.GetGasCount(counts);
    if (DBStatus::DB_SUCCESS != ret)
    {
        if (DBStatus::DB_NOT_FOUND != ret)
        {
            return -48;
        }
        else
        {
            counts = 0;
        }
    }
    --counts;
    if (DBStatus::DB_SUCCESS != db_writer.SetGasCount(counts))
    {
        return -49;
    }
    // 额外奖励
    counts = 0;
    ret = db_writer.GetAwardCount(counts);
    if (DBStatus::DB_SUCCESS != ret)
    {
        if (DBStatus::DB_NOT_FOUND != ret)
        {
            return -50;
        }
        else
        {
            counts = 0;
        }
    }
    --counts;
    if (DBStatus::DB_SUCCESS != db_writer.SetAwardCount(counts))
    {
        return -51;
    }
    return 0;
    std::vector<std::string> addr_utxo_hashs;
    for (auto &addr : block_addr)
    {
        if (DBStatus::DB_SUCCESS != db_writer.GetUtxoHashsByAddress(addr, addr_utxo_hashs))
        {
            return -50;
        }
        uint64_t balance = 0;
        std::string txRaw;
        CTransaction balance_tx;
        for (auto utxo_hash : addr_utxo_hashs)
        {
            if (DBStatus::DB_SUCCESS != db_writer.GetTransactionByHash(utxo_hash, txRaw))
            {
                return -51;
            }
            if (!balance_tx.ParseFromString(txRaw))
            {
                return -52;
            }
            for (auto &vout : balance_tx.vout())
            {
                if (vout.scriptpubkey() == addr)
                {
                    balance += vout.value();
                }
            }
        }
        if (DBStatus::DB_SUCCESS != db_writer.SetBalanceByAddress(addr, balance))
        {
            return -53;
        }
    }
    return 0;
}
static int RollBackBlock(const std::multimap<uint64_t, std::string> &hashs)
{
    int i = 0;
    DBReadWriter db_writer;
    for (auto it = hashs.rbegin(); hashs.rend() != it; ++it)
    {
        ++i;
        auto ret = DeleteBlockByHash(db_writer, it->second);
        if (0 != ret)
        {
            return ret - 100;
        }
        if(10 == i)
        {
            i = 0;
            if (DBStatus::DB_SUCCESS != db_writer.TransactionCommit())
            {
                return -1;
            }
            if (DBStatus::DB_SUCCESS != db_writer.ReTransactionInit())
            {
                return -2;
            }
        }
    }
    if(i > 0)
    {
        if (DBStatus::DB_SUCCESS != db_writer.TransactionCommit())
        {
            return -3;
        }
    }
    return 0;
}
int ca_algorithm::RollBackToHeight(uint64_t height)
{
    DBReadWriter db_writer;
    uint64_t node_height = 0;
    if (DBStatus::DB_SUCCESS != db_writer.GetBlockTop(node_height))
    {
        return -1;
    }
    std::multimap<uint64_t, std::string> hashs;
    std::vector<std::string> block_hashs;
    for (uint32_t i = node_height; i > height; --i)
    {
        block_hashs.clear();
        if (DBStatus::DB_SUCCESS != db_writer.GetBlockHashsByBlockHeight(i, block_hashs))
        {
            return -2;
        }
        for (auto &hash : block_hashs)
        {
            hashs.insert(std::make_pair(i, hash));
        }
    }
    auto ret = RollBackBlock(hashs);
    if (0 != ret)
    {
        return ret - 1000;
    }
    return 0;
}
int ca_algorithm::RollBackByHash(const std::string &block_hash)
{
    DBReadWriter db_writer;
    std::multimap<uint64_t, std::string> hashs;
    uint64_t height = 0;
    bool rollback_by_height = false;
    {
        std::set<std::string> rollback_block_hashs;
        std::set<std::string> rollback_trans_hashs;
        CBlock block;
        {
            std::string block_raw;
            auto ret = db_writer.GetBlockByBlockHash(block_hash, block_raw);
            if (DBStatus::DB_NOT_FOUND == ret)
            {
                return 0;
            }
            else if (DBStatus::DB_SUCCESS != ret)
            {
                return -1;
            }
            if (!block.ParseFromString(block_raw))
            {
                return -2;
            }
            hashs.insert(std::make_pair(block.height(), block.hash()));
            rollback_block_hashs.insert(block.hash());
            for (auto &tx : block.txs())
            {
                rollback_trans_hashs.insert(tx.hash());
            }
        }
        uint64_t node_height = 0;
        if (DBStatus::DB_SUCCESS != db_writer.GetBlockTop(node_height))
        {
            return -3;
        }
        std::vector<std::string> block_raws;
        std::vector<std::string> block_hashs;
        for (height = block.height(); height < node_height + 1; ++height)
        {
            block_hashs.clear();
            block_raws.clear();
            if (DBStatus::DB_SUCCESS != db_writer.GetBlockHashsByBlockHeight(height, block_hashs))
            {
                return -4;
            }
            if (DBStatus::DB_SUCCESS != db_writer.GetBlocksByBlockHash(block_hashs, block_raws))
            {
                return -5;
            }
            bool flag = false;
            for (auto &block_raw : block_raws)
            {
                block.Clear();
                if (!block.ParseFromString(block_raw))
                {
                    return -6;
                }
                if (rollback_block_hashs.end() == std::find(rollback_block_hashs.cbegin(), rollback_block_hashs.cend(), block.prevhash()))
                {
                    for (auto &tx : block.txs())
                    {
                        if (GetTransactionType(tx) != kTransactionType_Tx)
                        {
                            continue;
                        }
                        for (auto &vin : tx.vin())
                        {
                            auto utxo_hash = vin.prevout().hash();
                            if (rollback_trans_hashs.end() != std::find(rollback_trans_hashs.cbegin(), rollback_trans_hashs.cend(), utxo_hash))
                            {
                                flag = true;
                                break;
                            }
                        }
                        if (flag)
                        {
                            break;
                        }
                    }
                }
                else
                {
                    flag = true;
                }
                if (flag)
                {
                    hashs.insert(std::make_pair(block.height(), block.hash()));
                    rollback_block_hashs.insert(block.hash());
                    for (auto &tx : block.txs())
                    {
                        rollback_trans_hashs.insert(tx.hash());
                    }
                }
            }
            if (flag && block_hashs.size() <= 1)
            {
                height = block.height();
                rollback_by_height = true;
                break;
            }
        }
    }
    if (rollback_by_height)
    {
        uint64_t node_height = 0;
        if (DBStatus::DB_SUCCESS != db_writer.GetBlockTop(node_height))
        {
            return -1;
        }
        std::vector<std::string> block_hashs;
        for (uint32_t i = node_height; i > height; --i)
        {
            block_hashs.clear();
            if (DBStatus::DB_SUCCESS != db_writer.GetBlockHashsByBlockHeight(i, block_hashs))
            {
                return -2;
            }
            for (auto &hash : block_hashs)
            {
                hashs.insert(std::make_pair(i, hash));
            }
        }
    }

    auto ret = RollBackBlock(hashs);
    if (0 != ret)
    {
        return ret - 1000;
    }
    return 0;
}

void ca_algorithm::PrintTx(const CTransaction &tx)
{
    using namespace std;
    cout << "========================================================================================================================" << endl;
    cout << "\ttx.version:" << tx.version() << endl;
    cout << "\ttx.time:" << tx.time() << endl;

    std::string hex;
    for (auto &sign_pre_hash : tx.signprehash())
    {
        hex.clear();
        Bytes2Hex(sign_pre_hash.sign(), hex, true);
        // cout << "\t\tsign_pre_hash.sign:" << hex << endl;
        hex.clear();
        Bytes2Hex(sign_pre_hash.pub(), hex, true);
        // cout << "\t\tsign_pre_hash.pub:" << hex << endl;
        cout << "\t\tsign_pre_hash.addr:" << GetBase58Addr(sign_pre_hash.pub()) << endl;
    }

    for (auto &vin : tx.vin())
    {
        cout << "\t\tvin.sequence:" << vin.sequence() << endl;
        auto &prevout = vin.prevout();
        cout << "\t\t\tprevout.hash:" << prevout.hash() << endl;
        cout << "\t\t\tprevout.n:" << prevout.n() << endl;
        auto &scriptSig = vin.scriptsig();
        // hex.clear();
        // Bytes2Hex(scriptSig.sign(), hex, true);
        // cout << "\t\t\tscriptSig.sign:" << hex << endl;
        // hex.clear();
        // Bytes2Hex(scriptSig.sign(), hex, true);
        // cout << "\t\t\tscriptSig.pub:" << hex << endl;
        cout << "\t\t\tscriptSig.addr:" << GetBase58Addr(scriptSig.pub()) << endl;
    }
    for (auto &vout : tx.vout())
    {
        cout << "\t\tvout.scriptpubkey:" << vout.scriptpubkey() << endl;
        cout << "\t\tvout.value:" << vout.value() << endl;
    }

    cout << "\ttx.owner:" << tx.owner() << endl;
    cout << "\ttx.n:" << tx.n() << endl;
    cout << "\ttx.identity:" << tx.identity() << endl;
    cout << "\ttx.hash:" << tx.hash() << endl;
    cout << "\ttx.extra:" << tx.extra() << endl;
    cout << "\ttx.comment:" << tx.comment() << endl;
}

void ca_algorithm::PrintBlock(const CBlock &block)
{
    using namespace std;
    cout << "version:" << block.version() << endl;
    cout << "hash:" << block.hash() << endl;
    cout << "prevhash:" << block.prevhash() << endl;
    cout << "height:" << block.height() << endl;
    cout << "merkleroot:" << block.merkleroot() << endl;

    for (auto &tx : block.txs())
    {
        PrintTx(tx);
    }

    for (auto &address : block.addresses())
    {
        cout << "\taddress.txowner:" << address.txowner() << endl;
        cout << "\taddress.n:" << address.n() << endl;
    }
    cout << "extra:" << block.extra() << endl;
    cout << "comment:" << block.comment() << endl;
    cout << "time:" << block.time() << endl;
    cout << "========================================================================================================================" << endl;
}
