#include <iostream>
#include <memory>
#include <sys/time.h>

#include "ca_rollback.h"
#include "ca_console.h"
#include "ca_txhelper.h"
#include "ca_transaction.h"

#include "MagicSingleton.h"
#include "../include/ScopeGuard.h"
#include "../utils/json.hpp"
#include "ca/ca_blockcache.h"
#include "block.pb.h"



int Rollback::RollbackRedeemTx(DBReadWriter &db_read_writer, CTransaction &tx)
{
	ca_console ResBlockColor(kConsoleColor_Green, kConsoleColor_Black, true);

	nlohmann::json txExtra = nlohmann::json::parse(tx.extra());
	nlohmann::json txInfo = txExtra["TransactionInfo"].get<nlohmann::json>();
	std::string redempUtxoStr = txInfo["RedeemptionUTXO"].get<std::string>();

	// 取出交易发起方，解质押交易只有一个发起方和接收方
	std::vector<std::string> owner_addrs = TxHelper::GetTxOwner(tx);
	std::string txOwner = owner_addrs[0];

	if(CheckTransactionType(tx) == kTransactionType_Tx)
	{
		uint64_t pledgeValue = 0;

		std::string txRaw;
		if (DBStatus::DB_SUCCESS != db_read_writer.GetTransactionByHash(redempUtxoStr, txRaw) )
		{
			ERRORLOG("GetTransactionByHash failed !!!");
			return -1;
		}

		CTransaction utxoTx;
		utxoTx.ParseFromString(txRaw);

		for (int j = 0; j < utxoTx.vout_size(); j++)
		{
			CTxout txOut = utxoTx.vout(j);
			if (txOut.scriptpubkey() == VIRTUAL_ACCOUNT_PLEDGE)
			{
				pledgeValue += txOut.value();
			}
		}

		// 回滚vin
		std::vector<std::string> vinUtxos;
		uint64_t vinAmountTotal = 0;
		uint64_t voutAmountTotal = 0;
		for (auto & txin : tx.vin())
		{
			vinUtxos.push_back(txin.prevout().hash());
		}

		auto iter = find(vinUtxos.begin(), vinUtxos.end(), redempUtxoStr);
		if (iter != vinUtxos.end())
		{
			vinUtxos.erase(iter);
		}
		else
		{
			ERRORLOG("Find redempUtxoStr in vinUtxos failed !");
			return -2;
		}
		
		for (auto & vinUtxo : vinUtxos)
		{
			vinAmountTotal += TxHelper::GetUtxoAmount(vinUtxo, txOwner);
		}
		
		for (auto & txout : tx.vout())
		{
			voutAmountTotal += txout.value();
		}

		// 判断解质押交易的vin中是否有质押产生的正常utxo部分
		nlohmann::json extra = nlohmann::json::parse(tx.extra());
		uint64_t signFee = extra["SignFee"].get<int>();
		uint64_t NeedVerifyPreHashCount = extra["NeedVerifyPreHashCount"].get<int>();
		uint64_t packageFee = extra["PackageFee"].get<int>();

		voutAmountTotal += signFee * (NeedVerifyPreHashCount - 1);
		voutAmountTotal += packageFee;

		bool bIsUnused = true;
		if (voutAmountTotal != vinAmountTotal)
		{
			uint64_t usable = TxHelper::GetUtxoAmount(redempUtxoStr, txOwner);
			if (voutAmountTotal == vinAmountTotal - usable)
			{
				// 本交易未使用质押utxo的正常部分
				bIsUnused = false;
			}
		}

		for (auto & txin : tx.vin())
		{
			if (txin.prevout().hash() == redempUtxoStr && bIsUnused)
			{
				continue;
			}
			
			if (DBStatus::DB_SUCCESS != db_read_writer.SetUtxoHashsByAddress(txOwner, txin.prevout().hash()) )
			{
				std::string txRaw;
				if (DBStatus::DB_SUCCESS != db_read_writer.GetTransactionByHash(txin.prevout().hash(), txRaw) )
				{
					ERRORLOG("GetTransactionByHash failed !!!");
					return -2;
				}

				CTransaction vinUtxoTx;
				vinUtxoTx.ParseFromString(txRaw);

				nlohmann::json extra = nlohmann::json::parse(vinUtxoTx.extra());
				std::string txType = extra["TransactionType"].get<std::string>();
				if (txType != TXTYPE_REDEEM)
				{
					ERRORLOG("SetUtxoHashsByAddress failed !!!");
					return -3;
				}
			}
		}

		int64_t value = 0;
		if (DBStatus::DB_SUCCESS != db_read_writer.GetBalanceByAddress(txOwner, value) )
		{
			ERRORLOG("GetBalanceByAddress failed !!!");
			return -2;
		}

		int64_t amount = 0;
		amount = value - pledgeValue;

		// 回滚余额
		if (DBStatus::DB_SUCCESS != db_read_writer.SetBalanceByAddress(txOwner, amount) )
		{
			ERRORLOG("SetBalanceByAddress failed !!!");
			return -3;
		}

		// 删除交易记录
		if (DBStatus::DB_SUCCESS != db_read_writer.RemoveAllTransactionByAddress(txOwner, tx.hash()) )
		{
			ERRORLOG("RemoveAllTransactionByAddress failed !!!");
			return -4;
		}

		// 放回解质押的utxo
		if (DBStatus::DB_SUCCESS != db_read_writer.SetPledgeAddressUtxo(txOwner, redempUtxoStr) )
		{
			ERRORLOG("SetPledgeAddressUtxo failed !!!");
			return -5;
		}

		// 放回解质押的地址
		std::vector<std::string> utxoes;
		if (DBStatus::DB_SUCCESS != db_read_writer.GetPledgeAddressUtxo(txOwner, utxoes))
		{
			ERRORLOG("GetPledgeAddressUtxo failed !!!");
			return -6;
		}

		// 如果是刚放入的utxo，则说明回滚前无质押地址，需要放回去
		if (utxoes.size() == 1)
		{
			if (DBStatus::DB_SUCCESS != db_read_writer.SetPledgeAddresses(txOwner))
			{
				ERRORLOG("SetPledgeAddresses failed !!!");
				return -7;
			}
		}

		if (DBStatus::DB_SUCCESS != db_read_writer.RemoveUtxoHashsByAddress(txOwner, tx.hash()))
		{
			ERRORLOG("RemoveUtxoHashsByAddress failed !!!");
			return -8;
		}
	}
	else if (CheckTransactionType(tx) == kTransactionType_Fee || CheckTransactionType(tx) == kTransactionType_Award)
	{
		uint64_t signFee = 0;
		std::string txOwnerAddr;
		std::string txRaw;
		if (DBStatus::DB_SUCCESS != db_read_writer.GetTransactionByHash(redempUtxoStr, txRaw) )
		{
			ERRORLOG("GetTransactionByHash failed !!!");
			return -9;
		}

		CTransaction utxoTx;
		utxoTx.ParseFromString(txRaw);

		for (int j = 0; j < utxoTx.vout_size(); j++)
		{
			CTxout txOut = utxoTx.vout(j);
			if (txOut.scriptpubkey() != VIRTUAL_ACCOUNT_PLEDGE)
			{
				txOwnerAddr += txOut.scriptpubkey();
			}
		}

		for (int j = 0; j < tx.vout_size(); j++)
		{
			CTxout txout = tx.vout(j);

			if (txout.scriptpubkey() != txOwnerAddr)
			{
				signFee += txout.value();
			}

			int64_t value = 0;
			if (DBStatus::DB_SUCCESS != db_read_writer.GetBalanceByAddress(txout.scriptpubkey(), value) )
			{
				ERRORLOG("GetBalanceByAddress  3  failed !!!");
				return -10;
			}
			int64_t amount = value - txout.value();
			if (DBStatus::DB_SUCCESS != db_read_writer.SetBalanceByAddress(txout.scriptpubkey(), amount) )
			{
				ERRORLOG("SetBalanceByAddress  3  failed !!!");
				return -11;
			}
			if (DBStatus::DB_SUCCESS != db_read_writer.RemoveAllTransactionByAddress(txout.scriptpubkey(), tx.hash()) )
			{
				ERRORLOG("RemoveAllTransactionByAddress  5  failed !!!");
				return -12;
			}
			if (DBStatus::DB_SUCCESS != db_read_writer.RemoveUtxoHashsByAddress(txout.scriptpubkey(), tx.hash()) )
			{
				ERRORLOG("RemoveUtxoHashsByAddress  2  failed !!!");
				return -13;
			}
		}

		if (CheckTransactionType(tx) == kTransactionType_Fee)
		{
			int64_t value = 0;
			if (DBStatus::DB_SUCCESS != db_read_writer.GetBalanceByAddress(txOwnerAddr, value) )
			{
				ERRORLOG("GetBalanceByAddress  3  failed !!!");
				return -14;
			}

			signFee += value;

			if (DBStatus::DB_SUCCESS != db_read_writer.SetBalanceByAddress(txOwnerAddr, signFee) )
			{
				ERRORLOG("SetBalanceByAddress  3  failed !!!");
				return -15;
			}
		}
	}

	return 0;
}



