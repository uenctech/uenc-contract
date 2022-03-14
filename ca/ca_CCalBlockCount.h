#ifndef _CA_CCALBLOCKCOUNT_H_
#define _CA_CCALBLOCKCOUNT_H_

#include "ca_blockcache.h"

class CCalBlockCount : public CCalBlockCacheInterface
{
public:
    int Process(const std::map<uint64_t, std::set<CBlock, CBlockCompare>> & cache)
    {
        DBReader db_reader;
        uint64_t start, end;
        uint16_t cache_len = cache.size();

        if(cache_len < 500){
            start = 0;
            if(DBStatus::DB_SUCCESS != db_reader.GetBlockTop(end))
            {
                ERRORLOG("(CCalBlockAward) GetBlockTop failed !");
                return -1;
            }
        }
        else
        {
            end = (++(cache.end()))->first;
            start = end - 500;;
        }

        std::vector<std::string> block_hashes;
        if (DBStatus::DB_SUCCESS != db_reader.GetBlockHashesByBlockHeight(start, end, block_hashes)) 
        {
            ERRORLOG("(CCalBlockAward) GetBlockHashesByBlockHeight failed"); 
            return -1;
        }
        std::vector<std::string> blocks;
        if (DBStatus::DB_SUCCESS != db_reader.GetBlocksByBlockHash(block_hashes, blocks)) 
        {
            ERRORLOG("(CCalBlockAward) GetBlocksByBlockHash failed"); 
            return -1;
        }  

        uint64_t result = 0;
        for(auto const & block_str : blocks)
        {
            CBlock block;
            block.ParseFromString(block_str);
            int len = block.txs_size();
            for (int j = 0; j < len; j++) 
            {
                CTransaction tx = block.txs(j);
                if(CheckTransactionType(tx) == kTransactionType_Tx)
                {
                   result += 1;
                }
            }
        }

        count = result;
        return 0;
    }
    uint64_t GetCount()
    {
        return count;
    }
private:
    uint64_t count;

};

#endif // !_CA_CCalBlockCount_H_