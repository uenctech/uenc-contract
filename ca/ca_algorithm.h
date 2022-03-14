#ifndef UENC_CA_ALGORITHM_H_
#define UENC_CA_ALGORITHM_H_

#include "ca_global.h"
#include "db/db_api.h"
#include "proto/block.pb.h"

namespace ca_algorithm
{
//获取1000高度内的异常账号
int GetAbnormalAwardAddrList(uint64_t block_height, std::vector<std::string> &abnormal_addr_list, DBReader *db_reader_ptr = nullptr);

//根据地址获取质押额度满500的质押交易的时间（纳秒）
//当返回值小于0 代表函数执行失败
//等于0代表未质押
//大于0代表质押时间
int64_t GetPledgeTimeByAddr(const std::string &addr, PledgeType pledge_type, DBReader *db_reader_ptr = nullptr);

//根据高度以及单位时间获取500高度以下的单位时间内区块数量
int GetBlockNumInUnitTime(uint64_t block_height, DBReader *db_reader_ptr = nullptr, uint64_t unit_time = 5 * 60 * 1000000);

//根据区块数量以及使用的总奖励获取区块总奖励（最小单位）
int64_t GetBlockTotalAward(uint32_t block_num, DBReader *db_reader_ptr = nullptr);

//根据地址获取账号的总奖励金额与签名次数
int GetAwardAmountAndSignCntByAddr(const std::string &addr, uint64_t block_height, uint64_t pledge_time, uint64_t tx_time, uint64_t &addr_award_total, uint32_t &sign_cnt, DBReader *db_reader_ptr = nullptr);

//获取所有地址的分配奖励
int GetAwardList(uint64_t block_height, uint64_t tx_time, const std::vector<std::string> &addrs,
                 std::vector<std::pair<std::string, uint64_t>> &award_list, DBReader *db_reader_ptr = nullptr);

std::string CalcTransactionHash(CTransaction tx);

std::string CalcBlockHash(CBlock block);

//校验交易
int MemVerifyTransaction(const CTransaction &tx);

//校验交易
int VerifyTransaction(const CTransaction &tx, uint64_t tx_height, bool verify_abnormal = true, DBReader *db_reader_ptr = nullptr);

//校验区块
int MemVerifyBlock(const CBlock &block);

//校验区块
int VerifyBlock(const CBlock &block, bool verify_abnormal = true, DBReader *db_reader_ptr = nullptr);

//保存区块 ADD
int SaveBlock(DBReadWriter &db_writer, const CBlock &block);

//调用的时候注意高度不要与最高高度相差太多，内存占用太大，进程容易被杀
//回滚到指定高度
int RollBackToHeight(uint64_t height);
//回滚指定hash
int RollBackByHash(const std::string &block_hash);

void PrintTx(const CTransaction &tx);
void PrintBlock(const CBlock &block);

}; // namespace ca_algorithm

#endif
