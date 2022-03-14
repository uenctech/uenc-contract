#ifndef __CA_ROLLBACK_H__
#define __CA_ROLLBACK_H__

#include <iostream>
#include <memory>
#include <mutex>
#include "ca_blockpool.h"
#include "transaction.pb.h"
#include "db/db_api.h"

class Rollback 
{
public:
    Rollback() = default;
    ~Rollback() = default;

    int RollbackBlockByBlockHash(DBReadWriter &db_read_writer, const std::string & blockHash);
    int RollbackToHeight(const unsigned int & height);
    int RollbackBlockBySyncBlock(const uint32_t & conflictHeight, const std::vector<Block> & syncBlocks);

private:
    int RollbackTx(DBReadWriter &db_read_writer, CTransaction tx);
    int RollbackPledgeTx(DBReadWriter &db_read_writer, CTransaction &tx);
    int RollbackRedeemTx(DBReadWriter &db_read_writer, CTransaction &tx);

public:
    bool isRollbacking = false;

private:
    std::mutex mutex_;
};

#endif // !__CA_ROLLBACK_H__
