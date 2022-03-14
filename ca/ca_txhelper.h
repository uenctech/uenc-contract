/*
 * @Author: your name
 * @Date: 2020-09-18 18:01:27
 * @LastEditTime: 2021-01-28 10:39:35
 * @LastEditors: Please set LastEditors
 * @Description: In User Settings Edit
 * @FilePath: \ebpc\ca\ca_txhelper.h
 */
#ifndef _TXHELPER_H_
#define _TXHELPER_H_

#include <map>
#include <list>
#include <mutex>
#include <vector>
#include <string>
#include <thread>
#include <vector>
#include <bitset>
#include <iostream>
#include "../proto/ca_protomsg.pb.h"
#include "db/db_api.h"

class TxHelper
{
public:
    TxHelper() = default;
    ~TxHelper() = default;

    static std::vector<std::string> GetTxOwner(const std::string tx_hash);
    static std::vector<std::string> GetTxOwner(const CTransaction& tx);
    static std::string GetTxOwnerStr(const std::string tx_hash);
    static std::string GetTxOwnerStr(const CTransaction& tx);
    static uint64_t GetUtxoAmount(std::string tx_hash, std::string address);
    static std::vector<std::string> GetUtxosByAddresses(std::vector<std::string> addresses);
    static std::vector<std::string> GetUtxosByTx(const CTransaction& tx);

    typedef enum emTransactionType 
    {
        kTransactionType_Unknown = -1,		// 未知
        kTransactionType_Tx = 0,			// 正常交易
        kTransactionType_Pledge,			// 质押交易
        kTransactionType_Redeem,	        // 解质押交易
        kTransactionType_Contract_Deploy,  //合约部署
        kTransactionType_Contract_Execute,  //合约执行
    } TransactionType;

    struct Utxo
    {
        std::uint64_t value;
        std::string scriptpubkey;
        std::string hash;
        std::uint32_t n;
    };

    class UtxoCompare
    {
    public:
        bool operator()(const Utxo& utxo1, const Utxo& utxo2) const
        {
            return utxo1.value < utxo2.value;
        }
    };

    typedef enum emPledgeType {
        kPledgeType_Unknown = -1,		// 未知
        kPledgeType_Node = 0,			// 节点质押
        kPledgeType_PublicNode,			// 公网质押
    } PledgeType;

    static int Check(const std::vector<std::string> &fromAddr,
                         const std::map<std::string, int64_t> toAddr,
                         uint32_t needVerifyPreHashCount,
                         uint64_t minerFees,
                         TxHelper::TransactionType type);

    static int FindUtxo(const std::vector<std::string>& fromAddr,
						const uint64_t expend,
						const uint64_t need_utxo_amount,
						uint64_t& total,
						std::multiset<TxHelper::Utxo, TxHelper::UtxoCompare>& setOutUtxos);

    static int CreateTxTransaction(const std::vector<std::string> & fromAddr,
                                   const std::map<std::string, int64_t> & toAddr,
                                   uint32_t needVerifyPreHashCount,
                                   uint64_t minerFees,
                                   CTransaction &outTx,
                                   std::vector<TxHelper::Utxo> & outVin);

    static int CreatePledgeTransaction(const std::string & fromAddr,
                                       uint64_t pledge_amount,
                                       uint32_t needVerifyPreHashCount,
                                       uint64_t minerFees,
                                       TxHelper::PledgeType pledgeType,
                                       CTransaction &outTx,
                                       std::vector<TxHelper::Utxo> & outVin);

    static int CreateRedeemTransaction(const std::string fromAddr,
                                       const std::string utxo_hash,
                                       uint32_t needVerifyPreHashCount,
                                       uint64_t minerFees,
                                       CTransaction &outTx, 
                                       std::vector<TxHelper::Utxo> & outVin);

    static int SignTransaction(const std::vector<TxHelper::Utxo> & outVin,
                               CTransaction &tx,
                               std::string &serTx,
                               std::string &encodeStrHash);

    static int CreateDeployContractMessage(const std::string & owneraddr, 
        const std::string &contract_name,
        const std::string &contract_raw, 
        const std::string &abi, 
        const std::string &contractversion,
        const std::string &description,
        uint32_t needVerifyPreHashCount, 
        uint64_t minerFees,
        uint64_t amount, 
        uint64_t impcnt,
        CTransaction & outTx,
        std::vector<TxHelper::Utxo> & outVin);


    static void DoDeployContract(const std::string & owneraddr, 
        const std::string &contract_name,
        const std::string &contract_raw, 
        const std::string &abi,
        const std::string &contractversion,
        const std::string &description,
        uint32_t needVerifyPreHashCount, 
        uint64_t gasFee,
        uint64_t  amount,
        uint64_t impcnt,
        std::string &txhash);

    
    static int DeployContractToDB(const CTransaction & tx, DBReadWriter &db_writer);
    static int ExecuteContractToDB(const CTransaction & tx, DBReadWriter &db_writer);


    static int CreateExecuteContractMessage(const std::string & addr, 
        const std::string &contract, 
        const std::string &action,
        const std::string &params,
        uint32_t needVerifyPreHashCount, 
        uint64_t minerFees, 
        CTransaction & outTx,
        std::vector<TxHelper::Utxo> & outVin);


    static void DoeExecuteContract(const std::string & addr, 
        const std::string &contract, 
        const std::string &action,
        const std::string &params,
        uint32_t needVerifyPreHashCount, 
        uint64_t gasFee,
        std::string &txhash);

    static int ExecutePreContractToBlock( const std::string & addr, 
        const std::string &contractname, 
        const std::string &action,
        const std::string &params, 
	    DBReader &db_reader,
        std::string &execret);                           
};
#endif