int Rollback::RollbackPledgeTx(DBReadWriter &db_read_writer, CTransaction &tx)
{
	DBStatus db_status;
	std::vector<std::string> owner_addrs = TxHelper::GetTxOwner(tx);
	ca_console ResBlockColor(kConsoleColor_Green, kConsoleColor_Black, true);

	// 取质押账户
	std::string addr;
	if (owner_addrs.size() != 0)
	{
		addr = owner_addrs[0]; 
	}
	
	if (CheckTransactionType(tx) == kTransactionType_Tx) 
	{
		if (DBStatus::DB_SUCCESS != db_read_writer.RemovePledgeAddressUtxo(addr, tx.hash()))
		{
			return -33;
		}

		std::vector<std::string> utxoes;
		db_read_writer.GetPledgeAddressUtxo(addr, utxoes); // 无需判断
		
		if (utxoes.size() == 0)
		{
			if (DBStatus::DB_SUCCESS != db_read_writer.RemovePledgeAddresses(addr))
			{
				return -34;
			}
		}

		//vin加
		for (int j = 0; j < tx.vin_size(); j++)
		{
			CTxin txin = tx.vin(j);
			std::string vin_hash = txin.prevout().hash();  //花费的vin
			std::string vin_owner = GetBase58Addr(txin.scriptsig().pub());

			if (DBStatus::DB_SUCCESS != db_read_writer.SetUtxoHashsByAddress(vin_owner, vin_hash ))
			{
				// vin 重复时，若不是解质押产生的utxo，则返回
				std::string txRaw;
				if (DBStatus::DB_SUCCESS != db_read_writer.GetTransactionByHash(vin_hash, txRaw) )
				{
					ERRORLOG("GetTransactionByHash  failed !!!");
					return -17;
				}

				CTransaction vinHashTx;
				vinHashTx.ParseFromString(txRaw);

				nlohmann::json extra = nlohmann::json::parse(vinHashTx.extra());
				std::string txType = extra["TransactionType"];

				if (txType != TXTYPE_REDEEM)
				{
					ERRORLOG("SetUtxoHashsByAddress  failed !!!");
					return -17;
				}
				else
				{
					continue;
				}	
			}

			//vin加余额
			uint64_t amount = TxHelper::GetUtxoAmount(vin_hash, vin_owner);
			int64_t balance = 0;

			db_status = db_read_writer.GetBalanceByAddress(vin_owner, balance);
			if (db_status != DBStatus::DB_SUCCESS)
			{
				ERRORLOG("AddBlock:GetBalanceByAddress");
			}

			balance += amount;
			db_status = db_read_writer.SetBalanceByAddress(vin_owner, balance);
			if (db_status != DBStatus::DB_SUCCESS)
			{
				return -18;
			}
		}

		//vout减
		for (int j = 0; j < tx.vout_size(); j++)
		{
			CTxout txout = tx.vout(j);
			// if(std::find(std::begin(owner_addrs), std::end(owner_addrs), txout.scriptpubkey()) == std::end(owner_addrs))
			
			int64_t value = 0;
			if (txout.scriptpubkey() != VIRTUAL_ACCOUNT_PLEDGE)
			{
				if (DBStatus::DB_SUCCESS != db_read_writer.GetBalanceByAddress(txout.scriptpubkey(), value) )
				{
					ERRORLOG("GetBalanceByAddress  3  failed !!!");
					return -30;
				}
				int64_t amount = value - txout.value();
				if (DBStatus::DB_SUCCESS != db_read_writer.SetBalanceByAddress(txout.scriptpubkey(), amount) )
				{
					ERRORLOG("SetBalanceByAddress  3  failed !!!");
					return -31;
				}

				auto remove_ret = db_read_writer.RemoveUtxoHashsByAddress(txout.scriptpubkey(), tx.hash());
				if (DBStatus::DB_SUCCESS != remove_ret)
				{
					ERRORLOG("RemoveUtxoHashsByAddress failed !!!");
					if(DBStatus::DB_SUCCESS != remove_ret)
					{
						return -15;
					}
				}
				if (DBStatus::DB_SUCCESS != db_read_writer.RemoveAllTransactionByAddress(txout.scriptpubkey(), tx.hash()) )
				{
					ERRORLOG("RemoveAllTransactionByAddress  5  failed !!!");
					return -32;
				}
			}
		}
	}
	else if (CheckTransactionType(tx) == kTransactionType_Fee || CheckTransactionType(tx) == kTransactionType_Award)
	{
		for (int j = 0; j < tx.vout_size(); j++)
		{
			CTxout txout = tx.vout(j);
			int64_t value = 0;
			if (DBStatus::DB_SUCCESS != db_read_writer.GetBalanceByAddress(txout.scriptpubkey(), value) )
			{
				ERRORLOG("GetBalanceByAddress  3  failed !!!");
				return -30;
			}
			int64_t amount = value - txout.value();
			if (DBStatus::DB_SUCCESS != db_read_writer.SetBalanceByAddress(txout.scriptpubkey(), amount) )
			{
				ERRORLOG("SetBalanceByAddress  3  failed !!!");
				return -31;
			}
			if (DBStatus::DB_SUCCESS != db_read_writer.RemoveAllTransactionByAddress(txout.scriptpubkey(), tx.hash()) )
			{
				ERRORLOG("RemoveAllTransactionByAddress  5  failed !!!");
				return -32;
			}
			if (DBStatus::DB_SUCCESS != db_read_writer.RemoveUtxoHashsByAddress(txout.scriptpubkey(), tx.hash()) )
			{
				ERRORLOG("RemoveUtxoHashsByAddress  2  failed !!!");
				return -18;
			}
		}
	}

	return 0;
}


