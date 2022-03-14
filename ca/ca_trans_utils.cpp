#include "ca_trans_utils.h"
#include "MagicSingleton.h"
#include "include/ScopeGuard.h"
#include "ca_global.h"
#include <map>
#include <algorithm>
#include "utils/json.hpp"
#include "../include/logging.h"
#include "db/db_api.h"

//根据地址查询与地址相关的质押交易
bool TransUtils::GetPledgeUtxoByAddr(const std::string &address, std::vector<UtxoTx> &out)
{
    std::vector<UtxoTx> utxt_txes;
    if(!GetAllUtxoByAddr(address, utxt_txes))
    {
        return false;
    }
    for(auto it = utxt_txes.cbegin(); it != utxt_txes.cend(); it++)
    {
        const std::string &strextra = it->utxo.extra();
        std::string type;
        if(!strextra.empty())
        {
            nlohmann::json extra = nlohmann::json::parse(strextra);
            type = extra["TransactionType"].get<std::string>();
        }
        //过滤区块
        if(type == TXTYPE_PLEDGE)
        {
            if(it->utxo.vin_size() > 0)
            {
                const CTxin &vin = it->utxo.vin(0);
                if ( vin.scriptsig().sign() == FEE_SIGN_STR ||  vin.scriptsig().sign() == EXTRA_AWARD_SIGN_STR)
                    continue;
                out.push_back(*it);
            }
        }
    }
    return true;
}
//根据地址查询与地址相关的解质押交易
bool TransUtils::GetRedeemUtxoByAddr(const std::string &address, std::vector<UtxoTx> &out)
{
    std::vector<UtxoTx> utxt_txes;
    if(!GetAllUtxoByAddr(address, utxt_txes))
    {
        return false;
    }
    for(auto it = utxt_txes.cbegin(); it != utxt_txes.cend(); it++)
    {
        const std::string &strextra = it->utxo.extra();
        std::string type;
        if(!strextra.empty())
        {
            nlohmann::json extra = nlohmann::json::parse(strextra);
            type = extra["TransactionType"].get<std::string>();
        }
        //过滤区块
        if(type == TXTYPE_REDEEM)
        {
            if(it->utxo.vin_size() > 0)
            {
                const CTxin &vin = it->utxo.vin(0);
                if ( vin.scriptsig().sign() == FEE_SIGN_STR ||  vin.scriptsig().sign() == EXTRA_AWARD_SIGN_STR)
                    continue;
                out.push_back(*it);
            }
        }
    }
    return true;
}
//根据地址查询与地址相关的质押与解质押
bool TransUtils::GetPledgeAndRedeemUtxoByAddr(const std::string &address, std::vector<UtxoTx> &out)
{
    std::vector<UtxoTx> utxt_txes;
    if(!GetAllUtxoByAddr(address, utxt_txes))
    {
        return false;
    }
    for(auto it = utxt_txes.cbegin(); it != utxt_txes.cend(); it++)
    {
        const std::string &strextra = it->utxo.extra();
        std::string type;
        if(!strextra.empty())
        {
            nlohmann::json extra = nlohmann::json::parse(strextra);
            type = extra["TransactionType"].get<std::string>();
        }
        //过滤区块
        if(type == TXTYPE_PLEDGE || type == TXTYPE_REDEEM)
        {
            if(it->utxo.vin_size() > 0)
            {
                const CTxin &vin = it->utxo.vin(0);
                if ( vin.scriptsig().sign() == FEE_SIGN_STR ||  vin.scriptsig().sign() == EXTRA_AWARD_SIGN_STR)
                    continue;
                out.push_back(*it);
            }
        }
    }
    return true;
}
//根据地址查询与地址相关的正常交易
bool TransUtils::GetTxUtxoByAddr(const std::string &address, std::vector<UtxoTx> &out)
{
    std::vector<UtxoTx> utxt_txes;
    if(!GetAllUtxoByAddr(address, utxt_txes))
    {
        return false;
    }
    for(auto it = utxt_txes.cbegin(); it != utxt_txes.cend(); it++)
    {
        const std::string &strextra = it->utxo.extra();
        std::string type;
        if(!strextra.empty())
        {
            nlohmann::json extra = nlohmann::json::parse(strextra);
            type = extra["TransactionType"].get<std::string>();
        }
        //过滤区块
        if(type == TXTYPE_TX)
        {
            if(it->utxo.vin_size() > 0)
            {
                const CTxin &vin = it->utxo.vin(0);
                if (vin.scriptsig().sign() == FEE_SIGN_STR ||  vin.scriptsig().sign() == EXTRA_AWARD_SIGN_STR)
                    continue;
                out.push_back(*it);
            }
        }
    }
    return true;
}
//根据地址查询与地址相关的手续费交易
bool TransUtils::GetSignUtxoByAddr(const std::string &address, std::vector<UtxoTx> &out)
{
    std::vector<UtxoTx> utxt_txes;
    if(!GetAllUtxoByAddr(address, utxt_txes))
    {
        return false;
    }
    for(auto it = utxt_txes.cbegin(); it != utxt_txes.cend(); it++)
    {
        
        if(it->utxo.vin_size() > 0
         && it->utxo.vin(0).scriptsig().sign() == FEE_SIGN_STR)
         {
            out.push_back(*it); 
         }
    }
    return true;
}
//根据地址查询与地址相关的挖矿交易
bool TransUtils::GetAwardUtxoByAddr(const std::string &address, std::vector<UtxoTx> &out)
{
    std::vector<UtxoTx> utxt_txes;
    if(!GetAllUtxoByAddr(address, utxt_txes))
    {
        return false;
    }
    for(auto it = utxt_txes.cbegin(); it != utxt_txes.cend(); it++)
    {
        if(it->utxo.vin_size() > 0
         && it->utxo.vin(0).scriptsig().sign() == EXTRA_AWARD_SIGN_STR)
        {
            out.push_back(*it); 
         }
    }
    return true;
}

