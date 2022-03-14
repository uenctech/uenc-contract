
#include <algorithm>
#include <iterator>
#include <pthread.h>

#include "ca_algorithm.h"
#include "ca_transaction.h"
#include "ca_global.h"
#include "ca_hexcode.h"
#include "ca_blockpool.h"
#include "ca_console.h"
#include "ca_synchronization.h"
#include "ca_base58.h"
#include "ca_rollback.h"
#include "db/db_api.h"
#include "ca_block_http_callback.h"
#include "MagicSingleton.h"
#include "../include/logging.h"
#include "../include/ScopeGuard.h"
#include "ca/ca_transaction.h"
#include "ca_blockcache.h"

extern int VerifyBuildBlock(const CBlock & cblock);

bool BlockPoll::CheckConflict(const CBlock& block1, const CBlock& block2)
{
	std::vector<std::string> v1;
	{
		CTransaction tx = block1.txs(0);
		for(int j = 0; j < tx.vin_size(); j++)
		{
			CTxin vin = tx.vin(j);
			std::string hash = vin.prevout().hash();
			if(hash.size() > 0)
			{
				v1.push_back(hash + "_" + GetBase58Addr(vin.scriptsig().pub()));
			}
		}
	}

	std::vector<std::string> v2;
	{
		CTransaction tx = block2.txs(0);
		for(int j = 0; j < tx.vin_size(); j++)
		{
			CTxin vin = tx.vin(j);
			std::string hash = vin.prevout().hash();
			if(hash.size() > 0)
			{
				v2.push_back(hash + "_" + GetBase58Addr(vin.scriptsig().pub()));
			}
		}
	}

    std::sort(v1.begin(), v1.end());
    std::sort(v2.begin(), v2.end());
 
    std::vector<std::string> v_intersection;
 
    std::set_intersection(v1.begin(), v1.end(),
                          v2.begin(), v2.end(),
                          std::back_inserter(v_intersection));
	return v_intersection.size() > 0 ;
}


bool BlockPoll::CheckConflict(const CBlock& block)
{
	for (auto it = blocks_.begin(); it != blocks_.end(); ++it) 
	{
		auto &curr_block = *it;
		bool ret = CheckConflict(curr_block.blockheader_, block);
		if(ret)   //有冲突
		{
			return true;
		}
	}
	return false;
}
void BlockPoll::AddSyncBlock(const std::vector<CBlock> &blocks, const std::vector<std::string> &rollback_blocks_hash)
{
    std::lock_guard<std::mutex> lck(mu_block_);
    for(auto block : blocks)
    {
        sync_blocks_.push_back(Block{block, true});
    }
    for(auto hash : rollback_blocks_hash)
    {
        rollback_blocks_hash_.push_back(hash);
    }
}
bool BlockPoll::Add(const Block& block)
{
    if( block.blockheader_.height() <= 0 ||  block.blockheader_.hash().empty())
    {
        return false;
    }
	std::lock_guard<std::mutex> lck(mu_block_);

	if(block.isSync_)
	{
		//INFOLOG("BlockPoll::Add sync=====height:{}, hash:{}", block.blockheader_.height(), block.blockheader_.hash().substr(0,HASH_LEN));

		//sync_blocks_.push_back(block);
		return false;
	}
	CBlock blockheader = block.blockheader_;

	if(!VerifyHeight(blockheader))
	{
		ERRORLOG("BlockPoll:VerifyHeight fail!!");
		return false;
	}  

	//检查utxo是否有冲突
	for (auto it = blocks_.begin(); it != blocks_.end(); ++it) 
	{
		auto &curr_block = *it;
		bool ret = CheckConflict(curr_block.blockheader_, blockheader);
		if(ret)   //有冲突
		{
			ERRORLOG("BlockPoll::Add====has conflict");
			if(curr_block.blockheader_.time() < blockheader.time())   //预留块中的早
			{
				return false;
			}
			else
			{     //预留块中的晚
				it = blocks_.erase(it);
				blocks_.push_back(block);
				return true;
			}
		}
	}
	INFOLOG("BlockPoll::Add sync=====height:{}, hash:{}", blockheader.height(), blockheader.hash().substr(0,HASH_LEN));
	blocks_.push_back(block);
	return true;
}