int Rollback::RollbackTx(DBReadWriter &db_read_writer, CTransaction tx)
{
	ca_console ResBlockColor(kConsoleColor_Green, kConsoleColor_Black, true);
	int db_status = 0;
	std::vector<std::string> owner_addrs = TxHelper::GetTxOwner(tx);
	if(CheckTransactionType(tx) == kTransactionType_Tx) 
	{
		for (auto & addr : owner_addrs)
		{
			if (DBStatus::DB_SUCCESS != db_read_writer.RemoveAllTransactionByAddress(addr, tx.hash()) )
			{
				ERRORLOG("RemoveAllTransactionByAddress failed !!!");
				return -1;
			}
		}
		//vin加
		for (int j = 0; j < tx.vin_size(); j++)
		{
			CTxin txin = tx.vin(j);
			std::string vin_hash = txin.prevout().hash();  //花费的vin
			std::string vin_owner = GetBase58Addr(txin.scriptsig().pub());

			if (DBStatus::DB_SUCCESS != db_read_writer.SetUtxoHashsByAddress(vin_owner, vin_hash ))
			{
				// vin 重复时，若不是解质押产生的utxo，则返回
				std::string txRaw;
				if ( 0 != db_read_writer.GetTransactionByHash(vin_hash, txRaw) )
				{
					ERRORLOG("GetTransactionByHash  failed !!!");
					return -2;
				}

				CTransaction vinHashTx;
				vinHashTx.ParseFromString(txRaw);

				nlohmann::json extra = nlohmann::json::parse(vinHashTx.extra());
				std::string txType = extra["TransactionType"];

				if (txType != TXTYPE_REDEEM)
				{
					ERRORLOG("SetUtxoHashsByAddress  failed !!!");
					return -3;
				}
				else
				{
					continue;
				}	
			}

			//vin加余额
			uint64_t amount = TxHelper::GetUtxoAmount(vin_hash, vin_owner);
			int64_t balance = 0;
			db_status = db_read_writer.GetBalanceByAddress(vin_owner, balance);
			if (db_status != DBStatus::DB_SUCCESS)
			{
				ERRORLOG("AddBlock:GetBalanceByAddress");
			}
			balance += amount;
			db_status = db_read_writer.SetBalanceByAddress(vin_owner, balance);
			if (db_status != DBStatus::DB_SUCCESS)
			{
				return -4;
			}
		}
		//vout减
		for (int j = 0; j < tx.vout_size(); j++)
		{
			CTxout txout = tx.vout(j);
			
			int64_t value = 0;
			if (DBStatus::DB_SUCCESS != db_read_writer.GetBalanceByAddress(txout.scriptpubkey(), value) )
			{
				ERRORLOG("GetBalanceByAddress  3  failed !!!");
				return -5;
			}
			int64_t amount = value - txout.value();
			if (DBStatus::DB_SUCCESS != db_read_writer.SetBalanceByAddress(txout.scriptpubkey(), amount) )
			{
				ERRORLOG("SetBalanceByAddress  3  failed !!!");
				return -6;
			}

			if (DBStatus::DB_SUCCESS != db_read_writer.RemoveUtxoHashsByAddress(txout.scriptpubkey(), tx.hash()))
			{
				ERRORLOG("RemoveUtxoHashsByAddress failed !!!");
				return -7;
			}

			// 交易接收方交易记录
			if (owner_addrs.end() == find(owner_addrs.begin(), owner_addrs.end(), txout.scriptpubkey()))
			{
				if (DBStatus::DB_SUCCESS != db_read_writer.RemoveAllTransactionByAddress(txout.scriptpubkey(), tx.hash()) )
				{
					ERRORLOG("RemoveAllTransactionByAddress  5  failed !!!");
					return -8;
				}
			}
		}					
	}	
	else if (CheckTransactionType(tx) == kTransactionType_Fee || CheckTransactionType(tx) == kTransactionType_Award)
	{
		for (int j = 0; j < tx.vout_size(); j++)
		{
			CTxout txout = tx.vout(j);
			int64_t value = 0;
			if (DBStatus::DB_SUCCESS != db_read_writer.GetBalanceByAddress(txout.scriptpubkey(), value) )
			{
				ERRORLOG("GetBalanceByAddress  3  failed !!!");
				return -9;
			}
			int64_t amount = value - txout.value();
			if (DBStatus::DB_SUCCESS != db_read_writer.SetBalanceByAddress(txout.scriptpubkey(), amount) )
			{
				ERRORLOG("SetBalanceByAddress  3  failed !!!");
				return -10;
			}
			if (DBStatus::DB_SUCCESS != db_read_writer.RemoveAllTransactionByAddress(txout.scriptpubkey(), tx.hash()) )
			{
				ERRORLOG("RemoveAllTransactionByAddress  5  failed !!!");
				return -11;
			}
			if (DBStatus::DB_SUCCESS != db_read_writer.RemoveUtxoHashsByAddress(txout.scriptpubkey(), tx.hash()) )
			{
				ERRORLOG("RemoveUtxoHashsByAddress  2  failed !!!");
				return -12;
			}
		}
	}
	return 0;
}



