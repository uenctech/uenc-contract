#include "ca_blockcache.h"
#include "../db/db_api.h"
#include "../proto/block.pb.h"
#include "include/logging.h"

CBlockCache::CBlockCache()
{
    construct_success = CacheInit();
}

bool CBlockCache::CacheInit()
{
    DBReader db_reader;
    uint64_t block_height;
    if(DBStatus::DB_SUCCESS != db_reader.GetBlockTop(block_height))
    {
        ERRORLOG("CBlockCache init failed, can't GetBlockTop");
        return false;
    }

    uint64_t end_height = block_height;
    uint64_t start_height = block_height > 1000 ? block_height - 1000 : 1;
    std::vector<std::string> block_hashes;
    if (DBStatus::DB_SUCCESS != db_reader.GetBlockHashesByBlockHeight(start_height, end_height, block_hashes))
    {
        ERRORLOG("CBlockCache init failed, can't GetBlockHashesByBlockHeight");
        return false;
    }

    std::vector<std::string> blocks;
    if (DBStatus::DB_SUCCESS != db_reader.GetBlocksByBlockHash(block_hashes, blocks))
    {
        ERRORLOG("CBlockCache init failed, can't GetBlocksByBlockHash");
        return false;
    }

    for(const auto & block_str : blocks)
    {
        CBlock block;
        block.ParseFromString(block_str);
        CacheAdd(block);
    }
    return true;
}

int CBlockCache::Add(const CBlock & block)
{
    std::lock_guard<std::mutex> lock(cache_mutex);
    if(!construct_success){
        return -1;
    } 
    CacheAdd(block);
    if(cache.size() > 1000)
    {
        CacheRemove(cache.begin()->first);
    }

    return 0;
}

void CBlockCache::CacheAdd(const CBlock & block)
{
    uint64_t height = block.height();
    auto iter = cache.find(height);
    if(iter == cache.end())
    {
        cache[height] = std::set<CBlock, CBlockCompare>{};
    }
    auto &value = cache.at(height);
    value.insert(block);

}

int CBlockCache::Remove(const std::string & block_hash)
{
    std::lock_guard<std::mutex> lock(cache_mutex);
    if(!construct_success){
        return -1;
    }
    DBReader db_reader;
    unsigned int height;
    if (DBStatus::DB_SUCCESS != db_reader.GetBlockHeightByBlockHash(block_hash, height))
    {
        ERRORLOG("fail to add block cache, can't GetBlockHashByBlockHeight");
    }
    CacheRemove(height + 1);

    uint64_t block_height = cache.begin()->first-1;
    std::vector<std::string> block_hashes;
    if (DBStatus::DB_SUCCESS != db_reader.GetBlockHashsByBlockHeight(block_height,block_hashes))
    {
        ERRORLOG("CBlockCache init failed, can't GetBlockHashsByBlockHeight");
        return -1;
    }
    std::vector<std::string> blocks;
    if (DBStatus::DB_SUCCESS != db_reader.GetBlocksByBlockHash(block_hashes, blocks))
    {
        ERRORLOG("CBlockCache init failed, can't GetBlocksByBlockHash");
        return -1;
    }

    for(const auto & block_str : blocks)
    {
        CBlock block;
        block.ParseFromString(block_str);
        CacheAdd(block);
    }
    
    return 0;
}

void CBlockCache::CacheRemove(uint64_t block_height)
{
    auto iter = cache.find(block_height);
    if(iter != cache.end())
    {
        cache.erase(iter);
    }
    else
    {
        INFOLOG("not found block height {} in the cache",block_height);
    }
}

int CBlockCache::Calculate(CCalBlockCacheInterface & cal) const
{
    if(!construct_success){
        return -1;
    }
    cal.Process(this->cache);
    return 0;
}

 void CBlockCache::GetCache(std::map<uint64_t, std::set<CBlock, CBlockCompare>>& block_cache)
 {
    block_cache = cache;
 }
