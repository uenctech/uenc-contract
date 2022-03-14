#include "ca_CCalAbnormalAwardList.h"
#include "ca_transaction.h"

int CCalAbnormalAwardList::Process(const std::map<uint64_t, std::set<CBlock, CBlockCompare>> & cache)
{
    std::unordered_map<std::string, std::pair<uint64_t, uint64_t>> addr_sign_cnt_and_award_amount;
    
    for (const auto &block_cache : cache)
    {
        for (const auto &block : block_cache.second)
        {
            for (auto &tx : block.txs())
            {
                if (CheckTransactionType(tx) != kTransactionType_Award)
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

void CCalAbnormalAwardList::GetAbnormalList(std::vector<std::string>& abnormal_addr_list)
{
    abnormal_addr_list = this->abnormal_addr_list;
}