int Rollback::RollbackBlockByBlockHash(DBReadWriter &db_read_writer, const std::string & blockHash)
{
	std::lock_guard<std::mutex> lck(mutex_);

	isRollbacking = true;
    ON_SCOPE_EXIT{
		isRollbacking = false;
    };

    /* 交易数据统计回滚 */
    // 交易
    uint64_t counts{0};
    db_read_writer.GetTxCount(counts);
    counts--;
    db_read_writer.SetTxCount( counts);
    // 燃料费
    counts = 0;
    db_read_writer.GetGasCount(counts);
    counts--;
    db_read_writer.SetGasCount(counts);
    // 额外奖励
    counts = 0;
    db_read_writer.GetAwardCount(counts);
    counts--;
    db_read_writer.SetAwardCount(counts);

    uint64_t top = 0;
    if (DBStatus::DB_SUCCESS != db_read_writer.GetBlockTop(top))
    {
        ERRORLOG("GetBlockTop failed !!!");
        return -2;
    }

    std::string serBlockHeader;
    if (DBStatus::DB_SUCCESS != db_read_writer.GetBlockByBlockHash(blockHash, serBlockHeader) )
    {
        ERRORLOG("GetBlockHeaderByBlockHash failed !!!");
        return -3;
    }

    CBlock cblock;
    cblock.ParseFromString(serBlockHeader);

    std::vector<std::string> blockHashs;
    if (DBStatus::DB_SUCCESS != db_read_writer.GetBlockHashsByBlockHeight(cblock.height(), blockHashs))
    {
        ERRORLOG("GetBlockHashsByBlockHeight failed !!!");
        return -4;
    }

    std::string bestChainHash;
    if (DBStatus::DB_SUCCESS != db_read_writer.GetBestChainHash(bestChainHash) )
    {
        ERRORLOG("GetBestChainHash failed !!!");
        return -5;
    }

	std::string prevBestChainHash;
    if (DBStatus::DB_SUCCESS != db_read_writer.GetBlockHashByBlockHeight(top - 1, prevBestChainHash) )
    {
        ERRORLOG("GetBlockHashByBlockHeight failed !!!");
        return -6;
    }

	if (blockHashs.size() == 0)
	{
		return -7;
	}

    if (blockHashs.size() == 1 && cblock.height() == top)
    {
        // 若当前高度只有一个块，则更新top和bestchain
        if (DBStatus::DB_SUCCESS != db_read_writer.SetBlockTop(--top) )
        {
            ERRORLOG("SetBlockTop failed !!!");
            return -8;
        }

        if (DBStatus::DB_SUCCESS != db_read_writer.SetBestChainHash(prevBestChainHash) )
        {
            ERRORLOG("SetBestChainHash failed !!!");
            return -9;
        }
    }
	else if (blockHashs.size() == 1 && cblock.height() != top)
	{
		return -10;
	}
    
	if (blockHashs.size() > 1 && bestChainHash == cblock.hash())
    {
        // 当前高度不只一个块，且本块是bestchain，更新bestchain
        struct timeval tv;
        gettimeofday( &tv, NULL );
        uint64_t blockTime = tv.tv_sec * 1000000 + tv.tv_usec;

		auto blockHashsIter = std::find(blockHashs.begin(), blockHashs.end(), cblock.hash());
		if (blockHashsIter != blockHashs.end())
		{
			blockHashs.erase(blockHashsIter);
		}

        for (const auto & hash : blockHashs)
        {
            std::string strBlock;
            if (DBStatus::DB_SUCCESS != db_read_writer.GetBlockByBlockHash(hash, strBlock) )
            {
                ERRORLOG("GetBlockByBlockHash failed !!!");
                return -11;
            }

            CBlock tmpBlock;
            tmpBlock.ParseFromString(strBlock);

            if (tmpBlock.time() < blockTime)
            {
                bestChainHash = tmpBlock.hash();
				blockTime = tmpBlock.time();
            }
        }

        if (DBStatus::DB_SUCCESS != db_read_writer.SetBestChainHash(bestChainHash) )
        {
            ERRORLOG("SetBestChainHash failed !!!");
            return -12;
        }
    }

    // 获取块交易类型
    bool isRedeem = false;
    bool isPledge = false;
    std::string redempUtxoStr;

    nlohmann::json blockExtra = nlohmann::json::parse(cblock.extra());
    std::string txType = blockExtra["TransactionType"].get<std::string>();
    if (txType == TXTYPE_PLEDGE)
    {
        isPledge = true;
    }
    else if (txType == TXTYPE_REDEEM)
    {
        isRedeem = true;
        nlohmann::json txInfo = blockExtra["TransactionInfo"].get<nlohmann::json>();
        redempUtxoStr = txInfo["RedeemptionUTXO"].get<std::string>();
    }

    for (int i = 0; i < cblock.txs_size(); i++)
    {
        CTransaction tx = cblock.txs(i);
        std::vector<std::string> owner_addrs = TxHelper::GetTxOwner(tx);
        if (DBStatus::DB_SUCCESS != db_read_writer.DeleteTransactionByHash(tx.hash()) )
        {
			ERRORLOG("DeleteTransactionByHash hash:{} failed",  tx.hash());
            return -13;
        }

        if (DBStatus::DB_SUCCESS != db_read_writer.DeleteBlockHashByTransactionHash(tx.hash()) )
        {
			ERRORLOG("DeleteBlockHashByTransactionHash hash:{} failed",  tx.hash());
            return -14;
        }
        if(CheckTransactionType(tx) == kTransactionType_Tx)
        {
            for(const auto& addr:owner_addrs)
            {
                if (DBStatus::DB_SUCCESS != db_read_writer.RemoveAllTransactionByAddress(addr, tx.hash()) )
                {
					ERRORLOG("RemoveAllTransactionByAddress addr:{}, hash:{} failed", addr, tx.hash());
                    return -15;
                }
            }
        }
    }

    CBlockHeader block;
    std::string serBlock;
    if (DBStatus::DB_SUCCESS != db_read_writer.GetBlockHeaderByBlockHash(blockHash, serBlock) )
    {
		ERRORLOG("GetBlockHeaderByBlockHash failed");
        return -16;
    }

    block.ParseFromString(serBlock);

	uint32_t blockHeight = 0;
	if (DBStatus::DB_SUCCESS != db_read_writer.GetBlockHeightByBlockHash(blockHash, blockHeight))
	{
		ERRORLOG("GetBlockHeightByBlockHash failed");
        return -17;
	}

    if (DBStatus::DB_SUCCESS != db_read_writer.DeleteBlockHeightByBlockHash(blockHash) )
    {
		ERRORLOG("DeleteBlockHeightByBlockHash failed");
        return -18;
    }

    if (DBStatus::DB_SUCCESS != db_read_writer.RemoveBlockHashByBlockHeight(blockHeight, blockHash) )
    {
		ERRORLOG("RemoveBlockHashByBlockHeight failed");
        return -19;
    }

	if (DBStatus::DB_SUCCESS != db_read_writer.DeleteBlockByBlockHash(blockHash))
    {
		ERRORLOG("DeleteBlockByBlockHash failed");
        return -20;
    }

    if (DBStatus::DB_SUCCESS != db_read_writer.DeleteBlockHeaderByBlockHash(blockHash) )
    {
		ERRORLOG("DeleteBlockHeaderByBlockHash failed");
        return -21;
    }

    for (int i = 0; i < cblock.txs_size(); i++)
    {
        CTransaction tx = cblock.txs(i);
        if (isPledge)
        {
            if (0 != RollbackPledgeTx(db_read_writer, tx) )
            {
                return -22;
            }
        }
        else if (isRedeem)
        {
            if (0 != RollbackRedeemTx(db_read_writer, tx))
            {
                return -23;
            }
        }
        else
        {
            int ret = RollbackTx(db_read_writer, tx);
            if( ret != 0)
            {
                return -24;
            }
        }

        // 回滚账号获得的总奖励值和总签名数
        if (CheckTransactionType(tx) == kTransactionType_Award)
        {
            for (auto & txout : tx.vout())
            {
                // 总奖励值
                uint64_t awardTotal = 0;
                if (DBStatus::DB_SUCCESS != db_read_writer.GetAwardTotalByAddress(txout.scriptpubkey(), awardTotal))
                {
					ERRORLOG("GetAwardTotalByAddress failed !");
                    return -25;
                }
                awardTotal = (txout.value() > 0) && (awardTotal > (uint64_t)txout.value()) ? awardTotal - txout.value() : 0;

                if (DBStatus::DB_SUCCESS != db_read_writer.SetAwardTotalByAddress(txout.scriptpubkey(), awardTotal))
                {
					ERRORLOG("SetAwardTotalByAddress failed !");
                    return -26;
                }

                // 总签名数
                uint64_t signSum = 0;
                if (DBStatus::DB_SUCCESS != db_read_writer.GetSignNumByAddress(txout.scriptpubkey(), signSum))
                {
					ERRORLOG("GetSignNumByAddress failed !");
                    return -27;
                }
                signSum = signSum > 0 ? signSum - 1 : 0;

                if (DBStatus::DB_SUCCESS != db_read_writer.SetSignNumByAddress(txout.scriptpubkey(), signSum))
                {
					ERRORLOG("SetSignNumByAddress failed !");
                    return -28;
                }
            }
        }
    }
	DEBUGLOG("The block {} is rollback successful !", blockHash);
    return 0;
}



