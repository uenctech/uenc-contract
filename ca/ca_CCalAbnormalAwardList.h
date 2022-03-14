#ifndef _CA_CCALABNORMALAWARDLIST_H_
#define _CA_CCALABNORMALAWARDLIST_H_

#include "ca_blockcache.h"

class CCalAbnormalAwardList : public CCalBlockCacheInterface
{
public:
    int Process(const std::map<uint64_t, std::set<CBlock, CBlockCompare>> & cache);
    void GetAbnormalList(std::vector<std::string>& abnormal_addr_list);

private:
    std::vector<std::string> abnormal_addr_list;
};

#endif