void BlockPoll::Process()
{
    if(processing_)
    {
        DEBUGLOG("BlockPoll::Process is processing_");
        return;
    }
    processing_ = true;
    std::lock_guard<std::mutex> lck(mu_block_);

    DBReader reader;
    uint64_t top = 0;
    reader.GetBlockTop(top);
    ON_SCOPE_EXIT{
        DBReader readerTop;
        uint64_t newTop = 0;
        if (readerTop.GetBlockTop(newTop) == DBStatus::DB_SUCCESS)
        {
            if (top != newTop)
            {
                NotifyNodeHeightChange();
                TRACELOG("BlockPoll NotifyNodeHeightChange update ok.");
            }
        }
	};

    for(auto &hash : rollback_blocks_hash_)
    {
        auto ret = ca_algorithm::RollBackByHash(hash);
        DEBUGLOG("rollback:{}:ret:{}",hash, ret);
        if(0 != ret)
        {
            return;
        }
        MagicSingleton<CBlockCache>::GetInstance()->Remove(hash);
    }
    rollback_blocks_hash_.clear();

    for (auto &block : sync_blocks_)
    {
        DBReadWriter db_writer;
        auto ret = ca_algorithm::VerifyBlock(block.blockheader_, false, &db_writer);
        if (0 != ret)
        {
            ERRORLOG("verify block fail ret:{}:{}:{}", ret, block.blockheader_.height(), block.blockheader_.hash());
            continue;
        }
        ret = ca_algorithm::SaveBlock(db_writer, block.blockheader_);
        INFOLOG("save block ret:{}:{}:{}", ret, block.blockheader_.height(), block.blockheader_.hash());
        if (0 != ret)
        {
            continue;
        }
        if(DBStatus::DB_SUCCESS == db_writer.TransactionCommit())
        {
            for (int i = 0; i < block.blockheader_.txs_size(); i++)
            {
                CTransaction tx = block.blockheader_.txs(i);
                if (CheckTransactionType(tx) == kTransactionType_Tx)
                {
                    std::vector<std::string> txOwnerVec;
                    StringUtil::SplitString(tx.owner(), txOwnerVec, "_");
                    int result = MagicSingleton<TxVinCache>::GetInstance()->Remove(tx.hash(), txOwnerVec);
                    if (result == 0)
                    {
                        std::cout << "Remove pending transaction in Cache " << tx.hash() << " from ";
                        for_each(txOwnerVec.begin(), txOwnerVec.end(), [](const string& owner){ cout << owner << " "; });
                        std::cout << std::endl;
                    }
                }
            }
            Singleton<PeerNode>::get_instance()->set_self_height();
            if (Singleton<Config>::get_instance()->HasHttpCallback())
            {
                if (MagicSingleton<CBlockHttpCallback>::GetInstance()->IsRunning())
                {
                    MagicSingleton<CBlockHttpCallback>::GetInstance()->AddBlock(block.blockheader_);
                }
            }
            MagicSingleton<CBlockCache>::GetInstance()->Add(block.blockheader_);
        }
    }
    sync_blocks_.clear();

    for (auto it = blocks_.begin(); it != blocks_.end(); )
    {
        auto &block = *it;
        if(time(NULL) - block.time_ >= PROCESS_TIME)
        {
            block.time_ = time(NULL);
            pending_block_.push_back(std::move(block));
            it = blocks_.erase(it);
        }
        else
        {
            ++it;
        }
    }

    if(pending_block_.size() == 0)
    {
        processing_ = false;
        return;
    }
    std::sort(pending_block_.begin(), pending_block_.end(), [](const Block & b1, const Block & b2){
        if (b1.blockheader_.height() < b2.blockheader_.height())
        {
            return true;
        }
        else
        {
            return false;
        }
    });
    DBReader db_reader;
    for (auto it = pending_block_.begin(); it != pending_block_.end(); it = pending_block_.erase(it))
    {
        auto &block = *it;
        std::string strTempHeader;

        DBStatus status = db_reader.GetBlockByBlockHash(block.blockheader_.hash(), strTempHeader);
        if (status != DBStatus::DB_SUCCESS && status != DBStatus::DB_NOT_FOUND)
        {
            ERRORLOG("get block not success or not found ");
            return;
        }

        if (strTempHeader.size() != 0)
        {
            continue;
        }

        std::string strPrevHeader;
        status = db_reader.GetBlockByBlockHash(block.blockheader_.prevhash(), strPrevHeader);
        if (status != DBStatus::DB_SUCCESS && status != DBStatus::DB_NOT_FOUND)
        {
            ERRORLOG("get block not success or not found ");
            return;
        }

        if (strPrevHeader.size() == 0)
        {
            continue;
        }
        //过期
        if(time(NULL) - block.time_ >= PENDING_EXPIRE_TIME)
        {
            continue;
        }

        DBReadWriter db_writer;
        auto ret = ca_algorithm::VerifyBlock(block.blockheader_, true, &db_writer);
        if (0 != ret)
        {
            ERRORLOG("verify block fail ret:{}:{}:{}", ret, block.blockheader_.height(), block.blockheader_.hash());
            continue;
        }
        ret = ca_algorithm::SaveBlock(db_writer, block.blockheader_);
        INFOLOG("save block ret:{}:{}:{}", ret, block.blockheader_.height(), block.blockheader_.hash());
        if (0 != ret)
        {
            continue;
        }
        if(DBStatus::DB_SUCCESS == db_writer.TransactionCommit())
        {
            for (int i = 0; i < block.blockheader_.txs_size(); i++)
            {
                CTransaction tx = block.blockheader_.txs(i);
                if (CheckTransactionType(tx) == kTransactionType_Tx)
                {
                    std::vector<std::string> txOwnerVec;
                    StringUtil::SplitString(tx.owner(), txOwnerVec, "_");
                    int result = MagicSingleton<TxVinCache>::GetInstance()->Remove(tx.hash(), txOwnerVec);
                    if (result == 0)
                    {
                        std::cout << "Remove pending transaction in Cache " << tx.hash() << " from ";
                        for_each(txOwnerVec.begin(), txOwnerVec.end(), [](const string& owner){ cout << owner << " "; });
                        std::cout << std::endl;
                    }
                }
            }
            Singleton<PeerNode>::get_instance()->set_self_height();
            if (Singleton<Config>::get_instance()->HasHttpCallback())
            {
                if (MagicSingleton<CBlockHttpCallback>::GetInstance()->IsRunning())
                {
                    MagicSingleton<CBlockHttpCallback>::GetInstance()->AddBlock(block.blockheader_);
                }
            }
            MagicSingleton<CBlockCache>::GetInstance()->Add(block.blockheader_);
        }
    }
    processing_ = false;


}


std::string BlockPoll::GetBlock()
{
	std::lock_guard<std::mutex> lck(mu_block_);
	std::vector<std::string> ser_block;
	for (auto &block:blocks_) 
	{
        ser_block.push_back(block.blockheader_.SerializeAsString());
	}
    
    size_t size = ser_block.size();
	std::string blocks;
    for(size_t i = 0; i < size; i++)
    {
        blocks += Str2Hex(ser_block[i]);
        blocks += "_";
    }
    return blocks;
}

bool BlockPoll::VerifyHeight(const CBlock& block)
{
	DBReader db_reader;

	uint64_t ownblockHeight = 0;
	if (DBStatus::DB_SUCCESS != db_reader.GetBlockTop(ownblockHeight))
	{
		ERRORLOG("get block top failed");
		return false;
	}

	unsigned int preheight = 0;
	if (DBStatus::DB_SUCCESS != db_reader.GetBlockHeightByBlockHash(block.prevhash(), preheight))
	{
		ERRORLOG("get block height failed");
		return false;
	}

	if(ownblockHeight > (preheight + 5))
	{
		return false;
	}
	return true;
}