//根据地址查询与地址相关的签名交易和挖扩交易  20210714
bool TransUtils::GetSignAndAwardUtxoByAddr(const std::string &address, std::vector<UtxoTx> &out)
{
    std::vector<UtxoTx> utxt_txes;
    if (!GetAllUtxoByAddr(address, utxt_txes))
    {
        return false;
    }
    for (auto it = utxt_txes.cbegin(); it != utxt_txes.cend(); it++)
    {
        if (it->utxo.vin_size() > 0)
        {
            string sign = it->utxo.vin(0).scriptsig().sign();
            if (sign == string(FEE_SIGN_STR) || sign == string(EXTRA_AWARD_SIGN_STR))
            {
                if (it->utxo.vout_size() > 0 && (it->utxo.vout(0).scriptpubkey() == address && it->utxo.vout(0).value() == 0))
                {
                    continue ;
                }
                out.push_back(*it);
            }
        }
    }
    //cout << "In GetSignAndAwardUtxoByAddr: all size = " << utxt_txes.size() << " selected size = " << out.size() << endl;
    return true;
}

//根据地址查询与地址相关的质押与解质押以及正常交易
bool TransUtils::GetUtxoByAddr(const std::string &address, std::vector<UtxoTx> &out)
{
    std::vector<UtxoTx> utxt_txes;
    if(!GetAllUtxoByAddr(address, utxt_txes))
    {
        return false;
    }
    for(auto it = utxt_txes.cbegin(); it != utxt_txes.cend(); it++)
    {
        if(it->utxo.vin_size() > 0)
        {
            const CTxin &vin = it->utxo.vin(0);
            if (vin.scriptsig().sign() == FEE_SIGN_STR ||  vin.scriptsig().sign() == EXTRA_AWARD_SIGN_STR)
                continue;
            out.push_back(*it);
        }
    }
    return true;
}
//根据地址查询与地址相关的所有交易
bool TransUtils::GetAllUtxoByAddr(const std::string &address, std::vector<UtxoTx> &out)
{
    DBReader db_reader;
    std::vector<std::string> vTxHashs;
    if(DBStatus::DB_SUCCESS != db_reader.GetAllTransactionByAddreess(address, vTxHashs))
    {
        return false;
    }
    //去重
    std::sort(vTxHashs.begin(), vTxHashs.end());
    vTxHashs.erase(std::unique(vTxHashs.begin(), vTxHashs.end()), vTxHashs.end());

    std::string strblock, type;
    std::multimap<uint32_t, CTransaction, std::greater<uint32_t>> txinfos;
    for(auto it = vTxHashs.cbegin(); it != vTxHashs.cend(); it++)
    {
        std::string txRaw;
        if (DBStatus::DB_SUCCESS != db_reader.GetTransactionByHash(*it, txRaw))
        {
            return false;
        }
        CTransaction utxo_tx;
        utxo_tx.ParseFromString(txRaw);
        std::string blockhash;
        unsigned height;
        auto stat = db_reader.GetBlockHashByTransactionHash(*it, blockhash);
        if (DBStatus::DB_SUCCESS != stat)
        {
            return false;
        }
        stat = db_reader.GetBlockHeightByBlockHash(blockhash, height);
        if (DBStatus::DB_SUCCESS != stat)
        {
            return false;
        }
        txinfos.insert(std::make_pair(height, utxo_tx));
    }
    std::vector<UtxoTx>().swap(out);
    for(auto iter = txinfos.begin(); iter != txinfos.end(); iter++)
    {
        UtxoTx utxo_tx;
        utxo_tx.height = iter->first;
        utxo_tx.utxo = iter->second;
        out.push_back(utxo_tx);
    }
    return true;
}