int Rollback::RollbackToHeight(const unsigned int & height)
{
    std::lock_guard<std::mutex> lock(rocksdb_mutex);
    uint64_t top = 0;
    {
	    DBReader db_read;
        if (db_read.GetBlockTop(top))
        {
		    ERRORLOG("(RollbackToHeight) GetBlockTop failed !");
            return -2;
        }
    }

	DEBUGLOG("RollbackToHeight:{} :{}", height, top);
    if (height >= top)
    {
		ERRORLOG("(RollbackToHeight) height >= top !");
        return -3;
    }
	

    while(top > height)
    {
        DBReadWriter db_read_writer;
		for(int i = 0; i < 10 && top > height; i++)
		{
			std::vector<std::string> blockHashs;
			if (db_read_writer.GetBlockHashsByBlockHeight(top, blockHashs))
			{
				ERRORLOG("(RollbackToHeight) GetBlockHashsByBlockHeight failed!");
				return -3;
			}

			for (const auto & blockHash : blockHashs)
			{
				if ( 0 != RollbackBlockByBlockHash(db_read_writer, blockHash) )
				{
					ERRORLOG("(RollbackToHeight) RollbackBlockByBlockHash failed!");
					return -4;
				}
			}
			// 更新top
			if (db_read_writer.GetBlockTop(top))
			{
				ERRORLOG("(RollbackToHeight) GetBlockTop failed!");
				return -5;
			}
		}
		TRACELOG("(RollbackToHeight) TransactionCommit !!!");
		if(db_read_writer.TransactionCommit())
		{
			ERRORLOG("(RollbackToHeight) TransactionCommit failed!");
			return -29;
		}
    }
    return 0;
}


