#ifndef _CA_CCALBLOCKAWARD_H_
#define _CA_CCALBLOCKAWARD_H_

#include "ca_blockcache.h"

class CCalBlockAward : public CCalBlockCacheInterface
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
            start = cache.begin()->first;
            end = start + 500;
        }

        std::vector<std::string> block_hashes;
        if (DBStatus::DB_SUCCESS != db_reader.GetBlockHashesByBlockHeight(start, end, block_hashes)) 
        {
            DEBUGLOG("(CCalBlockAward) GetBlockHashesByBlockHeight failed"); 
            return -1;
        }
        std::vector<std::string> blocks;
        if (DBStatus::DB_SUCCESS != db_reader.GetBlocksByBlockHash(block_hashes, blocks)) 
        {
            DEBUGLOG("(CCalBlockAward) GetBlocksByBlockHash failed");
            return -1;
        }   

        for(auto const & block_str : blocks)
        {
            CBlock block;
            block.ParseFromString(block_str);
            int len = block.txs_size();
            for (int j = 0; j < len; j++) 
            {
                CTransaction tx = block.txs(j);
                if(CheckTransactionType(tx) == kTransactionType_Award)
                {
                    for (int k = 0; k < tx.vout_size(); k++)
                    {
                        CTxout txout = tx.vout(k);
                        std::string account = txout.scriptpubkey();
                        auto iter = award_map.find(account);
                        if(iter == award_map.end())
                        {
                            award_map[txout.scriptpubkey()] = std::make_pair(0, 0);     
                        }
                        auto &value = award_map.at(account);
                        value.first += 1;
                        value.second += txout.value(); 
                    } 
                }
            }
        }
        return 0;
    }
    void GetAwardMap(std::map<std::string , std::pair<uint64_t,uint64_t>> &award_map)
    {
        award_map = this->award_map;
    }
private:
    std::map<std::string , std::pair<uint64_t,uint64_t>> award_map; //pair(sign count, total award)
};



#endif // !_CA_CCALBLOCKAWARD_H_