int Rollback::RollbackBlockBySyncBlock(const uint32_t & conflictHeight, const std::vector<Block> & syncBlocks)
{
	std::lock_guard<std::mutex> lock(rocksdb_mutex);

	if (syncBlocks.size() == 0)
	{
		INFOLOG("(RollbackBlockBySyncBlock) SyncBlocks is empty !");
		return -1;
	}

    DBReadWriter db_read_writer;
	uint64_t top = 0;
	if (0 != db_read_writer.GetBlockTop(top))
	{
		ERRORLOG("(RollbackBlockBySyncBlock) GetBlockTop failed!");
		return -3;
	}

	if (conflictHeight >= top)
	{
		ERRORLOG("(RollbackBlockBySyncBlock) conflictHeight >= top!");
		return -4;
	}

	std::vector<std::pair<uint32_t, std::vector<std::string>>> blockHeightHashs;
	for (const auto & block : syncBlocks)
	{
		CBlock cblock = block.blockheader_;
		if (cblock.height() < conflictHeight)
		{
			continue;
		}

		bool isHeightExist = false;
		for (auto & blockHeightHash : blockHeightHashs)
		{
			if (cblock.height() == blockHeightHash.first)
			{
				isHeightExist = true;
				blockHeightHash.second.push_back(cblock.hash());
			}
		}

		if (!isHeightExist)
		{
			std::vector<std::string> blockHashs{cblock.hash()};
			blockHeightHashs.push_back(std::make_pair(cblock.height(), blockHashs));
		}
	}

	using type_pair = std::pair<uint32_t, std::vector<std::string>>;
	std::sort(blockHeightHashs.begin(), blockHeightHashs.end(), [](type_pair pair1, type_pair pair2){
		return pair1.first > pair2.first;
	});

	for (const auto & blockHeightHash : blockHeightHashs)
	{
		const uint32_t & height = blockHeightHash.first;
		const std::vector<std::string> & syncBlockHashs = blockHeightHash.second;
		if (height > top)
		{
			continue;
		}

		std::vector<std::string> blockHashs;
		if (0 != db_read_writer.GetBlockHashsByBlockHeight(height, blockHashs))
		{
			ERRORLOG("(RollbackBlockBySyncBlock) GetBlockHashsByBlockHeight failed!");
			return -5;
		}

		for (const std::string & blockHash : blockHashs)
		{
			auto iter = find(syncBlockHashs.begin(), syncBlockHashs.end(), blockHash);
			if (iter == syncBlockHashs.end())
			{
				if ( 0 != RollbackBlockByBlockHash(db_read_writer, blockHash) )
				{
					ERRORLOG("(RollbackBlockBySyncBlock) RollbackBlockByBlockHash failed!");
					return -6;
				}
			}
		}
	}

	INFOLOG("(RollbackToHeight) TransactionCommit !!!");
    if (db_read_writer.TransactionCommit())
    {
		ERRORLOG("(RollbackBlockBySyncBlock) TransactionCommit failed!");
        return -29;
    }

	return 0;
}

