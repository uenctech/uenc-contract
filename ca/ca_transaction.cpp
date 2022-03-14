#include "ca_transaction.h"

#include <assert.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>

#include <iostream>
#include <set>
#include <algorithm>
#include <shared_mutex>
#include <mutex>

#include "getmac.pb.h"
#include "interface.pb.h"
#include "proto/ca_protomsg.pb.h"

#include "Crypto_ECDSA.h"
#include "db/db_api.h"
#include "net/node_cache.h"
#include "common/devicepwd.h"
#include "common/config.h"
#include "utils/time_util.h"
#include "utils/base64.h"
#include "utils/string_util.h"
#include "include/logging.h"
#include "include/net_interface.h"
#include "ca.h"
#include "ca_message.h"
#include "ca_hexcode.h"
#include "ca_buffer.h"
#include "ca_serialize.h"
#include "ca_util.h"
#include "ca_global.h"
#include "ca_coredefs.h"
#include "ca_hexcode.h"
#include "ca_interface.h"
#include "ca_clientinfo.h"
#include "ca_clientinfo.h"
#include "ca_clientinfo.h"
#include "ca_console.h"
#include "ca_device.h"
#include "ca_header.h"
#include "ca_sha2.h"
#include "ca_base64.h"
#include "ca_pwdattackchecker.h"
#include "ca_blockpool.h"
#include "ca_AwardAlgorithm.h"
#include "ca_txvincache.h"
#include "ca_txconfirmtimer.h"
#include "ca_block_http_callback.h"
#include "ca/ca_algorithm.h"
#include "ca/ca_black_list_cache.h"
#include "ca_pledgecache.h"
#include "ca/ca_blockcache.h"

static std::mutex s_ResMutex;
std::shared_mutex MinutesCountLock;
static const int REASONABLE_HEIGHT_RANGE = 10;

template<typename Ack> void ReturnAckCode(const MsgData& msgdata, std::map<int32_t, std::string> errInfo, Ack & ack, int32_t code, const std::string & extraInfo = "");
template<typename TxReq> int CheckAddrs( const std::shared_ptr<TxReq>& req);

int StringSplit(std::vector<std::string>& dst, const std::string& src, const std::string& separator)
{
    if (src.empty() || separator.empty())
        return 0;

    int nCount = 0;
    std::string temp;
    size_t pos = 0, offset = 0;

    // 分割第1~n-1个
    while((pos = src.find_first_of(separator, offset)) != std::string::npos)
    {
        temp = src.substr(offset, pos - offset);
        if (temp.length() > 0)
		{
            dst.push_back(temp);
            nCount ++;
        }
        offset = pos + 1;
    }

    // 分割第n个
    temp = src.substr(offset, src.length() - offset);
    if (temp.length() > 0)
	{
        dst.push_back(temp);
        nCount ++;
    }

    return nCount;
}



int GetBalanceByUtxo(const std::string & address,uint64_t &balance)
{
	if (address.size() == 0)
	{
		return -1;
	}

    DBReader db_reader;
	std::vector<std::string> addr_utxo_hashs;
	db_reader.GetUtxoHashsByAddress(address, addr_utxo_hashs);
	
	uint64_t total = 0;
	std::string txRaw;
	CTransaction tx;
	for (auto utxo_hash : addr_utxo_hashs)
	{
		if (DBStatus::DB_SUCCESS != db_reader.GetTransactionByHash(utxo_hash, txRaw))
		{
			return -2;
		}
		if (!tx.ParseFromString(txRaw))
		{
			return -3;
		}
		for (auto &vout : tx.vout())
		{
			if (vout.scriptpubkey() == address)
			{
				total += vout.value();
			}
		}
	}
 	balance = total;
	return 0;
}

TransactionType CheckTransactionType(const CTransaction& tx)
{
	if( tx.time() == 0 || tx.hash().length() == 0 || tx.vin_size() == 0 || tx.vout_size() == 0)
	{
		return kTransactionType_Unknown;
	}

	CTxin txin = tx.vin(0);
	if ( txin.scriptsig().sign() == std::string(FEE_SIGN_STR))
	{
		return kTransactionType_Fee;
	}
	else if (txin.scriptsig().sign() == std::string(EXTRA_AWARD_SIGN_STR))
	{
		return kTransactionType_Award;
	}

	return kTransactionType_Tx;
}

bool checkTop(int top)
{
    DBReader db_reader;
	uint64_t mytop = 0;
	db_reader.GetBlockTop(mytop);

	if(top < (int)mytop - 4 )
	{
		ERRORLOG("checkTop fail other top:{} my top:{}", top, (int)mytop);
		return false;
	}
	else if(top > (int)mytop + 1)
	{
		ERRORLOG("checkTop fail other top:{} my top:{}", top, (int)mytop);
		return false;
	}
	else
	{
		return true;
	}
}

// Check the out of transaction
static bool IsOutDuplicate(const CTransaction& tx)
{
	vector<string> outAccount;
	for (auto i = 0; i < tx.vout_size(); i++)
	{
		outAccount.push_back(tx.vout(i).scriptpubkey());
	}		
	set<string> accounts(outAccount.begin(), outAccount.end());
	if (accounts.size() != outAccount.size())
	{
		ERRORLOG("IsOutDuplicate found duplicate out!");
		return true;
	}

	return false;
}

// Create: Strict check the transaction,  20210926  Liu
int StrictCheckTransaction(const CTransaction& tx)
{
	// Check version
	if (tx.version() != 1)
	{
		ERRORLOG("StrictCheckTransaction version wrong!");
		return -1;
	}
	
	// Check hash, length is 64
	if (tx.hash().empty() || tx.hash().size() != 64)
	{
		ERRORLOG("StrictCheckTransaction hash is empty!");
		return -2;
	}

	if (tx.vin_size() < 1)
	{
		return -3;
	}
	if (tx.vout_size() < 1)
	{
		return -4;
	}

	// Check output account
	for (auto i = 0; i < tx.vout_size(); i++)
	{
		const string& addr = tx.vout(i).scriptpubkey();
		if (addr.size() != 33 && addr.size() != 34)
		{
			ERRORLOG("StrictCheckTransaction owner is not 33 or 34!");
			return -5;
		}

		if (addr != VIRTUAL_ACCOUNT_PLEDGE && !CheckBase58Addr(addr))
		{
			ERRORLOG("StrictCheckTransaction owner base58 failed!");
			return -6;
		}
	}

	// Check time, nanosecond.
	const uint64_t MIN_TIME_NS = 1600000000000000;
	if (tx.time() < MIN_TIME_NS)
	{
		ERRORLOG("StrictCheckTransaction tx time is wrong!");
		return -7;
	}

	if (CheckTransactionType(tx) == kTransactionType_Tx)
	{
		// Check owner account
		std::vector<std::string> txOwners;
		StringUtil::SplitString(tx.owner(), txOwners, "_");
		if (txOwners.empty())
		{
			ERRORLOG("StrictCheckTransaction owner is empty!");
			return -8;
		}
		if (txOwners.size() != 1)
		{
			ERRORLOG("StrictCheckTransaction size greater 1");
			return -9;
		}
		std::vector<std::string> txGetOwners = TxHelper::GetTxOwner(tx);
		if (txOwners != txGetOwners)
		{
			ERRORLOG("StrictCheckTransaction owner not equal!");
			return -10;
		}
		for (const auto& owner : txOwners)
		{
			if (owner.size() != 33 && owner.size() != 34)
			{
				ERRORLOG("StrictCheckTransaction owner is not 33 or 34!");
				return -11;
			}

			if (!CheckBase58Addr(owner))
			{
				ERRORLOG("StrictCheckTransaction owner base58 failed!");
				return -12;
			}
		}

		// Check extra
		if (tx.extra().empty())
		{
			ERRORLOG("StrictCheckTransaction extra is empty!");
			return -13;
		}
		std::string txType;
		uint32_t needVerifyPreHashCount;
		uint64_t signFee;
		uint64_t packageFee;
		try
		{
			nlohmann::json extra = nlohmann::json::parse(tx.extra());
			txType = extra["TransactionType"].get<std::string>();
			needVerifyPreHashCount = extra["NeedVerifyPreHashCount"].get<uint32_t>();
			signFee = extra["SignFee"].get<uint64_t>();
			packageFee = extra["PackageFee"].get<uint64_t>();
		}
		catch(const std::exception& e)
		{
			ERRORLOG("StrictCheckTransaction {}", e.what());
			return -14;
		}
		if (signFee < g_minSignFee || signFee > g_maxSignFee)
		{
			ERRORLOG("StrictCheckTransaction sign fee is wrong!");
			return -15;
		}
		if (needVerifyPreHashCount < g_MinNeedVerifyPreHashCount)
		{
			ERRORLOG("StrictCheckTransaction needVerifyPreHashCount is less than 6!");
			return -16;
		}
		if (txType != string(TXTYPE_TX) && txType != string(TXTYPE_PLEDGE) && txType != string(TXTYPE_REDEEM))
		{
			ERRORLOG("StrictCheckTransaction tx type is wrong!");
			return -17;
		}

		// Check out account duplicate
		if (txType == string(TXTYPE_TX))
		{
			if (IsOutDuplicate(tx))
			{
				ERRORLOG("StrictCheckTransaction found duplicate out!");
				return -18;
			}
		}

		// Check pledge
		if (txType == string(TXTYPE_PLEDGE))
		{
			if (tx.vout_size() != 2)
			{
				return -19;
			}
			bool findVirtual = false;
			bool findOwner = false;
			for (int i = 0; i < tx.vout_size(); i++)
			{
				if (tx.vout(i).scriptpubkey() == VIRTUAL_ACCOUNT_PLEDGE)
				{
					findVirtual = true;
				}
				if (tx.vout(i).scriptpubkey() == txOwners[0])
				{
					findOwner = true;
				}
			}
			if (!findVirtual || !findOwner)
			{
				return -20;
			}
		}

		// Check redeem
		if (txType == string(TXTYPE_REDEEM))
		{
			if (tx.vout_size() != 2)
			{
				return -21;
			}
			if (tx.vout(0).scriptpubkey() != tx.vout(1).scriptpubkey())
			{
				return -22;
			}
			if (tx.vout(0).scriptpubkey() != txOwners[0])
			{
				return -23;
			}
		}

		// Check signature
		if (tx.signprehash_size() > needVerifyPreHashCount)
		{
			ERRORLOG("StrictCheckTransaction signature is more than needing!");
			return -24;
		}
	}

	// Check fee
	if (CheckTransactionType(tx) == kTransactionType_Fee)
	{
		if (tx.vin(0).prevout().hash().size() != 64)
		{
			return -25;
		}
		if (IsOutDuplicate(tx))
		{
			ERRORLOG("StrictCheckTransaction found duplicate out!");
			return -26;
		}
	}

	// Check award
	if (CheckTransactionType(tx) == kTransactionType_Award)
	{
		if (!tx.vin(0).prevout().hash().empty())
		{
			return -27;
		}
		if (IsOutDuplicate(tx))
		{
			ERRORLOG("StrictCheckTransaction found duplicate out!");
			return -28;
		}
	}

	return 0;
}

bool checkTransaction(const CTransaction & tx)
{
	int result = StrictCheckTransaction(tx);
	if (result != 0)
	{
		ERRORLOG("StrictCheckTransaction failed! code = {}", result);
		return false;
	}

	if (tx.vin_size() == 0 || tx.vout_size() == 0)
	{
		return false;
	}

	uint64_t total = 0;
	for (int i = 0; i < tx.vout_size(); i++)
	{
		CTxout txout = tx.vout(i);
		total += txout.value();
	}

	// 检查总金额
	if (total < 0 || total > 21000000LL * COIN)
	{
		return false;
	}

	std::vector<CTxin> vTxins;
	for (int i = 0; i < tx.vin_size(); i++)
	{
		vTxins.push_back(tx.vin(i));
	}

	bool isRedeem = false;
	nlohmann::json extra = nlohmann::json::parse(tx.extra());
	std::string txType = extra["TransactionType"].get<std::string>();
	std::string redeemUtxo;

	if (txType == TXTYPE_REDEEM)
	{
		isRedeem= true;

		if (CheckTransactionType(tx) == kTransactionType_Tx)
		{
			nlohmann::json txInfo = extra["TransactionInfo"].get<nlohmann::json>();
			redeemUtxo = txInfo["RedeemptionUTXO"];

			// 检查质押时间
			if ( 0 != IsMoreThan30DaysForRedeem(redeemUtxo) )
			{
				ERRORLOG("Redeem time is less than 30 days!");
				return false;
			}
		}
	}

	DBReader db_reader;
	if (CheckTransactionType(tx) == kTransactionType_Tx)
	{
		// 检查txowner和vin签名者是否一致
		std::vector<std::string> vinSigners;
		for (const auto & vin : vTxins)
		{
			std::string pubKey = vin.scriptsig().pub();
			std::string addr = GetBase58Addr(pubKey);
			if (vinSigners.end() == find(vinSigners.begin(), vinSigners.end(), addr))
			{
				vinSigners.push_back(addr);
			}
		}

		std::vector<std::string> txOwners = TxHelper::GetTxOwner(tx);
		if (vinSigners != txOwners)
		{
			ERRORLOG("TxOwner or vin signer error!");
			return false;
		}

		// utxo是否存在
		for (const auto & vin : vTxins)
		{
			std::string pubKey = vin.scriptsig().pub();
			std::string addr = GetBase58Addr(pubKey);

			std::vector<std::string> utxos;
			DBStatus status = db_reader.GetUtxoHashsByAddress(addr, utxos);
			if (status != DBStatus::DB_SUCCESS && status != DBStatus::DB_NOT_FOUND)
			{
				ERRORLOG("GetUtxoHashsByAddress error !");
				return false;
			}
			
			std::vector<std::string> pledgeUtxo;
			db_reader.GetPledgeAddressUtxo(addr, pledgeUtxo);
			if (utxos.end() == find(utxos.begin(), utxos.end(), vin.prevout().hash()))
			{
				if (isRedeem)
				{
					if (vin.prevout().hash() != redeemUtxo)
					{
						ERRORLOG("tx vin not found !");
						return false;
					}
				}
				else
				{
					ERRORLOG("tx vin not found !");
					return false;
				}
			}
		}
	}

	// 检查是否有重复vin
	std::sort(vTxins.begin(), vTxins.end(), [](const CTxin & txin0, const CTxin & txin1){
		if (txin0.prevout().n() > txin1.prevout().n())
		{
			return true;
		}
		else
		{
			return false;
		}
	});
	auto iter = std::unique(vTxins.begin(), vTxins.end(), [](const CTxin & txin0, const CTxin & txin1){
		return txin0.prevout().n() == txin1.prevout().n() &&
				txin0.prevout().hash() == txin1.prevout().hash() &&
				txin0.scriptsig().sign() == txin1.scriptsig().sign();
	});

	if (iter != vTxins.end())
	{
		if (isRedeem)
		{
			std::vector<std::string> utxos;
			string txowner = TxHelper::GetTxOwner(tx)[0];
			DBStatus status = db_reader.GetPledgeAddressUtxo(txowner, utxos);
			if (status != DBStatus::DB_SUCCESS && status != DBStatus::DB_NOT_FOUND)
			{
				ERRORLOG("db get pledge utxo failed");
				return false;
			}
			auto utxoIter = find(utxos.begin(), utxos.end(), iter->prevout().hash());
			if (utxoIter == utxos.end())
			{
				std::string txRaw;
				status = db_reader.GetTransactionByHash(iter->prevout().hash(), txRaw);
				if (status != DBStatus::DB_SUCCESS)
				{
					ERRORLOG("db get tx failed");
					return false;
				}

				CTransaction utxoTx;
				utxoTx.ParseFromString(txRaw);
				if (utxoTx.vout_size() == 2)
				{
					if (utxoTx.vout(0).scriptpubkey() != utxoTx.vout(1).scriptpubkey())
					{
						return false;
					}
				}
			}
		}
		else
		{
			std::string txRaw;
			DBStatus status = db_reader.GetTransactionByHash(iter->prevout().hash(), txRaw);
			if (status != DBStatus::DB_SUCCESS)
			{
				return false;
			}

			CTransaction utxoTx;
			utxoTx.ParseFromString(txRaw);
			if (utxoTx.vout_size() == 2)
			{
				if (utxoTx.vout(0).scriptpubkey() != utxoTx.vout(1).scriptpubkey())
				{
					return false;
				}
			}
		}
	}

	if (CheckTransactionType(tx) == kTransactionType_Tx)
	{
		// 交易
		for (auto &txin : vTxins)
		{

			if (txin.prevout().n() == 0xFFFFFFFF)
			{
				return false;
			}
		}
	}
	else
	{
		// 奖励
		uint64_t height = 0;
		DBStatus status  = db_reader.GetBlockTop(height);
		if (status != DBStatus::DB_SUCCESS)
		{
			return false;
		}
		if (tx.signprehash().size() > 0 && 0 == height)
		{
			return false;
		}

		CTxin txin0 = tx.vin(0);
		int scriptSigLen = txin0.scriptsig().sign().length() + txin0.scriptsig().pub().length();
		if (scriptSigLen < 2 || scriptSigLen > 100)
		{
			return false;
		}

		for (auto &txin : vTxins)
		{
			if (height == 0 && (txin.scriptsig().sign() + txin.scriptsig().pub()) == OTHER_COIN_BASE_TX_SIGN)
			{
				return false;
			}
		}
	}
	
	return true;
}

std::vector<std::string> randomNode(unsigned int n)
{
	std::vector<Node> nodeInfos ;
	if (Singleton<PeerNode>::get_instance()->get_self_node().is_public_node)
	{
		nodeInfos = Singleton<PeerNode>::get_instance()->get_nodelist();
		DEBUGLOG("randomNode PeerNode size() = {}", nodeInfos.size());
	}
	else
	{
		nodeInfos = Singleton<NodeCache>::get_instance()->get_nodelist();
		DEBUGLOG("randomNode NodeCache size() = {}", nodeInfos.size());
	}
	std::vector<std::string> v;
	for (const auto & i : nodeInfos)
	{
		v.push_back(i.base58address);
	}
	
	unsigned int nodeSize = n;
	std::vector<std::string> sendid;
	if ((unsigned int)v.size() < nodeSize)
	{
		DEBUGLOG("not enough node to send");
		return  sendid;
	}

	std::string s = net_get_self_node_id();
	auto iter = std::find(v.begin(), v.end(), s);
	if (iter != v.end())
	{
		v.erase(iter);
	}

	if (v.empty())
	{
		return v;
	}

	if (v.size() <= nodeSize)
	{
		for (auto & i : v)
		{
			sendid.push_back(i);	
		}
	}
	else
	{
		std::set<int> rSet;
		while (1)
		{
			int i = rand() % v.size();
			rSet.insert(i);
			if (rSet.size() == nodeSize)
			{
				break;
			}
		}

		for (auto &i : rSet)
		{
			sendid.push_back(v[i]);
		}
	}
	
	return sendid;
}

int GetSignString(const std::string & message, std::string & signature, std::string & strPub)
{
	if (message.size() <= 0)
	{
		ERRORLOG("(GetSignString) parameter is empty!");
		return -1;
	}

	bool result = false;
	result = SignMessage(g_privateKey, message, signature);
	if (!result)
	{
		ERRORLOG("(GetSignString) sign failed!");
		return -1;
	}

	GetPublicKey(g_publicKey, strPub);
	return 0;
}

int GetSignString(const std::string & base58Addr, const std::string & message, std::string & signature, std::string & strPub)
{
	if (!CheckBase58Addr(base58Addr))
	{
		ERRORLOG("GetSignString base58 address is not valid!");
		return -1;
	}

	if (message.empty())
	{
		ERRORLOG("GetSignString parameter is empty!");
		return -2;
	}

	account acc;
	if (!g_AccountInfo.FindKey(base58Addr.c_str(), acc))
	{
		ERRORLOG("GetSignString GetKey error!");
		return -3;
	}

	if (!g_AccountInfo.Sign(base58Addr.c_str(), message, signature))
	{
		ERRORLOG("GetSignString sign error!");
		return -4;
	}
	
	strPub = acc.sPubStr;
	return 0;
}

int GetRedemUtxoAmount(const std::string & redeemUtxoStr, uint64_t & amount)
{
	if (redeemUtxoStr.size() != 64)
	{
		ERRORLOG("(GetRedemUtxoAmount) param error !");
		return -1;
	}

    DBReader db_reader;
	std::string txRaw;
	if ( DBStatus::DB_SUCCESS != db_reader.GetTransactionByHash(redeemUtxoStr, txRaw) )
	{
		ERRORLOG("(GetRedemUtxoAmount) GetTransactionByHash failed !");
		return -3;
	}

	CTransaction tx;
	tx.ParseFromString(txRaw);

	for (int i = 0; i < tx.vout_size(); ++i)
	{
		CTxout txout = tx.vout(i);
		if (txout.scriptpubkey() == VIRTUAL_ACCOUNT_PLEDGE)
		{
			amount = txout.value();
		}
	}

	return 0;
}


bool VerifyBlockHeader(const CBlock & cblock)
{
	std::string hash = cblock.hash();
	DBReader db_read;

	std::string strTempHeader;
	auto ret = db_read.GetBlockByBlockHash(hash, strTempHeader);
	if(DBStatus::DB_SUCCESS == ret || strTempHeader.size() != 0)
	{
		DEBUGLOG("BlockInfo has exist , do not need to add ...");
        return false;
	}
	if(DBStatus::DB_NOT_FOUND != ret)
	{
		ERRORLOG("GetBlockByBlockHash Error {} ret:{}", hash, ret);
		return false;
	}

	std::string strPrevHeader;
	ret = db_read.GetBlockByBlockHash(cblock.prevhash(), strPrevHeader);
    if (DBStatus::DB_SUCCESS != ret || strPrevHeader.empty())
	{
        ERRORLOG("GetBlockByBlockHash {} failed db_status:{}! ", cblock.prevhash(), ret);
		return false;
	}

	// 区块检查

	// 时间戳检查	
	std::string strGenesisBlockHash;
	ret = db_read.GetBlockHashByBlockHeight(0, strGenesisBlockHash);
	if (DBStatus::DB_SUCCESS != ret)
	{
		ERRORLOG("GetBlockHashByBlockHeight failed!" );
		return false;
	}
	std::string strGenesisBlockHeader;
	ret = db_read.GetBlockByBlockHash(strGenesisBlockHash, strGenesisBlockHeader);
	if (DBStatus::DB_SUCCESS != ret)
	{
		ERRORLOG("GetBlockHashByBlockHeight failed!" );
		return false;
	}

	if (strGenesisBlockHeader.length() == 0)
	{
		ERRORLOG("Genesis Block is not exist");
		return false;
	}
	
	CBlock genesisBlockHeader;
	genesisBlockHeader.ParseFromString(strGenesisBlockHeader);
	uint64_t blockHeaderTimestamp = cblock.time();
	uint64_t genesisBlockHeaderTimestamp = genesisBlockHeader.time();
	if (blockHeaderTimestamp == 0 || genesisBlockHeaderTimestamp == 0 || blockHeaderTimestamp <= genesisBlockHeaderTimestamp)
	{
		ERRORLOG("block timestamp error!");
		return false;
	}

	if (cblock.txs_size() == 0)
	{
		ERRORLOG("cblock.txs_size() == 0");
		return false;
	}

	if (cblock.SerializeAsString().size() > MAX_BLOCK_SIZE)
	{
		ERRORLOG("cblock.SerializeAsString().size() > MAX_BLOCK_SIZE");
		return false;
	}

	if (CalcBlockHeaderMerkle(cblock) != cblock.merkleroot())
	{
		ERRORLOG("CalcBlockHeaderMerkle(cblock) != cblock.merkleroot()");
		return false;
	}

	// 获取本块的签名个数
	uint64_t blockSignNum = 0;
	for (auto & txTmp : cblock.txs())
	{
		if (CheckTransactionType(txTmp) == kTransactionType_Fee)
		{
			blockSignNum = txTmp.vout_size();
		}
	}

	std::map<std::string, uint64_t> addrAwards;
	// 获取奖励交易中的节点信息,计算各个账号奖励值,用于校验
	if (cblock.height() > g_compatMinHeight)
	{
		for (auto & txTmp : cblock.txs())
		{
			if (CheckTransactionType(txTmp) == kTransactionType_Award)
			{
				nlohmann::json txExtra;
				try
				{
					txExtra = nlohmann::json::parse(txTmp.extra());
				}
				catch(const std::exception& e)
				{
					std::cerr << e.what() << '\n';
					return false;
				}
				
				std::vector<std::string> addrs;
				std::vector<double> onlines;
				std::vector<uint64_t> awardTotals;
				std::vector<uint64_t> signSums;

				for(const auto& info : txExtra["OnlineTime"])
				{
					try
					{
						addrs.push_back(info["Addr"].get<std::string>());
						onlines.push_back(info["OnlineTime"].get<double>());
						awardTotals.push_back(info["AwardTotal"].get<uint64_t>());
						signSums.push_back(info["SignSum"].get<uint64_t>());
					}
					catch(const std::exception& e)
					{
						std::cerr << e.what() << '\n';
						return false;
					}
				}

				if (addrs.size() == 0 || onlines.size() == 0 || awardTotals.size() == 0 || signSums.size() == 0)
				{
					if (cblock.height() != 0)
					{
						ERRORLOG("Get sign node info error !");
						return false;
					}
				}
				else
				{
					a_award::AwardAlgorithm awardAlgorithm;
					if ( 0 != awardAlgorithm.Build(cblock.height(), blockSignNum, addrs, onlines, awardTotals, signSums) )
					{
						ERRORLOG("awardAlgorithm.Build() failed!");
						return false;
					}

					auto awardInfo = awardAlgorithm.GetDisAward();
					for (auto & award : awardInfo)
					{
						addrAwards.insert(std::make_pair(award.second, award.first));
					}	
				}
				// awardAlgorithm.TestPrint(true);
			}
		}
	}

	// 交易检查
	for (int i = 0; i < cblock.txs_size(); i++)
	{
		CTransaction tx = cblock.txs(i);
		if (!checkTransaction(tx))
		{
			ERRORLOG("checkTransaction(tx)");
			return false;
		}

		bool iscb = CheckTransactionType(tx) == kTransactionType_Fee || CheckTransactionType(tx) == kTransactionType_Award;

		if (iscb && 0 == tx.signprehash_size())
		{
			continue;
		}

		std::string bestChainHash;
		ret = db_read.GetBestChainHash(bestChainHash);
        if (DBStatus::DB_SUCCESS != ret) 
		{
			ERRORLOG(" pRocksDb->GetBestChainHash db_status{}", ret);
            return false;
        }
		bool isBestChainHash = bestChainHash.size() != 0;
		if (! isBestChainHash && iscb && 0 == cblock.txs_size())
		{
			continue;
		}

		if (0 == cblock.txs_size() && ! isBestChainHash)
		{
			ERRORLOG("0 == cblock.txs_size() && ! isBestChainHash");
			return false;
		}

		if (isBestChainHash)
		{
			int verifyPreHashCount = 0;
			std::string txBlockHash;

            std::string txHashStr;
            
            for (int i = 0; i < cblock.txs_size(); i++)
            {
                CTransaction transaction = cblock.txs(i);
                if ( CheckTransactionType(transaction) == kTransactionType_Tx)
                {
                    CTransaction copyTx = transaction;
                    for (int i = 0; i != copyTx.vin_size(); ++i)
                    {
                        CTxin * txin = copyTx.mutable_vin(i);
                        txin->clear_scriptsig();
                    }

                    copyTx.clear_signprehash();
                    copyTx.clear_hash();

                    std::string serCopyTx = copyTx.SerializeAsString();

                    size_t encodeLen = serCopyTx.size() * 2 + 1;
                    unsigned char encode[encodeLen] = {0};
                    memset(encode, 0, encodeLen);
                    long codeLen = base64_encode((unsigned char *)serCopyTx.data(), serCopyTx.size(), encode);
                    std::string encodeStr( (char *)encode, codeLen );

                    txHashStr = getsha256hash(encodeStr);
                }
            }
			
			if (! VerifyTransactionSign(tx, verifyPreHashCount, txBlockHash, txHashStr))
			{
				ERRORLOG("VerifyTransactionSign");
				return false;
			}

			if (verifyPreHashCount < g_MinNeedVerifyPreHashCount)
			{
				ERRORLOG("verifyPreHashCount < g_MinNeedVerifyPreHashCount");
				return false;
			}
		}

		// 获取交易类型
		// bool bIsRedeem = false;
		std::string redempUtxoStr;
		for (int i = 0; i < cblock.txs_size(); i++)
		{
			CTransaction transaction = cblock.txs(i);
			if ( CheckTransactionType(transaction) == kTransactionType_Tx)
			{
				CTransaction copyTx = transaction;

				nlohmann::json txExtra = nlohmann::json::parse(copyTx.extra());
				std::string txType = txExtra["TransactionType"].get<std::string>();

				if (txType == TXTYPE_REDEEM)
				{
					// bIsRedeem = true;

					nlohmann::json txInfo = txExtra["TransactionInfo"].get<nlohmann::json>();
					redempUtxoStr = txInfo["RedeemptionUTXO"].get<std::string>();
				}
			}
		}
		//{{ Redeem time is more than 30 days, 20201214
		if (!redempUtxoStr.empty() && cblock.height() > g_compatMinHeight)
		{
			int result = IsMoreThan30DaysForRedeem(redempUtxoStr);
			if (result != 0)
			{
				ERRORLOG("Redeem time is less than 30 days!");
				return false;
			}
			else
			{
				DEBUGLOG("Redeem time is more than 30 days!");
			}
		}
		//}}

		// 验证签名公钥和base58地址是否一致
		std::vector< std::string > signBase58Addrs;
		for (int i = 0; i < cblock.txs_size(); i++)
		{
			CTransaction transaction = cblock.txs(i);
			if ( CheckTransactionType(transaction) == kTransactionType_Tx)
			{
				// 取出所有签名账号的base58地址
				for (int k = 0; k < transaction.signprehash_size(); k++) 
                {
                    char buf[2048] = {0};
                    size_t buf_len = sizeof(buf);
                    GetBase58Addr(buf, &buf_len, 0x00, transaction.signprehash(k).pub().c_str(), transaction.signprehash(k).pub().size());
					std::string bufStr(buf);
					signBase58Addrs.push_back( bufStr );
                }
			}
		}

		std::vector<std::string> txOwners;
		uint64_t packageFee = 0;
		uint64_t signFee = 0;
		for (int i = 0; i < cblock.txs_size(); i++)
		{
			CTransaction transaction = cblock.txs(i);
			if ( CheckTransactionType(transaction) == kTransactionType_Tx)
			{
				StringUtil::SplitString(tx.owner(), txOwners, "_");
				if (txOwners.size() < 1)
				{
					ERRORLOG("txOwners error!");
					return false;
				}

				nlohmann::json extra = nlohmann::json::parse(tx.extra());
				packageFee = extra["PackageFee"].get<uint64_t>();
				signFee = extra["SignFee"].get<uint64_t>();
			}
		}

		for (int i = 0; i < cblock.txs_size(); i++)
		{
			CTransaction transaction = cblock.txs(i);
			if ( CheckTransactionType(transaction) == kTransactionType_Fee)
			{
				// 签名账号的数量和vout的数量不一致，错误
				if( signBase58Addrs.size() != (size_t)transaction.vout_size() )
				{
					ERRORLOG("signBase58Addrs.size() != (size_t)transaction.vout_size()");
					return false;
				}

				// base58地址不一致，错误
				for(int l = 0; l < transaction.vout_size(); l++)
				{
					CTxout txout = transaction.vout(l);	
					auto iter = find(signBase58Addrs.begin(), signBase58Addrs.end(), txout.scriptpubkey());
					if( iter == signBase58Addrs.end() )
					{
						ERRORLOG("iter == signBase58Addrs.end()");
						return false;
					}

					if (txout.value() < 0)
					{
						ERRORLOG("vout error !");
						return false;
					}

					if (txOwners.end() != find(txOwners.begin(), txOwners.end(), txout.scriptpubkey()))
					{
						if (txout.value() != 0)
						{
							return false;
						}
					}
					else
					{
						if ((uint64_t)txout.value() != signFee && (uint64_t)txout.value() != packageFee)
						{
							ERRORLOG("SignFee or packageFee error !");
							return false;
						}
					}
				}
			}
			else if ( CheckTransactionType(transaction) == kTransactionType_Award )
			{
				uint64_t awardAmountTotal = 0;
				for (auto & txout : transaction.vout())
				{
					// 不使用uint64 是为了防止有负值的情况
					int64_t value = txout.value();
					std::string voutAddr = txout.scriptpubkey();

					if (cblock.height() > g_compatMinHeight)
					{		
						// 发起方账号奖励为0
						if (txOwners.end() != find(txOwners.begin(), txOwners.end(), voutAddr))
						{
							if (value != 0)
							{
								ERRORLOG("Award error !");
								return false;
							}
						}
						else
						{
							// 奖励为负值，或者大于单笔最高奖励值的时候，错误，返回
							if (value < 0 || (uint64_t)value > g_MaxAwardTotal)
							{
								ERRORLOG("Award error !");
								return false;
							}
							else if (value == 0)
							{
								for (int i = 0; i < cblock.txs_size(); i++)
								{
									CTransaction transaction = cblock.txs(i);
									uint32_t count = 0;
									if ( CheckTransactionType(transaction) == kTransactionType_Award)
									{
										for (auto & txout : transaction.vout())
										{
											if (txout.value() == 0)
											{
												count++;
											}
										}

										if (count > 1)
										{
											return false;
										}
									}
								}
							}

							nlohmann::json txExtra;
							try
							{
								txExtra = nlohmann::json::parse(tx.extra());
							}
							catch(const std::exception& e)
							{
								std::cerr << e.what() << '\n';
								return false;
							}
							
							for (nlohmann::json::iterator it = txExtra.begin(); it != txExtra.end(); ++it) 
							{
								if (voutAddr == it.key())
								{
									auto iter = addrAwards.find(voutAddr);
									if (iter == addrAwards.end())
									{
										ERRORLOG("Transaction award error !");
										return false;
									}
									else
									{
										if ((uint64_t)value != iter->second)
										{
											ERRORLOG("Transaction award error !");
											return false;
										}
									}
								}
							}
						}
					}
					awardAmountTotal += txout.value();
				}

				if (awardAmountTotal > g_MaxAwardTotal)
				{
					ERRORLOG("awardAmountTotal error !");
					return false;
				}
			}
		}
	}


	// 共识数不能小于g_MinNeedVerifyPreHashCount
	if (cblock.height() > g_compatMinHeight)
	{
		for(auto & tx : cblock.txs())
		{
			if (CheckTransactionType(tx) == kTransactionType_Tx)
			{
				std::vector<std::string> txownersTmp = TxHelper::GetTxOwner(tx);

				for (auto & txout : tx.vout())
				{
					if (txout.value() <= 0 && txownersTmp.end() == find(txownersTmp.begin(), txownersTmp.end(), txout.scriptpubkey()))
					{
						// 交易接收方接收金额不能为0
						ERRORLOG("Tx vout error !");
						return false;
					}
					else if (txout.value() < 0 && txownersTmp.end() != find(txownersTmp.begin(), txownersTmp.end(), txout.scriptpubkey()))
					{
						// 交易发起方剩余资产可以为0.但不能小于0
						ERRORLOG("Tx vout error !");
						return false;
					}
				}
			}
			else if ( CheckTransactionType(tx) == kTransactionType_Fee)
			{
				if (tx.vout_size() < g_MinNeedVerifyPreHashCount)
				{
					ERRORLOG("The number of signers is not less than {} !", g_MinNeedVerifyPreHashCount);
					return false;
				}
			}
		}
	}

	return true;
}


std::string CalcBlockHeaderMerkle(const CBlock & cblock)
{
	std::string merkle;
	if (cblock.txs_size() == 0)
	{
		return merkle;
	}

	std::vector<std::string> vTxHashs;
	for (int i = 0; i != cblock.txs_size(); ++i)
	{
		CTransaction tx = cblock.txs(i);
		vTxHashs.push_back(tx.hash());
	}

	unsigned int j = 0, nSize;
    for (nSize = cblock.txs_size(); nSize > 1; nSize = (nSize + 1) / 2)
	{
        for (unsigned int i = 0; i < nSize; i += 2)
		{
            unsigned int i2 = MIN(i+1, nSize-1);

			std::string data1 = vTxHashs[j + i];
			std::string data2 = vTxHashs[j + i2];
			data1 = getsha256hash(data1);
			data2 = getsha256hash(data2);

			vTxHashs.push_back(getsha256hash(data1 + data2));
        }

        j += nSize;
    }

	merkle = vTxHashs[vTxHashs.size() - 1];

	return merkle;
}

void CalcBlockMerkle(CBlock & cblock)
{
	if (cblock.txs_size() == 0)
	{
		return;
	}

	cblock.set_merkleroot(CalcBlockHeaderMerkle(cblock));
}

CBlock CreateBlock(const CTransaction & tx, const std::shared_ptr<TxMsg>& SendTxMsg)
{
	CBlock cblock;

	uint64_t time = Singleton<TimeUtil>::get_instance()->getlocalTimestamp();
    cblock.set_time(time);
	cblock.set_version(1);

    DBReader  db_reader;

	std::string prevBlockHash = SendTxMsg->prevblkhash();
	unsigned int prevBlockHeight = 0;
	if (DBStatus::DB_SUCCESS != db_reader.GetBlockHeightByBlockHash(prevBlockHash, prevBlockHeight))
	{
		// 父块不存在, 不建块
		cblock.clear_hash();
		return cblock;
	}

	// 要加的块的高度
	unsigned int cblockHeight = ++prevBlockHeight;

	uint64_t myTop = 0;
	db_reader.GetBlockTop(myTop);
	if ( (myTop  > 9) && (myTop - 9 > cblockHeight))
	{
		cblock.clear_hash();
		return cblock;
	}
	else if (myTop + 1 < cblockHeight)
	{
		cblock.clear_hash();
		return cblock;
	}

	std::string bestChainHash;
    auto ret = db_reader.GetBestChainHash(bestChainHash);
    if (DBStatus::DB_SUCCESS != ret)
	{
		ERRORLOG("(CreateBlock) GetBestChainHash failed db_status:{}!", ret);
    }
	if (bestChainHash.size() == 0)
	{
		cblock.set_prevhash(std::string(64, '0'));
		cblock.set_height(0);
	}
	else
	{
		cblock.set_prevhash(SendTxMsg->prevblkhash());
		cblock.set_height(SendTxMsg->top() + 1);
	}

	CTransaction * tx0 = cblock.add_txs();
	*tx0 = tx;

	if (ENCOURAGE_TX && CheckTransactionType(tx) == kTransactionType_Tx)
	{
		DEBUGLOG("Crreate Encourage TX ... ");
		CTransaction workTx = CreateWorkTx(*tx0);
		if (workTx.hash().empty())
		{
			cblock.clear_hash();
			return cblock;
		}
		CTransaction * tx1 = cblock.add_txs();
		*tx1 = workTx;

        if (get_extra_award_height()) 
		{
            DEBUGLOG("Crreate Encourage TX 2 ... ");
            CTransaction workTx2 = CreateWorkTx(*tx0, true, SendTxMsg);
			if (workTx2.hash().empty())
			{
				cblock.clear_hash();
				return cblock;
			}
            CTransaction * txadd2 = cblock.add_txs();
            *txadd2 = workTx2;
        }
	}

	CalcBlockMerkle(cblock);

	std::string serBlockHeader = cblock.SerializeAsString();
	cblock.set_hash(getsha256hash(serBlockHeader));
	
	return cblock;
}

bool AddBlock(const CBlock & cblock, bool isSync)
{
    lock_guard<std::mutex> lock(rocksdb_mutex);
    unsigned int preheight;
	DBReadWriter db_read_write;
	auto ret = db_read_write.GetBlockHeightByBlockHash(cblock.prevhash(), preheight);
    if (DBStatus::DB_SUCCESS != ret) {
	    ERRORLOG("AddBlock GetBlockHeightByBlockHash ret:{}", ret);
        return false;
    }

	CBlockHeader block;
	block.set_hash(cblock.hash());
	block.set_prevhash(cblock.prevhash());
	block.set_time(cblock.time());
	block.set_height(preheight +1);

	uint64_t top = 0;
	DEBUGLOG("AddBlock GetBlockTop");
	if (db_read_write.GetBlockTop(top) != 0)
	{
		ERRORLOG("AddBlock GetBlockTop ret != 0");
		return false;
	}

	//更新top和BestChain
	bool is_mainblock = false;
	if (block.height() > top)  
	{
		is_mainblock = true;
		ret = db_read_write.SetBlockTop(block.height());
        if (DBStatus::DB_SUCCESS != ret)
		{
            return false;
        }
		ret = db_read_write.SetBestChainHash(block.hash());
        if (DBStatus::DB_SUCCESS != ret)
		{
            return false;
        }
		auto cache_height = block.height() - 1;
		std::string block_hash;
		if (DBStatus::DB_SUCCESS != db_read_write.GetBlockHashByBlockHeight(cache_height,block_hash))
		{
			ERRORLOG("fail to add block cache, can't GetBlockHashByBlockHeight");
		}
		std::string block_str;
		if (DBStatus::DB_SUCCESS != db_read_write.GetBlockByBlockHash(block_hash, block_str))
		{
			ERRORLOG("fail to add block cache, can't GetBlockByBlockHash");
		}
		CBlock cache_block;
		cache_block.ParseFromString(block_str);
		MagicSingleton<CBlockCache>::GetInstance()->Add(cache_block);		
	}
	else if (block.height() == top)
	{
		std::string strBestChainHash;
		if (DBStatus::DB_SUCCESS != db_read_write.GetBestChainHash(strBestChainHash))
		{
			return false;
		}

		std::string strBestChainHeader;
		if (DBStatus::DB_SUCCESS != db_read_write.GetBlockByBlockHash(strBestChainHash, strBestChainHeader))
		{
			return false;
		}

		CBlock bestChainBlockHeader;
		bestChainBlockHeader.ParseFromString(strBestChainHeader);

		if (cblock.time() < bestChainBlockHeader.time())
		{
			is_mainblock = true;
			ret = db_read_write.SetBestChainHash(block.hash());
            if (DBStatus::DB_SUCCESS != ret)
			{
                return false;
            }
		}
	}
	else if(block.height() < top)
	{
		std::string main_hash;
		db_read_write.GetBlockHashByBlockHeight(block.height(), main_hash);
		std::string main_block_str;
		if (DBStatus::DB_SUCCESS != db_read_write.GetBlockByBlockHash(main_hash, main_block_str))
		{
			return false;
		}
		CBlock main_block;
		main_block.ParseFromString(main_block_str);	
		if (cblock.time() < main_block.time())
		{
			is_mainblock = true;
		}
	}

	ret = db_read_write.SetBlockHeightByBlockHash(block.hash(), block.height());
    if (DBStatus::DB_SUCCESS != ret)
	{
	    ERRORLOG("SetBlockHeightByBlockHash db_status:{}", ret);
        return false;
    }
	ret = db_read_write.SetBlockHashByBlockHeight(block.height(), block.hash(), is_mainblock);

    if (DBStatus::DB_SUCCESS != ret)
	{
	    ERRORLOG("SetBlockHashByBlockHeight db_status:{}", ret);
        return false;
    }
	ret = db_read_write.SetBlockHeaderByBlockHash(block.hash(), block.SerializeAsString());
    if (DBStatus::DB_SUCCESS != 0)
	{
	    ERRORLOG("SetBlockHeaderByBlockHash db_status:{}", ret);
        return false;
    }
	ret = db_read_write.SetBlockByBlockHash(block.hash(), cblock.SerializeAsString());
    if (DBStatus::DB_SUCCESS != 0)
	{
	    ERRORLOG("AddBlock SetBlockByBlockHash db_status:{}", ret);
        return false;
    }

	// 判断交易是否是特殊交易
	bool isPledge = false;
	bool isRedeem = false;
	std::string redempUtxoStr;

	nlohmann::json extra = nlohmann::json::parse(cblock.extra());
	std::string txType = extra["TransactionType"].get<std::string>();
	if (txType == TXTYPE_PLEDGE)
	{
		isPledge = true;
	}
	else if (txType == TXTYPE_REDEEM)
	{
		isRedeem = true;
		nlohmann::json txInfo = extra["TransactionInfo"].get<nlohmann::json>();
		redempUtxoStr = txInfo["RedeemptionUTXO"].get<std::string>();
	}
	
	// 计算支出的总燃油费
	uint64_t totalGasFee = 0;
	for (int txCount = 0; txCount < cblock.txs_size(); txCount++)
	{
		CTransaction tx = cblock.txs(txCount);
		if ( CheckTransactionType(tx) == kTransactionType_Fee)
		{
			for (int j = 0; j < tx.vout_size(); j++)
			{
				CTxout txout = tx.vout(j);
				totalGasFee += txout.value();
			}
		}
	}

	if(totalGasFee == 0 && !isRedeem)
	{
		ERRORLOG("tx sign GasFee is 0! AddBlock failed! ");
		return false;
	}

	for (int i = 0; i < cblock.txs_size(); i++)
	{
		CTransaction tx = cblock.txs(i);
		bool isTx = false;
		if (CheckTransactionType(tx) == kTransactionType_Tx)
		{
			isTx = true;
		}

		for (int j = 0; j < tx.vout_size(); j++)
		{
			CTxout txout = tx.vout(j);

			if (isPledge && isTx)
			{
				if ( !txout.scriptpubkey().compare(VIRTUAL_ACCOUNT_PLEDGE) )
				{
					ret =  db_read_write.SetPledgeAddresses(TxHelper::GetTxOwner(tx)[0]);
					if (DBStatus::DB_SUCCESS != ret && DBStatus::DB_IS_EXIST != ret)
					{
						return false;
					}

					ret = db_read_write.SetPledgeAddressUtxo(TxHelper::GetTxOwner(tx)[0], tx.hash());
					if (DBStatus::DB_SUCCESS != ret)
					{
						return false;
					}
				}
			}

			// if ( txout.scriptpubkey().compare(VIRTUAL_ACCOUNT_PLEDGE) )
			{
				ret = db_read_write.SetUtxoHashsByAddress(txout.scriptpubkey(), tx.hash());
				if (DBStatus::DB_SUCCESS != ret && DBStatus::DB_IS_EXIST != ret)
				{
					return false;
				}	
			}
		}

		ret = db_read_write.SetTransactionByHash(tx.hash(), tx.SerializeAsString());
		if (DBStatus::DB_SUCCESS != ret)
		{
		    ERRORLOG("AddBlock SetTransactionByHash db_status:{}", ret);
            return false;
        }
		ret = db_read_write.SetBlockHashByTransactionHash(tx.hash(), cblock.hash());
		if (DBStatus::DB_SUCCESS != ret)
		{
		    ERRORLOG("AddBlock SetBlockHashByTransactionHash db_status:{}", ret);
            return false;
        }

		std::vector<std::string> vPledgeUtxoHashs;
		if (isRedeem && isTx)
		{
			ret = db_read_write.GetPledgeAddressUtxo(TxHelper::GetTxOwner(tx)[0], vPledgeUtxoHashs);
		    if (DBStatus::DB_SUCCESS != ret)
			{
				return false;
			}			
		}

		// 判断交易的vin中是否有质押产生的正常utxo部分
		nlohmann::json extra = nlohmann::json::parse(tx.extra());
		std::string txType = extra["TransactionType"];
		std::string redempUtxoStr;
		uint64_t packageFee = 0;
		if (txType == TXTYPE_REDEEM)
		{
			nlohmann::json txInfo = extra["TransactionInfo"].get<nlohmann::json>();
			redempUtxoStr = txInfo["RedeemptionUTXO"].get<std::string>();
			packageFee = extra["PackageFee"].get<uint64_t>();
		}

		std::vector<CTxin> txVins;
		uint64_t vinAmountTotal = 0;
		uint64_t voutAmountTotal = 0;

		for (auto & txin : tx.vin())
		{
			txVins.push_back(txin);
		}

		if (txType == TXTYPE_REDEEM)
		{
			for (auto iter = txVins.begin(); iter != txVins.end(); ++iter)
			{
				if (iter->prevout().hash() == redempUtxoStr)
				{
					txVins.erase(iter);
					break;
				}
			}
		}

		std::vector<std::string> utxos;
		for (auto & txin : txVins)
		{
			if (utxos.end() != find(utxos.begin(), utxos.end(), txin.prevout().hash()))
			{
				continue;
			}

			std::string txinAddr = GetBase58Addr(txin.scriptsig().pub());
			vinAmountTotal += TxHelper::GetUtxoAmount(txin.prevout().hash(), txinAddr);
			utxos.push_back(txin.prevout().hash());
		}

		bool bIsUsed = false;
		if (CheckTransactionType(tx) == kTransactionType_Tx && txType == TXTYPE_REDEEM)
		{
			for (int txCount = 0; txCount < cblock.txs_size(); txCount++)
			{
				CTransaction txTmp = cblock.txs(txCount);
				if (CheckTransactionType(txTmp) != kTransactionType_Award)
				{
					for (auto & txout : txTmp.vout())
					{
						voutAmountTotal += txout.value();
					}
				}
			}

			if (voutAmountTotal != vinAmountTotal)
			{
				uint64_t usable = TxHelper::GetUtxoAmount(redempUtxoStr, TxHelper::GetTxOwner(tx)[0]);
				uint64_t redeemAmount = TxHelper::GetUtxoAmount(redempUtxoStr,VIRTUAL_ACCOUNT_PLEDGE);
				if (voutAmountTotal == vinAmountTotal + usable + redeemAmount)
				{
					// 本交易使用了质押utxo的正常部分
					bIsUsed = true;
				}
				else if (voutAmountTotal == vinAmountTotal + redeemAmount + packageFee)
				{
					bIsUsed = false;
				}
				else
				{
					if (cblock.height() > g_compatMinHeight)
					{
						return false;
					}
				}
				
			}
		}

		// vin
		std::vector<std::string> fromAddrs;
		if (CheckTransactionType(tx) == kTransactionType_Tx)
		{
			// 解质押交易有重复的UTXO,去重
			std::set<std::pair<std::string, std::string>> utxoAddrSet; 
			for (auto & txin : tx.vin())
			{
				std::string addr = GetBase58Addr(txin.scriptsig().pub());

				// 交易记录
				if (DBStatus::DB_SUCCESS != db_read_write.SetAllTransactionByAddress(addr, tx.hash()))
				{
					return false;
				}
				fromAddrs.push_back(addr);

				std::vector<std::string> utxoHashs;
				ret = db_read_write.GetUtxoHashsByAddress(addr, utxoHashs);
				if (DBStatus::DB_SUCCESS != ret)
				{
					return false;
				}

				// 在所有utxo中查找vin使用的utxo是否存在
				if (utxoHashs.end() == find(utxoHashs.begin(), utxoHashs.end(), txin.prevout().hash() ) )
				{
					// 在可使用的utxo找不到，则判断是否为解质押的utxo
					if (txin.prevout().hash() != redempUtxoStr)
					{
						return false;
					}
					else
					{
						continue;
					}
				}

				std::pair<std::string, std::string> utxoAddr = make_pair(txin.prevout().hash(), addr);
				utxoAddrSet.insert(utxoAddr);
			}

			for (auto & utxoAddr : utxoAddrSet) 
			{
				std::string utxo = utxoAddr.first;
				std::string addr = utxoAddr.second;

				std::string txRaw;
				if (DBStatus::DB_SUCCESS != db_read_write.GetTransactionByHash(utxo, txRaw) )
				{
					return false;
				}

				CTransaction utxoTx;
				utxoTx.ParseFromString(txRaw);

				nlohmann::json extra = nlohmann::json::parse(tx.extra());
				std::string txType = extra["TransactionType"];
				
				if (txType == TXTYPE_PLEDGE && !bIsUsed && utxo == redempUtxoStr)
				{
					continue;
				}

				ret = db_read_write.RemoveUtxoHashsByAddress(addr, utxo);
				if (DBStatus::DB_SUCCESS != ret)
				{
					return false;
				}

				// vin减utxo
				uint64_t amount = TxHelper::GetUtxoAmount(utxo, addr);
				int64_t balance = 0;
				ret = db_read_write.GetBalanceByAddress(addr, balance);
				if (DBStatus::DB_SUCCESS != ret)
				{
					ERRORLOG("AddBlock:GetBalanceByAddress ret:{}", ret);
					return false;
				}

				balance -= amount;

				if(balance < 0)
				{
					balance = 0;
				}

				ret = db_read_write.SetBalanceByAddress(addr, balance);
				if (DBStatus::DB_SUCCESS != ret)
				{
					return false;
				}
			}

			if (isRedeem)
			{
				std::string addr = TxHelper::GetTxOwner(tx)[0];
				ret = db_read_write.RemovePledgeAddressUtxo(addr, redempUtxoStr);
				if (DBStatus::DB_SUCCESS != ret)
				{
					return false;
				}
				std::vector<string> utxoes;
				ret = db_read_write.GetPledgeAddressUtxo(addr, utxoes);
				if (DBStatus::DB_SUCCESS != ret && DBStatus::DB_NOT_FOUND != ret)
				{
					return false;
				}
				if(utxoes.size() == 0)
				{
					ret = db_read_write.RemovePledgeAddresses(addr);
				    if (DBStatus::DB_SUCCESS != ret)
					{
						return false;
					}
				}
			}
		}

		// vout
		if ( CheckTransactionType(tx) == kTransactionType_Tx)
		{	
			for (int j = 0; j < tx.vout_size(); j++)
			{
				//vout加余额
				CTxout txout = tx.vout(j);
				std::string vout_address = txout.scriptpubkey();
				int64_t balance = 0;
				ret = db_read_write.GetBalanceByAddress(vout_address, balance);
				if (DBStatus::DB_SUCCESS != ret)
				{
					INFOLOG("AddBlock:GetBalanceByAddress");
				}
				balance += txout.value();
				ret = db_read_write.SetBalanceByAddress(vout_address, balance);
				if (DBStatus::DB_SUCCESS != ret)
				{
					return false;
				}

				if (isRedeem && 
					tx.vout_size() == 2 && 
					tx.vout(0).scriptpubkey() == tx.vout(1).scriptpubkey())
				{
					if (j == 0)
					{
						ret = db_read_write.SetAllTransactionByAddress(txout.scriptpubkey(), tx.hash());
				        if (DBStatus::DB_SUCCESS != ret)
				        {
					        return false;
			         	}
					}
				}
				else
				{
					// 交易发起方已经记录
					if (fromAddrs.end() != find( fromAddrs.begin(), fromAddrs.end(), txout.scriptpubkey()))
					{
						continue;
					}

					ret = db_read_write.SetAllTransactionByAddress(txout.scriptpubkey(), tx.hash());
				    if (DBStatus::DB_SUCCESS != ret)
				    {
					    return false;
			        }
				}
			}
		}
		else
		{
			for (int j = 0; j < tx.vout_size(); j++)
			{
				CTxout txout = tx.vout(j);
				int64_t value = 0;
				ret = db_read_write.GetBalanceByAddress(txout.scriptpubkey(), value);
				if (DBStatus::DB_SUCCESS != ret && DBStatus::DB_NOT_FOUND != ret)
				{
					return false;
			    }
				value += txout.value();
				ret = db_read_write.SetBalanceByAddress(txout.scriptpubkey(), value);
				if (DBStatus::DB_SUCCESS != ret)
				{
					return false;
			    }
				ret = db_read_write.SetAllTransactionByAddress(txout.scriptpubkey(), tx.hash());
				if (DBStatus::DB_SUCCESS != ret)
				{
					return false;
			    }
			}
		}

		// 累加额外奖励
		if ( CheckTransactionType(tx) == kTransactionType_Award)
		{
			for (int j = 0; j < tx.vout_size(); j++)
			{
				CTxout txout = tx.vout(j);

				uint64_t awardTotal = 0;
				ret = db_read_write.GetAwardTotal(awardTotal);

				awardTotal += txout.value();
				if (DBStatus::DB_SUCCESS != db_read_write.SetAwardTotal(awardTotal) )
				{
					return false;
				}

				if (txout.value() >= 0)
				{
					// 累加账号的额外奖励总值
					uint64_t addrAwardTotal = 0;
					ret = db_read_write.GetAwardTotalByAddress(txout.scriptpubkey(), addrAwardTotal);
					if (DBStatus::DB_SUCCESS != ret && DBStatus::DB_NOT_FOUND != ret)
					{
						ERRORLOG("(AddBlock) GetAwardTotalByAddress failed !");
					}
					addrAwardTotal += txout.value();

					if (DBStatus::DB_SUCCESS != db_read_write.SetAwardTotalByAddress(txout.scriptpubkey(), addrAwardTotal))
					{
						ERRORLOG("(AddBlock) SetAwardTotalByAddress failed !");
						return false;
					}
				}

				// 累加账号的签名次数
				uint64_t signSum = 0;
				ret = db_read_write.GetSignNumByAddress(txout.scriptpubkey(), signSum);
				if (DBStatus::DB_SUCCESS != ret && DBStatus::DB_NOT_FOUND != ret)
				{
					ERRORLOG("(AddBlock) GetSignNumByAddress failed !");
				}
				++signSum;

				if (DBStatus::DB_SUCCESS != db_read_write.SetSignNumByAddress(txout.scriptpubkey(), signSum))
				{
					ERRORLOG("(AddBlock) SetSignNumByAddress failed !");
					return false;
				}
			}
		}
	}
	if(DBStatus::DB_SUCCESS != db_read_write.TransactionCommit())
	{
		ERRORLOG("(Addblock) TransactionCommit failed !");
		return false;
	}

	//{{ Delete pending transaction, 20201215
	for (int i = 0; i < cblock.txs_size(); i++)
	{
		CTransaction tx = cblock.txs(i);
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
	//}}

	//{{ Update the height of the self node, 20210323 Liu
    Singleton<PeerNode>::get_instance()->set_self_height();
	//}}
	
	//{{ Check http callback, 20210324 Liu
	if (Singleton<Config>::get_instance()->HasHttpCallback())
	{
		if (MagicSingleton<CBlockHttpCallback>::GetInstance()->IsRunning())
		{
			MagicSingleton<CBlockHttpCallback>::GetInstance()->AddBlock(cblock);
		}
	}
	//}}
	return true;
}

void CalcTransactionHash(CTransaction & tx)
{
	std::string hash = tx.hash();
	if (hash.size() != 0)
	{
		return;
	}

	CTransaction copyTx = tx;

	copyTx.clear_signprehash();

	std::string serTx = copyTx.SerializeAsString();

	hash = getsha256hash(serTx);
	tx.set_hash(hash);
}

bool ContainSelfSign(const CTransaction & tx)
{
	for (int i = 0; i != tx.signprehash_size(); ++i)
	{
		CSignPreHash signPreHash = tx.signprehash(i);

		char pub[2045] = {0};
		size_t pubLen = sizeof(pub);
		GetBase58Addr(pub, &pubLen, 0x00, signPreHash.pub().c_str(), signPreHash.pub().size());

		if (g_AccountInfo.isExist(pub))
		{
			INFOLOG("Signer [{}] Has Signed !!!", pub);
			return true;
		}
	}
	return false;
}

bool VerifySignPreHash(const CSignPreHash & signPreHash, const std::string & serTx)
{
	int pubLen = signPreHash.pub().size();
	char * rawPub = new char[pubLen * 2 + 2]{0};
	encode_hex(rawPub, signPreHash.pub().c_str(), pubLen);

	ECDSA<ECP, SHA1>::PublicKey publicKey;
	std::string sPubStr;
	sPubStr.append(rawPub, pubLen * 2);
	SetPublicKey(publicKey, sPubStr);

	delete [] rawPub;

	return VerifyMessage(publicKey, serTx, signPreHash.sign());
}

bool VerifyScriptSig(const CScriptSig & scriptSig, const std::string & serTx)
{
	std::string addr = GetBase58Addr(scriptSig.pub());

	int pubLen = scriptSig.pub().size();
	char * rawPub = new char[pubLen * 2 + 2]{0};
	encode_hex(rawPub, scriptSig.pub().c_str(), pubLen);

	ECDSA<ECP, SHA1>::PublicKey publicKey;
	std::string sPubStr;
	sPubStr.append(rawPub, pubLen * 2);
	SetPublicKey(publicKey, sPubStr);

	delete [] rawPub;

	return VerifyMessage(publicKey, serTx, scriptSig.sign());
}

bool isRedeemTx(const CTransaction &tx)
{
	std::vector<std::string> txOwners = TxHelper::GetTxOwner(tx);
	for (int i = 0; i < tx.vout_size(); ++i)
	{
		CTxout txout = tx.vout(i);
		if (txOwners.end() == find(txOwners.begin(), txOwners.end(), txout.scriptpubkey()))
		{
			return false;
		}
	}
	return true;
}

bool VerifyTransactionSign(const CTransaction & tx, int & verifyPreHashCount, std::string & txBlockHash, std::string txHash)
{
	DBReader db_reader;
	if ( CheckTransactionType(tx) == kTransactionType_Tx)
	{
		CTransaction newTx = tx;
		for (int i = 0; i != newTx.vin_size(); ++i)
		{
			CTxin * txin = newTx.mutable_vin(i);
			txin->clear_scriptsig();
		}

		newTx.clear_signprehash();
		newTx.clear_hash();

		std::string serNewTx = newTx.SerializeAsString();
		size_t encodeLen = serNewTx.size() * 2 + 1;
		unsigned char encode[encodeLen] = {0};
		memset(encode, 0, encodeLen);
		long codeLen = base64_encode((unsigned char *)serNewTx.data(), serNewTx.size(), encode);
		std::string encodeStr( (char *)encode, codeLen );

		std::string txHashStr = getsha256hash(encodeStr);
		txBlockHash = txHashStr;

		if(0 != txHashStr.compare(txHash))
		{
			ERRORLOG("Receive txhash({}) != Calculate txhash({})!", txHashStr, txHash);
			return false;
		}
		//验证转账者签名
		for (int i = 0; i != tx.vin_size(); ++i)
		{
			CTxin txin = tx.vin(i);
			if (! VerifyScriptSig(txin.scriptsig(), txHash))
			{
				ERRORLOG("Verify TX InputSign failed ... ");
				return false;
			}
		}
		
		std::vector<std::string> owner_pledge_utxo;
		auto extra = nlohmann::json::parse(tx.extra());
    	std::string type = extra["TransactionType"].get<std::string>();
		if (isRedeemTx(tx) && (type != TXTYPE_CONTRACT_EXECUTE))
		{
			std::vector<std::string> txOwners = TxHelper::GetTxOwner(tx);
			for (auto i : txOwners)
			{
				std::vector<string> utxos;
				if (DBStatus::DB_SUCCESS != db_reader.GetPledgeAddressUtxo(i, utxos))
				{
					ERRORLOG("GetPledgeAddressUtxo failed ... ");
					return false;
				}

				std::for_each(utxos.begin(), utxos.end(),
						[&](std::string &s){ s = s + "_" + i;}
				);

				std::vector<std::string> tmp_owner = owner_pledge_utxo;
				std::sort(utxos.begin(), utxos.end());
				std::set_union(utxos.begin(),utxos.end(),tmp_owner.begin(),tmp_owner.end(),std::back_inserter(owner_pledge_utxo));
				std::sort(owner_pledge_utxo.begin(), owner_pledge_utxo.end());
			}
		}

		std::vector<std::string> owner_utxo_tmp = TxHelper::GetUtxosByAddresses(TxHelper::GetTxOwner(tx));
		std::sort(owner_utxo_tmp.begin(), owner_utxo_tmp.end());

		std::vector<std::string> owner_utxo;
		std::set_union(owner_utxo_tmp.begin(),owner_utxo_tmp.end(),owner_pledge_utxo.begin(),owner_pledge_utxo.end(),std::back_inserter(owner_utxo));
		std::sort(owner_utxo.begin(), owner_utxo.end());

		std::vector<std::string> tx_utxo = TxHelper::GetUtxosByTx(tx);
    	std::sort(tx_utxo.begin(), tx_utxo.end());

		std::vector<std::string> v_union;
		std::set_union(owner_utxo.begin(),owner_utxo.end(),tx_utxo.begin(),tx_utxo.end(),std::back_inserter(v_union));
		std::sort(v_union.begin(), v_union.end());
		//v_union.erase(unique(v_union.begin(), v_union.end()), v_union.end());

		// 解质押交易UTXO有重复,去重
		std::set<std::string> tmpSet(v_union.begin(), v_union.end());
		v_union.assign(tmpSet.begin(), tmpSet.end());

		std::vector<std::string> v_diff;
		std::set_difference(v_union.begin(),v_union.end(),owner_utxo.begin(),owner_utxo.end(),std::back_inserter(v_diff));

		if(v_diff.size() > 0)
		{
			ERRORLOG("VerifyTransactionSign fail. not have enough utxo");
			return false;
		}

		// 判断手机或RPC交易时，交易签名者是否是交易发起人
		std::set<std::string> txVinVec;
		for(auto & vin : tx.vin())
		{
			std::string prevUtxo = vin.prevout().hash();
			std::string strTxRaw;
			if (DBStatus::DB_SUCCESS !=  db_reader.GetTransactionByHash(prevUtxo, strTxRaw))
			{
				ERRORLOG("get tx failed");
				return false;
			}

			CTransaction prevTx;
			prevTx.ParseFromString(strTxRaw);
			if (prevTx.hash().size() == 0)
			{
				return false;
			}
			
			std::string vinBase58Addr = GetBase58Addr(vin.scriptsig().pub());
			txVinVec.insert(vinBase58Addr);

			std::vector<std::string> txOutVec;
			for (auto & txOut : prevTx.vout())
			{
				txOutVec.push_back(txOut.scriptpubkey());
			}

			if (std::find(txOutVec.begin(), txOutVec.end(), vinBase58Addr) == txOutVec.end())
			{
				return false;
			}
		}

		std::vector<std::string> txOwnerVec;
		StringUtil::SplitString(tx.owner(), txOwnerVec, "_");

		std::vector<std::string> tmptxVinSet;
		tmptxVinSet.assign(txVinVec.begin(), txVinVec.end());

		std::vector<std::string> ivec(txOwnerVec.size() + tmptxVinSet.size());
		auto iVecIter = set_symmetric_difference(txOwnerVec.begin(), txOwnerVec.end(), tmptxVinSet.begin(), tmptxVinSet.end(), ivec.begin());
		ivec.resize(iVecIter - ivec.begin());

		if (ivec.size()!= 0)
		{
			return false;
		}
	}
	else
	{
		std::string strBestChainHash;
		if (DBStatus::DB_SUCCESS != db_reader.GetBestChainHash(strBestChainHash)) 
		{
			ERRORLOG("get best chain hash failed");
            return false;
        }
		if (strBestChainHash.size() != 0)
		{
			txBlockHash = COIN_BASE_TX_SIGN_HASH;
		}
	}
	//验证矿工签名
	CTransaction sign_tx = tx;
    for (auto &tx_sign_pre_hash1 : tx.signprehash())
    {
        sign_tx.clear_signprehash();
        for (auto &tx_sign_pre_hash2 : tx.signprehash())
        {
            if (tx_sign_pre_hash1.pub() == tx_sign_pre_hash2.pub())
            {
                break;
            }
            auto sign_pre_hash = sign_tx.add_signprehash();
            *sign_pre_hash = tx_sign_pre_hash2;
        }
	    std::string base64 ;
		base64.clear();
        base64 =  base64Encode(sign_tx.SerializeAsString());
		if (! VerifySignPreHash(tx_sign_pre_hash1, getsha256hash(base64)))
		{
			ERRORLOG("VerifyPreHashCount  VerifyMessage failed ... ");
			return false;
		}
	
        INFOLOG("Verify PreBlock HashSign succeed !!! VerifySignedCount[{}] -> {}", verifyPreHashCount + 1, getsha256hash(base64).c_str());
		(verifyPreHashCount)++ ;
    }
	return true;
}

unsigned get_extra_award_height() 
{
    const unsigned MAX_AWARD = 500000; //TODO test 10
    uint64_t award = 0;
    uint64_t top = 0;
    DBReader db_resder;
    auto ret = db_resder.GetBlockTop(top);
    if (DBStatus::DB_SUCCESS != ret)
	{
        return 0;
    }
    auto b_height = top;
    if (b_height <= MAX_AWARD) 
	{
        award = 2000;
    }
    return award;
}


bool IsNeedPackage(const CTransaction & tx)
{
	std::vector<std::string> owners = TxHelper::GetTxOwner(tx);
	return IsNeedPackage(owners);
}

bool IsNeedPackage(const std::vector<std::string> & fromAddr)
{
	bool bIsNeedPackage = true;
	for (auto &account : g_AccountInfo.AccountList)
	{
		if (fromAddr.end() != find(fromAddr.begin(), fromAddr.end(), account.first))
		{
			bIsNeedPackage = false;
		}
	}
	return bIsNeedPackage;
}


int new_add_ouput_by_signer(CTransaction &tx, bool bIsAward, const std::shared_ptr<TxMsg>& msg) 
{
    //获取共识数
	nlohmann::json extra = nlohmann::json::parse(tx.extra());
	int needVerifyPreHashCount = extra["NeedVerifyPreHashCount"].get<int>();
	int gasFee = extra["SignFee"].get<int>();
	int packageFee = extra["PackageFee"].get<int>();

    //额外奖励
    std::vector<int> award_list;
	std::vector<std::pair<std::string, uint64_t>> sin_award_list ;
	std::vector<std::pair<std::string, uint64_t>> list;
	uint64_t awardvalue = 0;
    int award = 2000000;
    getNodeAwardList(needVerifyPreHashCount, award_list, award);
    auto award_begin = award_list.begin();
    auto award_end = award_list.end();

	std::vector<std::string> signers;
	std::vector<std::string> award_signers;
	std::vector<uint64_t> num_arr;
	
	std::vector<std::string> txOwners = TxHelper::GetTxOwner(tx);
	for (int i = 0; i < tx.signprehash_size(); i++)
	{
        if (bIsAward) 
		{
            CSignPreHash txpre = tx.signprehash(i);
            int pubLen = txpre.pub().size();
            char *rawPub = new char[pubLen * 2 + 2]{0};
            encode_hex(rawPub, txpre.pub().c_str(), pubLen);
            ECDSA<ECP, SHA1>::PublicKey publicKey;
            std::string sPubStr;
            sPubStr.append(rawPub, pubLen * 2);
            SetPublicKey(publicKey, sPubStr);
            delete [] rawPub;

            for (int j = 0; j < msg->signnodemsg_size(); j++) 
			{
                std::string ownPubKey;
                GetPublicKey(publicKey, ownPubKey);
                char SignerCstr[2048] = {0};
                size_t SignerCstrLen = sizeof(SignerCstr);
                GetBase58Addr(SignerCstr, &SignerCstrLen, 0x00, ownPubKey.c_str(), ownPubKey.size());
                auto psignNodeMsg = msg->signnodemsg(j);
                std::string signpublickey = psignNodeMsg.signpubkey();
                const char * signpublickeystr = signpublickey.c_str();

                if (!strcmp(SignerCstr, signpublickeystr)) 
				{
                    std::string temp_signature = psignNodeMsg.signmsg();
                    psignNodeMsg.clear_signmsg();
                    std::string message = psignNodeMsg.SerializeAsString();

                    auto re = VerifyMessage(publicKey, message, temp_signature);
                    if (!re) 
					{
                        ERRORLOG("VerifyMessage err!!!!!!!");
						return -1;
					} 
					else 
					{
						if( 0 == j || 1 == j)
						{
							list.push_back(std::make_pair(signpublickeystr, awardvalue));
						}
                       	else if ( j > 1 )
						{
							award_signers.push_back(signpublickeystr);
						}
                    }
                }
            }
        } 
		else 
		{
			bool bIsLocal = false;    // 本节点发起的交易
			
			if (txOwners.size() == 0) 
			{
				continue;
			}

			char buf[2048] = {0};
            size_t buf_len = sizeof(buf);

			CSignPreHash signPreHash = tx.signprehash(i);
			GetBase58Addr(buf, &buf_len, 0x00, signPreHash.pub().c_str(), signPreHash.pub().size());
			signers.push_back(buf);

			if (txOwners[0] == buf)
			{
				bIsLocal = true;
			}

			uint64_t num = 0;
			// 默认第一个签名为发起方的时候 
            if (i == 0)
            {
				if (bIsLocal)
				{
					num = 0;
				}
				else
				{
					num = packageFee;
				}
            }
            else
            {
                if (!bIsAward) 
				{
					num = gasFee;
                } 
				else 
				{
                    num = (*award_begin);
                    ++award_begin;
                    if (award_begin == award_end) break;
                }
            }

            num_arr.push_back(num);
        }
	}

    if (bIsAward) 
	{
		int ret = ca_algorithm::GetAwardList(msg->top() + 1, tx.time(), award_signers,sin_award_list);
       	if (ret < 0 ) 
		{
			ERRORLOG("GetAwardList(block_height, tx_time, signers,award_list)");
			return -1;
		} 
		
		for(auto& item: sin_award_list )
		{
			list.push_back(std::make_pair(item.first, item.second));
		}
        for (auto &v : list)
		{
            CTxout * txout = tx.add_vout();
            txout->set_value(v.second);
            txout->set_scriptpubkey(v.first);
        }
    } 
	else 
	{
        for (int i = 0; i < needVerifyPreHashCount; ++i)
        {
            CTxout * txout = tx.add_vout();
            txout->set_value(num_arr[i]);
            txout->set_scriptpubkey(signers[i]);
            INFOLOG("Transaction signer [{}]", signers[i].c_str());
        }
    }

	return 0;
}

CTransaction CreateWorkTx(const CTransaction & tx, bool bIsAward, const std::shared_ptr<TxMsg>& psignNodeMsg ) 
{
    CTransaction retTx;
    if (tx.vin_size() == 0) 
	{
        return retTx;
    }
    retTx = tx;

    retTx.clear_vin();
    retTx.clear_vout();
    retTx.clear_hash();

    DBReader db_reader;
    unsigned int txIndex = 0;
    auto status = db_reader.GetTransactionTopByAddress(retTx.owner(), txIndex);
    if (DBStatus::DB_SUCCESS != status) {
		ERRORLOG("(CreateWorkTx) GetTransactionTopByAddress failed db_status:{} !", status);
    }

    txIndex++;
    retTx.set_n(txIndex);
   	retTx.set_identity(net_get_self_node_id());

	CTxin ownerTxin = tx.vin(0);

    CTxin * txin = retTx.add_vin();
    txin->set_sequence(0x00);
    CTxprevout * prevout = txin->mutable_prevout();
    prevout->set_n(0x00);
    CScriptSig * scriptSig = txin->mutable_scriptsig();
   
	*scriptSig = ownerTxin.scriptsig();

    if (!bIsAward) 
	{
        prevout->set_hash(tx.hash());
    }

    if ( 0 != new_add_ouput_by_signer(retTx, bIsAward, psignNodeMsg) )
	{
		retTx.clear_hash();
		return retTx;
	}

	uint64_t time = Singleton<TimeUtil>::get_instance()->getlocalTimestamp();
    retTx.set_time(time);
	retTx.clear_signprehash();

    for (int i = 0; i < retTx.vin_size(); i++)
    {
        CTxin * txin = retTx.mutable_vin(i);
        CScriptSig * scriptSig = txin->mutable_scriptsig();

        if (!bIsAward) 
		{
            scriptSig->set_sign(FEE_SIGN_STR);
        } 
		else 
		{
            scriptSig->set_sign(EXTRA_AWARD_SIGN_STR);
        }
        scriptSig->set_pub("");
    }

    CalcTransactionHash(retTx);

    return retTx;
}

void InitAccount(accountinfo *acc, const char *path)
{
	// 默认账户
	if (0 == g_testflag)
	{
		g_InitAccount = "16psRip78QvUruQr9fMzr8EomtFS1bVaXk";
	}
	else if (1 == g_testflag)
	{
		g_InitAccount = "19cjJN6pqEjwtVPzpPmM7VinMtXVKZXEDu";
	}
	else
	{
		g_InitAccount = "1vkS46QffeM4sDMBBjuJBiVkMQKY7Z8Tu";
	}

	if(NULL == path)
	{
		g_AccountInfo.path =  OWNER_CERT_PATH;
	}
	else
	{
		g_AccountInfo.path =  path;
	}

	if('/' != g_AccountInfo.path[g_AccountInfo.path.size()-1]) 
	{
		g_AccountInfo.path += "/";
	}

	if(access(g_AccountInfo.path.c_str(), F_OK))
    {
        if(mkdir(g_AccountInfo.path.c_str(), S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH))
        {
            assert(false);
            return;
        }
    }

    if(!acc)
	{
		ERRORLOG("InitAccount Failed ...");
        return;
	}
	
    DIR *dir;
    struct dirent *ptr;

    if ((dir=opendir(g_AccountInfo.path.c_str())) == NULL)
    {
		ERRORLOG("OPEN DIR {} ERROR ..." , g_AccountInfo.path.c_str());
		return;
    }

    while ((ptr=readdir(dir)) != NULL)
    {
        if(strcmp(ptr->d_name,".")==0 || strcmp(ptr->d_name,"..")==0)
		{
            continue;
		}
        else
        {
			DEBUGLOG("type[{}] filename[{}]", ptr->d_type, ptr->d_name);
            std::string bs58addr;
            if(0 == memcmp(ptr->d_name, OWNER_CERT_DEFAULT, strlen(OWNER_CERT_DEFAULT)))
            {
				std::string name(ptr->d_name);
				char *p = ptr->d_name + name.find('.') + 1;
				std::string ps(p);
                bs58addr.append(ptr->d_name, p + ps.find('.') - ptr->d_name );
                acc->GenerateKey(bs58addr.c_str(), true);
            }
            else
            {
				std::string name(ptr->d_name);
                bs58addr.append(ptr->d_name, ptr->d_name + name.find('.') - ptr->d_name);
                acc->GenerateKey(bs58addr.c_str(), false);
            }
        }
    }
	closedir(dir);

	if(!g_AccountInfo.GetAccSize())
    {
        g_AccountInfo.GenerateKey();
    }

	return;
}

std::string GetDefault58Addr()
{
	string sPubStr;
	GetPublicKey(g_publicKey, sPubStr);
	return GetBase58Addr(sPubStr);
}

/**
    vector最后一列为奖励总额
    amount总额为(共识数减一)乘(基数)
*/
int getNodeAwardList(int consensus, std::vector<int> &award_list, int amount, float coe) 
{
    using namespace std;

    //*奖励分配
    amount = amount*coe; //TODO
    consensus -= 1;
    consensus = consensus == 0 ? 1 : consensus;
    //auto award = consensus * base;
    int base {amount/consensus}; //平分资产 会有余
    int surplus = amount - base*consensus; //余
    award_list.push_back(amount);
    for (auto i = 1; i <= consensus; ++i) 
	{ //初始化 从1开始 除去总额
        award_list.push_back(base);
    }
    award_list[consensus] += surplus;

    //利率分配
    auto list_end_award {0};
    for (auto i = 1; i < consensus; ++i) 
	{
        award_list[i] -= i;
        list_end_award += i;
    }

    auto temp_consensus = consensus;
    auto diff_value = 10; //最后值差度(值越大相差越大)
    while (list_end_award > diff_value) 
	{
        if (list_end_award > diff_value && list_end_award < consensus) 
		{
            consensus = diff_value;
        }
        for (auto i = 1; i < consensus; ++i) 
		{ //XXX
            award_list[i] += 1; //XXX
        }
        if (list_end_award < consensus) 
		{
            list_end_award -= diff_value;
        } 
		else 
		{
            list_end_award -= consensus-1;
        }

    }

    award_list[temp_consensus] += list_end_award;
    sort(award_list.begin(), award_list.end());

    //去除负数情况
    while (award_list[0] <= 0) 
	{ //对称填负值
        for (auto i = 0; i < temp_consensus - 1; ++i) 
		{
            if (award_list[i] <= 0) 
			{
                if (award_list[i] == 0) 
				{
                    award_list[i] = 1;
                    award_list[temp_consensus-1-i] -= 1;
                } 
				else 
				{
                    award_list[i] = abs(award_list[i]) + 1;
                    award_list[temp_consensus-1-i] -= award_list[i] + (award_list[i] - 1);
                }
            } 
			else 
			{
                break;
            }
        }

        sort(award_list.begin(), award_list.end());
    }

    //最后一笔奖励不能等于上一笔 XXX
    while (award_list[temp_consensus-1] == award_list[temp_consensus-2]) 
	{
        award_list[temp_consensus-1] += 1;
        award_list[temp_consensus-2] -= 1;
        sort(award_list.begin(), award_list.end());
    }

    if (amount == 0) 
	{
        for (auto &v : award_list) 
		{
            v = 0;
        }
    }

    return 1;
}

bool ExitGuardian()
{
	std::string name = "ebpc_daemon";
	char cmd[128];
	memset(cmd, 0, sizeof(cmd));

	sprintf(cmd, "ps -ef | grep %s | grep -v grep | awk '{print $2}' | xargs kill -9 ",name.c_str());
	system(cmd);
	return true;
}


int HandleBuildBlockBroadcastMsg( const std::shared_ptr<BuildBlockBroadcastMsg>& msg, const MsgData& msgdata )
{
    // 判断版本是否兼容
    if( 0 != Util::IsVersionCompatible( msg->version() ) )
    {
        ERRORLOG("HandleBuildBlockBroadcastMsg IsVersionCompatible");
        return -1;
    }

    std::string serBlock = msg->blockraw();
    CBlock cblock;
    if(!cblock.ParseFromString(serBlock))
    {
        return -2;
    }
    // if(Singleton<PeerNode>::get_instance()->get_self_node().is_public_node)
	if(false)
    {
        bool flag = false;
        for(auto &tx : cblock.txs())
        {
            if(kTransactionType_Tx != CheckTransactionType(tx))
            {
                continue;
            }
            if(MagicSingleton<BlackListCache>::GetInstance()->AddBlack(tx))
            {
                ERRORLOG("BlackListCache VerifyDoubleSpend AddBlack");
                flag = true;
            }
        }
        if(flag)
        {
            return -3;
        }
    }
    MagicSingleton<BlockPoll>::GetInstance()->Add(Block(cblock));
    return 0;
}

// Create: receive pending transaction from network and add to cache,  20210114   Liu
int HandleTxPendingBroadcastMsg(const std::shared_ptr<TxPendingBroadcastMsg>& msg, const MsgData& msgdata)
{
	// 判断版本是否兼容
	if (Util::IsVersionCompatible(msg->version()) != 0)
	{
		ERRORLOG("HandleTxPendingBroadcastMsg IsVersionCompatible");
		return 0;
	}

	std::string transactionRaw = msg->txraw();
	CTransaction tx;
	tx.ParseFromString(transactionRaw);
	int result = MagicSingleton<TxVinCache>::GetInstance()->Add(tx, msg->prevblkheight(), false);
	DEBUGLOG("Receive pending transaction broadcast message result:{} ", result);
    return 0;
}


int VerifyBuildBlock(const CBlock & cblock)
{
	// 检查签名节点是否有异常账号
	std::vector<std::string> addrList;
	if ( 0 != ca_algorithm::GetAbnormalAwardAddrList(cblock.height(), addrList) )
	{
		ERRORLOG("GetAbnormalAwardAddrList failed!");
		return -1;
	}

	for (auto & tx : cblock.txs())
	{
		if (CheckTransactionType(tx) == kTransactionType_Award)
		{
			for (auto & txout : tx.vout())
			{
				if (addrList.end() != find(addrList.begin(), addrList.end(), txout.scriptpubkey()))
				{
					std::vector<std::string> txOwnerVec;
					StringUtil::SplitString(tx.owner(), txOwnerVec, "_");

					if (txOwnerVec.end() == find(txOwnerVec.begin(), txOwnerVec.end(), txout.scriptpubkey()))
					{
						if (txout.value() != 0)
						{
							ERRORLOG("sign addr Abnormal !");
							return -2;
						}
					}
				}
			}
		}
	}

	return 0;
}

int BuildBlock(std::string &recvTxString, const std::shared_ptr<TxMsg>& SendTxMsg)
{
	if (recvTxString.empty() || SendTxMsg->prevblkhash().empty())
	{
		ERRORLOG("BuildBlock: paramer is empty!");
		return -1;
	}

	CTransaction tx;
	if (!tx.ParseFromString(recvTxString))
	{
		ERRORLOG("BuildBlock: parse CTransaction failed!");
		return -2;
	}
	
	CBlock cblock = CreateBlock(tx, SendTxMsg);
	if (cblock.hash().empty())
	{
		ERRORLOG("BuildBlock: CreateBlock failed!");
		return -3;
	}
	
	std::string serBlock = cblock.SerializeAsString();
	if(MagicSingleton<BlockPoll>::GetInstance()->CheckConflict(cblock))
	{
		ERRORLOG("BuildBlock BlockPoll have CheckConflict!!!");
		return -4;
	}

	if (MagicSingleton<TxVinCache>::GetInstance()->IsBroadcast(tx.hash()))
	{
		ERRORLOG("Block has already broadcas");
		return -5;
	}
	ca_algorithm::PrintBlock(cblock);
	auto VerifyBlock = ca_algorithm::VerifyBlock(cblock);
	if(VerifyBlock < 0)
	{
		ERRORLOG("ca_algorithm::VerifyBlock() has been mistake :{}",VerifyBlock);
		std::cout << "ca_algorithm::VerifyBlock ret:" << VerifyBlock << std::endl;
		return -6;
	}
	MagicSingleton<TxVinCache>::GetInstance()->SetBroadcastFlag(tx.hash());
	MagicSingleton<TxVinCache>::GetInstance()->UpdateTransactionBroadcastTime(tx.hash());

	BuildBlockBroadcastMsg buildBlockBroadcastMsg;
	buildBlockBroadcastMsg.set_version(getVersion());
	buildBlockBroadcastMsg.set_blockraw(serBlock);

	net_broadcast_message<BuildBlockBroadcastMsg>(buildBlockBroadcastMsg, net_com::Priority::kPriority_High_1);
	
	INFOLOG("BuildBlock BuildBlockBroadcastMsg");
	return 0;
}

int IsBlockExist(const std::string & blkHash)
{
    DBReader db_reader;

	uint32_t blockHeight = 0;
	if (DBStatus::DB_SUCCESS != db_reader.GetBlockHeightByBlockHash(blkHash, blockHeight))
	{
		ERRORLOG("GetBlockHeightByBlockHash error !");
		return -2;
	}

	return 0;
}

int CalcTxTryCountDown(int needVerifyPreHashCount)
{
	if (needVerifyPreHashCount < g_MinNeedVerifyPreHashCount)
	{
		return 0;
	}
	else
	{
		return needVerifyPreHashCount - 4;
	}
}

int GetTxTryCountDwon(const TxMsg & txMsg)
{
	return txMsg.trycountdown();
}

int GetLocalDeviceOnlineTime(double_t & onlinetime)
{
	int ret = 0;
	std::string ownPubKey = GetDefault58Addr();
	std::vector<string> pledgeUtxos;
    DBReader db_reader;
	if (DBStatus::DB_SUCCESS != db_reader.GetPledgeAddressUtxo(ownPubKey, pledgeUtxos))
	{
		ret = 1;
	}

	uint64_t pledgeTime = time(NULL);
	uint64_t startTime = pledgeTime;
	for (auto & hash : pledgeUtxos)
	{
		std::string txRaw;
		if (DBStatus::DB_SUCCESS != db_reader.GetTransactionByHash(hash, txRaw))
		{
			ret = -2;
		}

		CTransaction utxoTx;
		utxoTx.ParseFromString(txRaw);

		for (auto & txout : utxoTx.vout())
		{
			if (txout.scriptpubkey() == VIRTUAL_ACCOUNT_PLEDGE)
			{
				if (txout.value() > (int64_t)g_TxNeedPledgeAmt && utxoTx.time() < pledgeTime)
				{
					pledgeTime = utxoTx.time();
				}
			}
		}
	}
	
	if (pledgeTime == startTime)
	{
		onlinetime = 1;
	}
	else
	{
		onlinetime = (time(NULL) - pledgeTime) / 3600 / 24;
		onlinetime = onlinetime > 1 ? onlinetime : 1;
	}

	return ret;
}

int SendTxMsg(const CTransaction & tx, const std::shared_ptr<TxMsg>& msg, uint32_t number)
{
	// 所有签过名的节点的id
	std::vector<std::string> signedIds;  
	for (auto & item : msg->signnodemsg())
	{
		signedIds.push_back( item.id() );
	}

	std::vector<std::string> sendid;
	int ret = FindSignNode(tx, msg->top(), number, signedIds, sendid);
	if( ret < 0)
	{
		ret -= 10;
		ERRORLOG("SendTxMsg failed, ret:{} sendid size: {}", ret, sendid.size());
		return ret;
	}
	if (sendid.empty())
	{
		ERRORLOG("SendTxMsg failed, sendid size is empty");
		return -1;
	}
	
	for (auto id : sendid)
	{
		cout<<"id = "<<id<<endl;
		net_send_message<TxMsg>(id.c_str(), *msg, net_com::Priority::kPriority_High_1);
	}

	return 0;
}

int RetrySendTxMsg(const CTransaction & tx, const std::shared_ptr<TxMsg>& msg)
{
	int tryCountDown = msg->trycountdown();
	tryCountDown--;
	msg->set_trycountdown(tryCountDown);
	if (tryCountDown <= 0)
	{
		return -1;
	}
	else
	{
		// 继续尝试转发
		return SendTxMsg(tx, msg, 1);
	}

	return 0;
}

int AddSignNodeMsg(const std::shared_ptr<TxMsg>& msg)
{
    DBReader db_reader;

	uint64_t mineSignatureFee = 0;
	db_reader.GetDeviceSignatureFee(mineSignatureFee );
	if(mineSignatureFee <= 0)
	{
		return -1;
	}

	std::string default58Addr = GetDefault58Addr();
	uint64_t addrAwardTotal = 0;
	db_reader.GetAwardTotalByAddress(default58Addr, addrAwardTotal);
	
	uint64_t signSum = 0;
	db_reader.GetSignNumByAddress(default58Addr, signSum);

	SignNodeMsg * psignNodeMsg = msg->add_signnodemsg();
	psignNodeMsg->set_id(net_get_self_node_id());
	psignNodeMsg->set_signpubkey( default58Addr );
	psignNodeMsg->set_gasfee( std::to_string( mineSignatureFee ) );

	std::string ser = psignNodeMsg->SerializeAsString();
	std::string signatureMsg;
	std::string strPub;
	GetSignString(ser, signatureMsg, strPub);
	psignNodeMsg->set_signmsg(signatureMsg);

	return 0;
}

int CheckTxMsg( const std::shared_ptr<TxMsg>& msg )
{
	// 取交易体
	CTransaction tx;
	if (!tx.ParseFromString(msg->tx()))
	{
		return -1;
	}
	
	// 取流转交易体中签名者
	std::vector<std::string> vMsgSigners;
	for (const auto & signer : msg->signnodemsg())
	{
		vMsgSigners.push_back(signer.signpubkey());
	}

	// 取交易体中签名者
	std::vector<std::string> vTxSigners;
	for (const auto & signInfo : tx.signprehash())
	{
		std::string addr = GetBase58Addr(signInfo.pub());
		vTxSigners.push_back(addr);
	}

	std::sort(vMsgSigners.begin(), vMsgSigners.end());
	std::sort(vTxSigners.begin(), vTxSigners.end());

	// 比对差异
	if (vMsgSigners != vTxSigners)
	{
		return -2;
	}

    uint64_t package_fee = 0;
    bool bIsPledgeTx = false;
    try 
	{
        // 取交易类型
        auto extra = nlohmann::json::parse(tx.extra());
        std::string txType = extra["TransactionType"].get<std::string>();
        if ( txType == TXTYPE_PLEDGE )
        {
            bIsPledgeTx = true;
        }
        extra["PackageFee"].get_to(package_fee);
    }
	catch (...) 
	{
        return -3;
    }
	
	// 取全网质押账号
	DBReader db_reader;
	std::vector<string> pledgeAddrs;
    auto status = db_reader.GetPledgeAddress(pledgeAddrs);
	if (DBStatus::DB_SUCCESS != status && DBStatus::DB_NOT_FOUND != status)
    {
        return -4;
    }

	// 判断是否为初始账号交易
	// 取交易发起方
	std::vector<std::string> vTxOwners = TxHelper::GetTxOwner(tx);
	bool bIsInitAccount = false;
	if (vTxOwners.end() != find(vTxOwners.begin(), vTxOwners.end(), g_InitAccount))
	{
		bIsInitAccount = true;
	}

	// 高度50以内质押和初始账户不需要校验
	if ( (bIsPledgeTx || bIsInitAccount) && msg->top() < g_minUnpledgeHeight)
	{
		return 0;
	}

    // 第一个签名节点不是交易的拥有者并且要收取打包费则必须作公网质押
    // 第二个签名节点必须是公网质押类型
    // 后面的签名节点必须是普通质押类型
    std::string sign_addr;
    if(tx.signprehash_size() > 0 && package_fee > 0)
    {
        sign_addr = GetBase58Addr(tx.signprehash(0).pub());
        if (vTxOwners.end() == std::find(vTxOwners.begin(), vTxOwners.end(), sign_addr))
        {
            int64_t pledge_time = ca_algorithm::GetPledgeTimeByAddr(sign_addr, PledgeType::kPledgeType_Public);
            if(pledge_time <= 0)
            {
                return -5;
            }
        }
    }
    if(tx.signprehash_size() > 1)
    {
        sign_addr = GetBase58Addr(tx.signprehash(1).pub());
        int64_t pledge_time = ca_algorithm::GetPledgeTimeByAddr(sign_addr, PledgeType::kPledgeType_Public);
        if(pledge_time <= 0)
        {
            return -6;
        }
    }
    for (int i = 2; i < tx.signprehash_size(); ++i)
    {
        sign_addr = GetBase58Addr(tx.signprehash(i).pub());
        int64_t pledge_time = ca_algorithm::GetPledgeTimeByAddr(sign_addr, PledgeType::kPledgeType_Node);
        if(pledge_time <= 0)
        {
            return -7;
        }
    }
    return 0;
}

int HandleTx( const std::shared_ptr<TxMsg>& msg, const MsgData& msgdata)
{
	std::string tx_hash;
    int ret = DoHandleTx( msg, tx_hash);
    return ret;
}

int DoHandleTx( const std::shared_ptr<TxMsg>& msg, std::string & tx_hash )
{
	// 判断版本是否兼容
	if( 0 != Util::IsVersionCompatible( msg->version() ) )
	{
		return -1;
	}
	
	// 判断高度是否符合
	if(!checkTop(msg->top()))
	{
		return -2;
	}

	// 检查本节点是否存在该交易的父块
	if (IsBlockExist(msg->prevblkhash()))
	{
		return -3;
	}

	INFOLOG("Recv TX ...");

	CTransaction tx;
	if (!tx.ParseFromString(msg->tx()))
	{
		return -4;
	}

	// 此次交易的共识数
	auto extra = nlohmann::json::parse(tx.extra());
    int needVerifyPreHashCount = extra["NeedVerifyPreHashCount"].get<int>();
	if (needVerifyPreHashCount < g_MinNeedVerifyPreHashCount || needVerifyPreHashCount > g_MaxNeedVerifyPreHashCount)
	{
		return -5;
	}

	tx.set_hash(ca_algorithm::CalcTransactionHash(tx));

	int meret = ca_algorithm::MemVerifyTransaction(tx);
	if(meret < 0 )
	{
		meret -= 100;
		cout<<"meret = "<< meret <<endl;
		return meret;
	}
	// 所有签过名的节点的id
	std::vector<std::string> signedIds;
	for (auto & item : msg->signnodemsg())
	{
		signedIds.push_back( item.id() );
	}
    int ret = CheckTxMsg(msg);
	if (ret < 0)
	{
		ret -= 200;
		return ret;
	}

	
	//验证别人的签名
	int verifyPreHashCount = 0;
	std::string txBlockHash;
	std::string blockPrevHash;
    bool rc = VerifyTransactionSign(tx, verifyPreHashCount, txBlockHash, msg->txencodehash());
	if(!rc)
	{
		if(tx.signprehash_size() == 0)
		{
			return -6;
		}
		else
		{
			return (0 != RetrySendTxMsg(tx, msg) ? -7 : 0);
		}
	}

	INFOLOG("verifyPreHashCount: {}", verifyPreHashCount);
	if ( verifyPreHashCount < needVerifyPreHashCount && ContainSelfSign(tx))
	{
        auto ret = (0 != RetrySendTxMsg(tx, msg) ? -8 : 0);
		return ret;
	}

	// 判断是否为质押交易
	bool isPledgeTx = false;
	std::string txType = extra["TransactionType"].get<std::string>();
	if (txType == TXTYPE_PLEDGE)
	{
		isPledgeTx = true;
	}

	// 判断是否为初始账号发起的交易
	bool isInitAccountTx = false;
	for (int i = 0; i < tx.vout_size(); ++i)
	{
		CTxout txout = tx.vout(i);
		if (txout.scriptpubkey() == g_InitAccount)
		{
			isInitAccountTx = true;
		}
	}

	// 账号未质押不允许签名转账交易, 但允许签名质押交易
	// verifyPreHashCount == 0 时为自己发起交易允许签名，verifyPreHashCount == needVerifyPreHashCount 时签名已经足够 开始建块
	if (!isPledgeTx && !isInitAccountTx && (verifyPreHashCount != 0 && verifyPreHashCount != needVerifyPreHashCount) )
	{
		std::string defauleAddr = GetDefault58Addr();
		PledgeType pledgeType = PledgeType::kPledgeType_Unknown; 
		const Node selfNode = Singleton<PeerNode>::get_instance()->get_self_node();
		uint64_t amount = 0;
        SearchPledge(defauleAddr, amount);
		if(selfNode.is_public_node && (amount >= g_TxNeedPublicPledgeAmt))
		{
			pledgeType = PledgeType::kPledgeType_Public;
		}
		else if(amount >= g_TxNeedPledgeAmt)
		{
			pledgeType = PledgeType::kPledgeType_Node;
		}

		int64_t pledge_time = ca_algorithm::GetPledgeTimeByAddr(selfNode.base58address, pledgeType);
		if (pledge_time < 0 && defauleAddr != g_InitAccount)
		{
			return (0 != RetrySendTxMsg(tx, msg) ? -9: 0);
		}
	}

	// 交易接收方禁止签名
	std::string default58Addr = GetDefault58Addr();
	for(int i = 0; i < tx.vout_size(); i++)
	{
		CTxout txout = tx.vout(i);
		if(default58Addr == txout.scriptpubkey())
		{
			auto txOwners = TxHelper::GetTxOwner(tx);
			if (txOwners.end() == std::find(txOwners.begin(), txOwners.end(), default58Addr))
			{
				auto ret = (0 != RetrySendTxMsg(tx, msg) ? -10 : 0);
                return ret;
			}
		}
	}

	// 自身开始签名
	if ( verifyPreHashCount < needVerifyPreHashCount)
	{
		CTransaction copyTx = tx;
		if (tx.signprehash_size() == 0)
		{
			copyTx.clear_signprehash();
		}
		std::string serCopyTx = copyTx.SerializeAsString();
		size_t encodeLen = serCopyTx.size() * 2 + 1;
		unsigned char encode[encodeLen] = {0};
		memset(encode, 0, encodeLen);
		long codeLen = base64_encode((unsigned char *)serCopyTx.data(), serCopyTx.size(), encode);
		std::string encodeStr( (char *)encode, codeLen );
		std::string txHashStr = getsha256hash(encodeStr);
		tx_hash = tx.hash();
		cout<<"tx_hash = "<<tx_hash<<endl;
		//自己来签名
		std::string strSignature;
		std::string strPub;
        GetSignString(txHashStr, strSignature, strPub);
		DEBUGLOG("GetDefault58Addr():{} add Sign ...",  GetDefault58Addr());

		CSignPreHash * signPreHash = tx.add_signprehash();
		signPreHash->set_sign(strSignature);
		signPreHash->set_pub(strPub);
		
		verifyPreHashCount ++;
		std::cout<<"签名次数是："<< verifyPreHashCount << std::endl;
	}
	
	uint64_t mineSignatureFee = 0;
    DBReader db_reader;
	db_reader.GetDeviceSignatureFee(mineSignatureFee );
	if(mineSignatureFee <= 0)
	{
		if (tx.signprehash_size() == 1)
		{
			// 发起方必须设置矿费
			return -11;
		}
		else
		{
			// 交易流转节点可重试发送
            auto ret = (0 != RetrySendTxMsg(tx, msg) ? -12 : 0);
            return ret;
		}
	}
	
	std::string ownBaseaddr  = net_get_self_node().base58address;
	int txOwnerPayGasFee = extra["SignFee"].get<int>();
	if (ownBaseaddr != tx.identity())
	{
		// 交易发起方所支付的手续费低于本节点设定的签名费时不予签名
		if(verifyPreHashCount != 0 && ((uint64_t)txOwnerPayGasFee) < mineSignatureFee )
		{
            auto ret = (0 != RetrySendTxMsg(tx, msg) ? -13 : 0);
            return ret;
		}
	}

	if((uint64_t)txOwnerPayGasFee < g_minSignFee || (uint64_t)txOwnerPayGasFee > g_maxSignFee)
	{
		ERRORLOG("Gas fee is out of range, txOwnerPayGasFee:{}", txOwnerPayGasFee);
		return -14;
	}
 
	std::string serTx = tx.SerializeAsString();
	msg->set_tx(serTx);

	if (verifyPreHashCount < needVerifyPreHashCount)
	{
		// 签名数不足时

		// 添加交易流转的签名信息
        int ret = AddSignNodeMsg(msg);
		if (0 != ret)
		{
			ret -= 300;
			ERRORLOG("Add SignNode Msg failed ! ret:{}", ret);
			return ret;
		}

		int nodeSize = needVerifyPreHashCount * 1;
		if(verifyPreHashCount > 1)   
		{
			// 除自己本身签名外，其他节点签名的时候转发节点数为1
			nodeSize = 1;
		}
		ret = SendTxMsg(tx, msg, nodeSize);
		cout<<"ret = "<<ret<<endl;
		if (0 != ret)
		{
			ret -= 400;
			ERRORLOG("Send TxMsg failed");
			return ret;
		}
		
		//只有一个签名,签名是自己,ip相等,添加到Cache中, 20201214
		if (ownBaseaddr == tx.identity() && tx.signprehash_size() == 1)
		{
			char buf[2048] = {0};
			size_t buf_len = sizeof(buf);
			string pub = tx.signprehash(0).pub();
			GetBase58Addr(buf, &buf_len, 0x00, pub.c_str(), pub.size());
			std::string strBuf(buf, strlen(buf));

			std::string default58Addr = GetDefault58Addr();
			if (strBuf == default58Addr)
			{
				int result = MagicSingleton<TxVinCache>::GetInstance()->Add(tx, msg->top());
				DEBUGLOG("Transaction add to Cach ({}) ({})", result, TxVinCache::TxToString(tx));
			}
		}

		DEBUGLOG("TX begin broadcast");
	}
	else
	{
		// 签名数达到共识数
		//std::string identity = net_get_self_node_id();
		std::string identity = net_get_self_node().base58address;
		if (identity != tx.identity())
		{
			// 如果是全网节点质押数
			
			// 添加交易流转的签名信息
            int ret = AddSignNodeMsg(msg);

			if (0 != ret)
			{
				ret -= 500;
				ERRORLOG("Add SignNode Msg failed ! ret:{}", ret);
				return ret;
			}
			net_send_message<TxMsg>(tx.identity(), *msg, net_com::Priority::kPriority_High_1);
			DEBUGLOG("TX Send to ip[{}] to Create Block ...", tx.identity().c_str());
		}
		else
		{
			// 返回到发起节点
			std::string blockHash;
			db_reader.GetBlockHashByTransactionHash(tx.hash(), blockHash);
			if(blockHash.length())
			{
				// 查询到说明已加块
				return -15;
			}

			if(msg->signnodemsg_size() != needVerifyPreHashCount)
			{
				return -16;
			}

			int ret = BuildBlock( serTx, msg);
			if( 0 != ret)
			{
				ret -= 600;
				ERRORLOG("HandleTx BuildBlock fail");
				return ret;
			}
		}
	}
	return 0;
}

std::map<int32_t, std::string> GetPreTxRawCode()
{
	std::map<int32_t, std::string> errInfo = {make_pair(0, "成功"), 
												make_pair(-1, "版本不兼容"),
												make_pair(-2, "获取主链信息失败"),
												};
	return errInfo;
}

int HandlePreTxRaw( const std::shared_ptr<TxMsgReq>& msg, const MsgData& msgdata )
{
	TxMsgAck txMsgAck;

	auto errInfo = GetPreTxRawCode();

	// 判断版本是否兼容
	if( 0 != Util::IsVersionCompatible( msg->version() ) )
	{
		ReturnAckCode<TxMsgAck>(msgdata, errInfo, txMsgAck, -1);
		return -1;
	}

	// 将交易信息体，公钥，签名信息反base64
	unsigned char serTxCstr[msg->sertx().size()] = {0};
	unsigned long serTxCstrLen = base64_decode((unsigned char *)msg->sertx().data(), msg->sertx().size(), serTxCstr);
	std::string serTxStr((char *)serTxCstr, serTxCstrLen);

	CTransaction tx;
	tx.ParseFromString(serTxStr);

	unsigned char strsignatureCstr[msg->strsignature().size()] = {0};
	unsigned long strsignatureCstrLen = base64_decode((unsigned char *)msg->strsignature().data(), msg->strsignature().size(), strsignatureCstr);

	unsigned char strpubCstr[msg->strsignature().size()] = {0};
	unsigned long strpubCstrLen = base64_decode((unsigned char *)msg->strpub().data(), msg->strpub().size(), strpubCstr);

	for (int i = 0; i < tx.vin_size(); i++)
	{
		CTxin * txin = tx.mutable_vin(i);
		txin->mutable_scriptsig()->set_sign( std::string( (char *)strsignatureCstr, strsignatureCstrLen ) );
		txin->mutable_scriptsig()->set_pub( std::string( (char *)strpubCstr, strpubCstrLen ) );
	}

	std::string serTx = tx.SerializeAsString();

	auto extra = nlohmann::json::parse(tx.extra());
	int needVerifyPreHashCount = extra["NeedVerifyPreHashCount"].get<int>();

	TxMsg phoneToTxMsg;
	phoneToTxMsg.set_version(getVersion());
	phoneToTxMsg.set_tx(serTx);
	phoneToTxMsg.set_txencodehash(msg->txencodehash());


    DBReader db_reader;
	std::string blockHash;
    if (DBStatus::DB_SUCCESS != db_reader.GetBestChainHash(blockHash) )
    {
    	ReturnAckCode<TxMsgAck>(msgdata, errInfo, txMsgAck, -2);
        return -2;
    }
    phoneToTxMsg.set_prevblkhash(blockHash);
	phoneToTxMsg.set_trycountdown(CalcTxTryCountDown(needVerifyPreHashCount));

	uint64_t top = 0;
	db_reader.GetBlockTop(top);
	phoneToTxMsg.set_top(top);

	auto txmsg = make_shared<TxMsg>(phoneToTxMsg);
	std::string txHash;
	int ret = DoHandleTx(txmsg, txHash);
	txMsgAck.set_txhash(txHash);
	if (ret != 0)
	{
		ret -= 1000;
	}
	DEBUGLOG("DoHandleTx ret:{} ", ret);
	ReturnAckCode<TxMsgAck>(msgdata, errInfo, txMsgAck, ret);
    return 0;
}

/* ====================================================================================  
 # 手机端交易流程：
 # 1，手机端发送CreateTxMsgReq请求到PC端，PC端调用HandleCreateTxInfoReq接口处理手机端请求；
 # 2，PC端在HandleCreateTxInfoReq中通过手机端发送的交易关键信息，打包成交易信息体，并将交易信息体进行base64之后，
 #    通过CreateTxMsgAck协议回传给手机端，CreateTxMsgAck协议中的txHash字段，是由交易体base64之后，再进
 #    行sha256，得到的hash值
 # 3，手机端接收到CreateTxMsgAck后，将自己计算的hash与PC传过来的txHash进行比较，不一致说明数据有误；一致，则调
 #    调用interface_NetMessageReqTxRaw对hash值进行签名。
 # 4，手机端回传TxMsgReq到PC端，PC端通过HandlePreTxRaw接口处理接收到的手机端的交易
 ==================================================================================== */

 std::map<int32_t, std::string> GetCreateTxInfoReqCode()
{
	std::map<int32_t, std::string> errInfo = {make_pair(0, "成功"), 
												make_pair(-1, "参数错误"),
												make_pair(-2, "版本不兼容"),
												make_pair(-3, "创建交易失败"),
												};
	return errInfo;
}
// markhere:modify
int HandleCreateTxInfoReq( const std::shared_ptr<CreateTxMsgReq>& msg, const MsgData& msgdata )
{
	CreateTxMsgAck createTxMsgAck;
	auto errInfo = GetCreateTxInfoReqCode();

	if ( msg == NULL )
	{
		ReturnAckCode<CreateTxMsgAck>(msgdata, errInfo, createTxMsgAck, -1);		
		return -1;
	}

	// 判断版本是否兼容
	if( 0 != Util::IsVersionCompatible( msg->version() ) )
	{
		ReturnAckCode<CreateTxMsgAck>(msgdata, errInfo, createTxMsgAck, -2);		
		return -2;
	}

	// 通过手机端发送的数据创建交易体
	std::vector<std::string> fromAddr;
    fromAddr.emplace_back(msg->from());
	std::map<std::string, int64_t> toAddr;
	double amount = stod( msg->amt() )* DECIMAL_NUM;
	toAddr[msg->to()] = amount;
	uint32_t needVerifyPreHashCount = stoi( msg->needverifyprehashcount() );
	double gasFee = stod( msg->minerfees() )* DECIMAL_NUM;

	CTransaction outTx;
	std::vector<TxHelper::Utxo> outVin;
	int ret = TxHelper::CreateTxTransaction(fromAddr, toAddr, needVerifyPreHashCount, gasFee, outTx, outVin);
	if (ret != 0)
	{
		ret -= 1000;
		ReturnAckCode<CreateTxMsgAck>(msgdata, errInfo, createTxMsgAck, ret);		
		return -3;
	}

	std::string txData = outTx.SerializeAsString();
	
	// 将交易体base64，方便传输，txHash用于手机端验证传输的数据是否正确
	size_t encodeLen = txData.size() * 2 + 1;
	unsigned char encode[encodeLen] = {0};
	memset(encode, 0, encodeLen);
	long codeLen = base64_encode((unsigned char *)txData.data(), txData.size(), encode);

	createTxMsgAck.set_txdata( (char *)encode, codeLen );
	std::string encodeStr((char *)encode, codeLen);
	std::string txEncodeHash = getsha256hash(encodeStr);
	createTxMsgAck.set_txencodehash(txEncodeHash);

	ReturnAckCode<CreateTxMsgAck>(msgdata, errInfo, createTxMsgAck, 0);
    return 0;
}


int HandleGetMacReq(const std::shared_ptr<GetMacReq>& getMacReq, const MsgData& from)
{	
	std::vector<string> outmac;
	get_mac_info(outmac);
	
	std::string macstr;
	for(auto &i:outmac)
	{
		macstr += i;
	}
	string md5 = getMD5hash(macstr.c_str());
	GetMacAck getMacAck;
	getMacAck.set_mac(md5);
	DEBUGLOG("getMD5hash:{}", md5);

	net_send_message(from, getMacAck);
    return 0;
}

int get_mac_info(vector<string> &vec)
{	 
	int fd;
    int interfaceNum = 0;
    struct ifreq buf[16] = {0};
    struct ifconf ifc;
    char mac[16] = {0};

    if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        ERRORLOG("socket ret:{}", fd);
        close(fd);
        return -1;
    }
    ifc.ifc_len = sizeof(buf);
    ifc.ifc_buf = (caddr_t)buf;
    if (!ioctl(fd, SIOCGIFCONF, (char *)&ifc))
    {
        interfaceNum = ifc.ifc_len / sizeof(struct ifreq);
        while (interfaceNum-- > 0)
        {
            if(string(buf[interfaceNum].ifr_name) == "lo")
            {
                continue;
            }
            if (!ioctl(fd, SIOCGIFHWADDR, (char *)(&buf[interfaceNum])))
            {
                memset(mac, 0, sizeof(mac));
                snprintf(mac, sizeof(mac), "%02x%02x%02x%02x%02x%02x",
                    (unsigned char)buf[interfaceNum].ifr_hwaddr.sa_data[0],
                    (unsigned char)buf[interfaceNum].ifr_hwaddr.sa_data[1],
                    (unsigned char)buf[interfaceNum].ifr_hwaddr.sa_data[2],

                    (unsigned char)buf[interfaceNum].ifr_hwaddr.sa_data[3],
                    (unsigned char)buf[interfaceNum].ifr_hwaddr.sa_data[4],
                    (unsigned char)buf[interfaceNum].ifr_hwaddr.sa_data[5]);
                // allmac[i++] = mac;
                std::string s = mac;
                vec.push_back(s);
            }
            else
            {
                ERRORLOG("ioctl: {}", strerror(errno));
                close(fd);
                return -1;
            }
        }
    }
    else
    {
        ERRORLOG("ioctl:{}", strerror(errno));
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

int SearchPledge(const std::string &address, uint64_t &pledgeamount, std::string pledgeType)
{
    DBReader db_reader;
	std::vector<string> utxos;
	auto status = db_reader.GetPledgeAddressUtxo(address, utxos);
	if (DBStatus::DB_SUCCESS != status)
	{
		ERRORLOG("GetPledgeAddressUtxo fail db_status:{}", status);
		return -1;
	}
	uint64_t total = 0;
	for (auto &item : utxos) 
    {
	 	std::string strTxRaw;
		if (DBStatus::DB_SUCCESS != db_reader.GetTransactionByHash(item, strTxRaw))
		{
			continue;
		}
		CTransaction utxoTx;
		utxoTx.ParseFromString(strTxRaw);

		nlohmann::json extra = nlohmann::json::parse(utxoTx.extra());
		nlohmann::json txInfo = extra["TransactionInfo"].get<nlohmann::json>();
		std::string txPledgeType = txInfo["PledgeType"].get<std::string>();
		// if (txPledgeType != pledgeType)
		// {
		// 	continue;
		// }

		for (int i = 0; i < utxoTx.vout_size(); i++)
		{
			CTxout txout = utxoTx.vout(i);
			if (txout.scriptpubkey() == VIRTUAL_ACCOUNT_PLEDGE)
			{
				total += txout.value();
			}
		}
    }
	pledgeamount = total;
	return 0;
}

int SearchPledge(const std::string &address, uint64_t &pledgeamount,  PledgeType type)
{
	DBReader db_reader;
	std::vector<string> utxos;
	auto status = db_reader.GetPledgeAddressUtxo(address, utxos);
	if (DBStatus::DB_SUCCESS != status)
	{
		ERRORLOG("GetPledgeAddressUtxo fail db_status:{}", status);
		return -1;
	}
	uint64_t total = 0;
	for (auto &item : utxos) 
    {
	 	std::string strTxRaw;
		if (DBStatus::DB_SUCCESS != db_reader.GetTransactionByHash(item, strTxRaw))
		{
			continue;
		}
		CTransaction utxoTx;
		utxoTx.ParseFromString(strTxRaw);

		nlohmann::json extra = nlohmann::json::parse(utxoTx.extra());
		nlohmann::json txInfo = extra["TransactionInfo"].get<nlohmann::json>();
		std::string txPledgeType = txInfo["PledgeType"].get<std::string>();

		if (type == PledgeType::kPledgeType_Node && txPledgeType != PLEDGE_NET_LICENCE)
		{	
			continue;
		}
		else if (type == PledgeType::kPledgeType_Public && txPledgeType != PLEDGE_PUBLIC_NET_LICENCE)
		{
			continue;
		}
		else if ((type == PledgeType::kPledgeType_All) && 
				(txPledgeType != PLEDGE_NET_LICENCE && txPledgeType != PLEDGE_PUBLIC_NET_LICENCE))
		{
			continue;
		}

		for (int i = 0; i < utxoTx.vout_size(); i++)
		{
			CTxout txout = utxoTx.vout(i);
			if (txout.scriptpubkey() == VIRTUAL_ACCOUNT_PLEDGE)
			{
				total += txout.value();
			}
		}
    }
	pledgeamount = total;
	return 0;
}

int GetAbnormalAwardAddrList(std::vector<std::string> & addrList)
{
    DBReader db_reader;
	const uint64_t heightRange = 1000;  // 检查异常的高度范围
	std::map<std::string, uint64_t> addrAwards;  // 存放账号和前500高度总奖励
	std::map<std::string, uint64_t> addrSignNum;  // 存放账号和前500高度总签名数

	uint64_t top = 0;
	if (DBStatus::DB_SUCCESS != db_reader.GetBlockTop(top))
	{
		ERRORLOG("(GetAbnormalAwardAddrList) GetBlockTop failed! ");
		return -2;
	}

	uint64_t minHeight = top > heightRange ? (int)top - heightRange : 0;  // 检查异常的最低高度

	for ( ; top != minHeight; --top)
	{
		std::vector<std::string> blockHashs;
		if (DBStatus::DB_SUCCESS != db_reader.GetBlockHashsByBlockHeight(top, blockHashs) )
		{
			ERRORLOG("(GetAbnormalAwardAddrList) GetBlockHashsByBlockHeight failed! ");
			return -3;
		}

		for (auto & hash : blockHashs)
		{
			std::string blockStr;
			if (DBStatus::DB_SUCCESS != db_reader.GetBlockByBlockHash(hash, blockStr))
			{
				ERRORLOG("(GetAbnormalAwardAddrList) GetBlockByBlockHash failed! ");
				return -4;
			}

			CBlock block;
			block.ParseFromString(blockStr);

			for (auto & tx : block.txs())
			{
				if (CheckTransactionType(tx) == kTransactionType_Award)
				{
					for (auto & txout : tx.vout())
					{
						if (txout.value() == 0)
						{
							continue;
						}

						// 总奖励
						auto iter = addrAwards.find(txout.scriptpubkey());
						if (addrAwards.end() != iter)
						{
							addrAwards[txout.scriptpubkey()] = iter->second + txout.value();
						}
						else
						{
							addrAwards[txout.scriptpubkey()] = txout.value();
						}

						// 总签名次数
						auto signNumIter = addrSignNum.find(txout.scriptpubkey());
						if (addrSignNum.end() != signNumIter)
						{
							addrSignNum[txout.scriptpubkey()] = (++signNumIter->second);
						}
						else
						{
							addrSignNum[txout.scriptpubkey()] = 1;
						}
						
					}
				}
			}
		}
	}

	if (addrAwards.size() == 0 || addrSignNum.size() == 0)
	{
		return 0;
	}

	std::vector<uint64_t> awards;  // 存放所有奖励值
	std::vector<uint64_t> vecSignNum;  // 存放所有奖励值
	for (auto & addrAward : addrAwards)
	{
		awards.push_back(addrAward.second);
	}

	for(auto & signNum : addrSignNum)
	{
		vecSignNum.push_back(signNum.second);
	}

	std::sort(awards.begin(), awards.end());
	std::sort(vecSignNum.begin(), vecSignNum.end());

	uint64_t awardQuarterNum = awards.size() * 0.25;
	uint64_t awardThreeQuarterNum = awards.size() * 0.75;
	
	uint64_t signNumQuarterNum = vecSignNum.size() * 0.25;
	uint64_t signNumThreeQuarterNum = vecSignNum.size() * 0.75;

	if (awardQuarterNum == awardThreeQuarterNum || signNumQuarterNum == signNumThreeQuarterNum)
	{
		return 0;
	}

	uint64_t awardQuarterValue = awards[awardQuarterNum];
	uint64_t awardThreeQuarterValue = awards[awardThreeQuarterNum];

	uint64_t signNumQuarterValue = vecSignNum[signNumQuarterNum];
	uint64_t signNumThreeQuarterValue = vecSignNum[signNumThreeQuarterNum];

	uint64_t awardDiffValue = awardThreeQuarterValue - awardQuarterValue;
	uint64_t awardUpperLimitValue = awardThreeQuarterValue + (awardDiffValue * 1.5);

	uint64_t signNumDiffValue = signNumThreeQuarterValue - signNumQuarterValue;
	uint64_t signNumUpperLimitValue = signNumThreeQuarterValue + (signNumDiffValue * 1.5);

	std::vector<std::string> awardList;
	std::vector<std::string> signNumList;
	for (auto & addrAward : addrAwards)
	{
		if (addrAward.second > awardUpperLimitValue)
		{
			awardList.push_back(addrAward.first);
		}
	}

	for (auto & addrSign : addrSignNum)
	{
		if (addrSign.second > signNumUpperLimitValue)
		{
			signNumList.push_back(addrSign.first);
		}
	}

	set_union(awardList.begin(), awardList.end(), signNumList.begin(), signNumList.end(), std::back_inserter(addrList));

	return 0;
}

// Select public node, 20211115  Liu
static bool FindPublicNode(const vector<Node>& nodelist, bool isPledge, uint64_t minerFee, const int nodeNumber, vector<string>& outPublicNodes)
{
	if (nodelist.empty())
	{
		ERRORLOG("nodelist is empty");
		return false;;
	}

	TRACELOG("nodelist size: {}", nodelist.size());

	std::vector<Node> pledgePublicNodes;
	std::vector<Node> unpledgePublicNodes;
	for (const auto& node : nodelist)
	{
		if (!node.is_public_node)
		{
			continue ;
		}
		if (node.sign_fee > minerFee)
		{
			continue ;
		}

		int64_t pledge_time = ca_algorithm::GetPledgeTimeByAddr(node.base58address, PledgeType::kPledgeType_Public);
		if (pledge_time > 0)
		{
			pledgePublicNodes.push_back(node);
		}
		else
		{
			unpledgePublicNodes.push_back(node);
		}
	}

	TRACELOG("pledgePublicNodes size: {}", pledgePublicNodes.size());
	TRACELOG("unpledgePublicNodes size: {}", unpledgePublicNodes.size());

	std::vector<Node> publicNodes;
	if (!pledgePublicNodes.empty())
	{
		publicNodes = pledgePublicNodes;
	}
	else if (pledgePublicNodes.empty() && isPledge)
	{
		publicNodes = unpledgePublicNodes;
	}

	if (!publicNodes.empty())
	{
		// Generate random number
		std::random_device device;
		std::mt19937 engine(device());
		std::uniform_int_distribution<size_t> dist(0, publicNodes.size()-1);

		const size_t randomCount = std::min(publicNodes.size(), (size_t)nodeNumber);
		std::unordered_set<size_t> randomIndexs;
		while (randomIndexs.size() < randomCount)
		{
			size_t random = dist(engine);
			randomIndexs.insert(random);
		}

		// If count of node is less than nodeNumber, fill it. Improve the success rate. 20211123  Liu
		std::vector<size_t> indexs(randomIndexs.begin(), randomIndexs.end());
		if (randomCount < nodeNumber)
		{
			while (indexs.size() < nodeNumber)
			{
				size_t random = dist(engine);
				indexs.push_back(random);
			}
		}

		for (const auto& i : indexs)
		{
			outPublicNodes.push_back(publicNodes[i].base58address);
		}

		return true;
	}
	return false;
}

// Random select node from list, 20211207  Liu
static void RandomSelectNode(const vector<Node>& nodes, size_t selectNumber, vector<string>& outNodes)
{
	if (nodes.empty())
	{
		return ;
	}
	// Generate random number
	std::random_device device;
	std::mt19937 engine(device());
	std::uniform_int_distribution<size_t> dist(0, nodes.size()-1);

	const size_t randomCount = std::min(nodes.size(), selectNumber);
	std::unordered_set<size_t> randomIndexs;
	while (randomIndexs.size() < randomCount)
	{
		size_t random = dist(engine);
		randomIndexs.insert(random);
	}

	for (const auto& i : randomIndexs)
	{
		outNodes.push_back(nodes[i].base58address);
	}
}

int FindSignNode(const CTransaction & tx, uint64_t top, const int nodeNumber, const std::vector<std::string> & signedNodes, std::vector<std::string> & nextNodes)
{
	// 参数判断
	if(nodeNumber <= 0)
	{
		return -1;
	}

	nlohmann::json txExtra = nlohmann::json::parse(tx.extra());
	uint64_t minerFee = txExtra["SignFee"].get<int>();

	bool isPledge = false;
	std::string txType = txExtra["TransactionType"].get<std::string>();
	if (txType == TXTYPE_PLEDGE)
	{
		isPledge = true;
	}

	bool isInitAccount = false;
	std::vector<std::string> vTxowners = TxHelper::GetTxOwner(tx);
	if (vTxowners.size() == 1 && vTxowners.end() != find(vTxowners.begin(), vTxowners.end(), g_InitAccount) )
	{
		isInitAccount = true;
	}

	const Node selfNode = Singleton<PeerNode>::get_instance()->get_self_node();
	std::vector<Node> nodelist;
	if (selfNode.is_public_node)
	{
		nodelist = Singleton<PeerNode>::get_instance()->get_nodelist();
	}
	else
	{
		nodelist = Singleton<NodeCache>::get_instance()->get_nodelist();
	}
	
	// 当前数据块高度为0时，GetPledgeAddress会返回错误，故不做返回判断
	std::vector<string> addresses; // 已质押地址
	std::vector<string> pledgeAddrs; // 待选的已质押地址
	{
        DBReader db_reader;
        //db_reader.GetPledgeAddress(addresses);
        db_reader.GetPledgeAddress(pledgeAddrs);
    }

	// 去除base58重复的节点
	std::map<std::string, std::string> tmpBase58Ids; // 临时去重
	std::vector<std::string> vRepeatedIds; // 重复的地址
	for (auto & node : nodelist)
	{
		// 查询重复base58地址
		auto ret = tmpBase58Ids.insert(make_pair(node.base58address, node.base58address));
		if (!ret.second)
		{
			vRepeatedIds.push_back(node.base58address);
		}
	}
	
	for (auto & base58address : vRepeatedIds)
	{
		auto iter = std::find_if(nodelist.begin(), nodelist.end(), [base58address](auto & node){
			return base58address == node.base58address;
		});
		if (iter != nodelist.end())
		{
			nodelist.erase(iter);
		}
	}

	std::string ownerID = net_get_self_node_id(); // 自己的节点

	// 取出交易双方
	std::vector<std::string> txAddrs;
	for(int i = 0; i < tx.vout_size(); ++i)
	{
		
		CTxout txout = tx.vout(i);
		txAddrs.push_back(txout.scriptpubkey());
	}

	//获取异常账户的节点
	uint64_t block_height = top + 1;
	std::vector<std::string>  abnormalAddrList ;
	 if ( 0 != ca_algorithm::GetAbnormalAwardAddrList(block_height, abnormalAddrList) )
	{
		ERRORLOG("(FindSignNode) GetAbnormalAwardAddrList failed! ");
		return -2;
	}

	bool isFirstSend = (ownerID == tx.identity() && tx.signprehash_size() == 1);

	std::vector<Node> nodeListTmp;
	for (auto iter = nodelist.begin(); iter != nodelist.end(); ++iter)
	{
		// 删除自身节点
		if (iter->base58address == ownerID)
		{
			DEBUGLOG("FindSignNode filter: owner {}", iter->base58address);
			continue;
		}

		// 删除交易双方节点
		if (txAddrs.end() != find(txAddrs.begin(), txAddrs.end(), iter->base58address))
		{
			DEBUGLOG("FindSignNode filter: exchanger {}", iter->base58address);
			continue;
		}

		//去除奖励值异常账号
		if (isFirstSend)
		{
			if (! iter->is_public_node)
			{
				continue;
			}
		}
		else
		{
			if (abnormalAddrList.end() != find(abnormalAddrList.begin(), abnormalAddrList.end(), iter->base58address))
			{
				DEBUGLOG("FindSignNode filter: abnormal addr {}", iter->base58address);
				continue;
			}
		}

		if (iter->height < top)
		{
			DEBUGLOG("FindSignNode filter: height {} less than top {}", iter->height, top);
			continue;
		}

		// 过滤已签名的
		if (find(signedNodes.begin(),signedNodes.end(),iter->base58address) != signedNodes.end())
		{
			DEBUGLOG("FindSignNode filter: already sign {}", iter->base58address);
			continue;
		}

		if (iter->sign_fee > minerFee)
		{
			DEBUGLOG("FindSignNode filter: high sign fee {}, minerfee {}", iter->sign_fee, minerFee);
			continue;
		}

		nodeListTmp.push_back(*iter);
	}

	nodelist = nodeListTmp;
	if (isFirstSend)
	{
		bool hasPledge = ((isPledge || isInitAccount) && (top < g_minUnpledgeHeight));
		std::vector<string> publicNodes;
		if (FindPublicNode(nodelist, hasPledge, minerFee, nodeNumber, publicNodes))
		{
			nextNodes = publicNodes;
		}
		else
		{
			ERRORLOG("(FindSignNode) Not found public node! ");
			return -3;
		}
	}
	else
	{
		// First separate to pledge and unpledge, then random select, 20211207  Liu
		vector<Node> pledgeNodes;
		vector<Node> unpledgeNodes;
		for (const auto& node : nodelist)
		{
			int64_t pledge_time = ca_algorithm::GetPledgeTimeByAddr(node.base58address, PledgeType::kPledgeType_Node);
			if (pledge_time > 0)
			{
				pledgeNodes.push_back(node);
			}
			else
			{
				unpledgeNodes.push_back(node);
			}
		}
		
		if (!pledgeNodes.empty())
		{
			RandomSelectNode(pledgeNodes, nodeNumber, nextNodes);
		}
		bool hasPledge = ((isPledge || isInitAccount) && (top < g_minUnpledgeHeight));
		if ((pledgeNodes.size() < nodeNumber) && hasPledge && !unpledgeNodes.empty())
		{
			size_t leftCount = nodeNumber - pledgeNodes.size();
			RandomSelectNode(unpledgeNodes, leftCount, nextNodes);
		}
	}

	// 过滤已签名的
	// for(auto signedId : signedNodes)
	// {
	// 	auto iter = std::find(nextNodes.begin(), nextNodes.end(), signedId);
	// 	if (iter != nextNodes.end())
	// 	{
	// 		nextNodes.erase(iter);
	// 	}
	// }
	// 筛选随机节点
	std::vector<std::string> sendid;
	if (nextNodes.size() <= (uint32_t)nodeNumber)
	{
		for (auto & nodeid  : nextNodes)
		{
			sendid.push_back(nodeid);
		}
	}
	else
	{
		std::set<int> rSet;
		int num = std::min((int)nextNodes.size(), nodeNumber);
		for(int i = 0; i < num; i++)
		{
			int j = rand() % nextNodes.size();
			rSet.insert(j);
		}

		for (auto i : rSet)
		{
			sendid.push_back(nextNodes[i]);
		}
	}
	nextNodes = sendid;
	return 0;
}

void GetOnLineTime()
{
	static time_t startTime = time(NULL);
    DBReadWriter db_readwriter;
	{
		// patch
		double minertime = 0.0;
		if (DBStatus::DB_SUCCESS != db_readwriter.GetDeviceOnLineTime(minertime))
		{
			if (DBStatus::DB_SUCCESS != db_readwriter.SetDeviceOnlineTime(0.00001157) )
			{
				ERRORLOG("(GetOnLineTime) SetDeviceOnlineTime failed!");
				return;
			}
		}

		if (minertime > 365.0)
		{
			if (DBStatus::DB_SUCCESS != db_readwriter.SetDeviceOnlineTime(0.00001157) )
			{
				ERRORLOG("(GetOnLineTime) SetDeviceOnlineTime failed!");
				return;
			}
		}
	}

	// 从有交易开始记录在线时长
	std::vector<std::string> vTxHashs;
	std::string addr = g_AccountInfo.DefaultKeyBs58Addr;
	auto db_get_status = db_readwriter.GetAllTransactionByAddreess(addr, vTxHashs);
	if (DBStatus::DB_SUCCESS != db_get_status)
	{
		ERRORLOG(" GetAllTransactionByAddreess failed db_get_status:{}!", db_get_status);
	}

	std::vector<Node> vnode = net_get_public_node();
	if(vTxHashs.size() >= 1 && vnode.size() >= 1 )
	{
		double onLineTime = 0.0;
		if (DBStatus::DB_SUCCESS != db_readwriter.GetDeviceOnLineTime(onLineTime) )
		{
			if (DBStatus::DB_SUCCESS != db_readwriter.SetDeviceOnlineTime(0.00001157) )
			{
				ERRORLOG("(GetOnLineTime) SetDeviceOnlineTime failed!");
				return;
			}
			return ;
		}

		time_t endTime = time(NULL);
		time_t dur = difftime(endTime, startTime);
		double durDay = (double)dur / (1*60*60*24);
		
		double minertime = 0.0;
		if (DBStatus::DB_SUCCESS != db_readwriter.GetDeviceOnLineTime(minertime))
		{
			ERRORLOG("(GetOnLineTime) GetDeviceOnLineTime failed!");
			return ;
		}

		double accumatetime = durDay + minertime; 
		if (DBStatus::DB_SUCCESS != db_readwriter.SetDeviceOnlineTime(accumatetime) )
		{
			ERRORLOG("(GetOnLineTime) SetDeviceOnlineTime failed!");
			return ;
		}
		
		startTime = endTime;
	}
	else
	{
		startTime = time(NULL);
	}
	
	if (DBStatus::DB_SUCCESS != db_readwriter.TransactionCommit())
	{
		ERRORLOG("(GetOnLineTime) TransactionCommit failed!");
		return ;
	}
}

int PrintOnLineTime()
{
	double  onlinetime;
    DBReader db_reader;
	auto db_status = db_reader.GetDeviceOnLineTime(onlinetime);
	int  day = 0,hour = 0,minute = 0,second = 0;

    double totalsecond = onlinetime *86400;

	cout<<"totalsecond="<<totalsecond<<endl;

	day = totalsecond/86400;
	cout<<"day="<< day <<endl;

	hour = (totalsecond - (day *86400))/3600;
	cout<<"hour="<< hour <<endl;

	minute = (totalsecond - (day *86400) - (hour *3600))/60;
	cout<<"minute="<< minute <<endl;

	second = (totalsecond - (day *86400) - (hour *3600) - (minute *60));
	cout<<"second="<< second <<endl;

	cout<<"day:"<<day<<"hour:"<<hour<<"minute:"<<minute <<"second:"<< second<<endl;

	if(DBStatus::DB_SUCCESS != db_status)
	{
		ERRORLOG("Get the device time  failed db_status:{}", db_status);
		return -1;
	}    
	return 0;        
}

int TestSetOnLineTime()
{
	cout<<"先查看在线时长然后设置设备在线时长"<<endl;
	PrintOnLineTime();
	std::cout <<"请输入设备的在线时长"<<std::endl;
	
	static double day  = 0.0,hour = 0.0,minute = 0.0,second = 0.0,totalsecond = 0.0,accumlateday=0.0;
	double inday  = 0.0,inhour = 0.0,inminute = 0.0,insecond = 0.0,intotalsecond = 0.0,inaccumlateday=0.0;
	cout<<"请输入设备在线天数"<<endl;
	std::cin >> inday;
	cout<<"请输入设备在线小时数"<<endl;
	std::cin >> inhour;
	cout<<"请输入设备在线分钟数"<<endl;
	std::cin >> inminute;
	cout<<"请输入设备在线秒数"<<endl;
	std::cin >> insecond;
	
	intotalsecond = inday *86400 + inhour *3600 + inminute*60 +insecond;
	inaccumlateday = intotalsecond/86400;
	
	cout<<"input day="<< inday<<endl;
	cout<<"input hour="<< inhour <<endl;
	cout<<"input minute="<< inminute <<endl;
	cout<<"input second="<< insecond <<endl;
	cout<< "input accumlateday= "<< inaccumlateday <<endl;
	cout<<"input totalsecond = "<<intotalsecond <<endl;
	day  += inday; 
	hour += inhour;
	minute += inminute;
	second += insecond;
	totalsecond = day *86400 + hour *3600 + minute*60 +second;
	accumlateday = totalsecond/86400;
    DBReadWriter db_readwriter;
  	auto db_status = db_readwriter.SetDeviceOnlineTime(accumlateday);
	if(db_status == 0)
	{
		INFOLOG("set the data success");
		return 0;
	}
	return -1;
}


/** 手机端连接矿机发起交易前验证矿机密码(测试连接是否成功) */
int HandleVerifyDevicePassword( const std::shared_ptr<VerifyDevicePasswordReq>& msg, const MsgData& msgdata )
{	
	VerifyDevicePasswordAck verifyDevicePasswordAck;
	verifyDevicePasswordAck.set_version(getVersion());

	// 判断版本是否兼容
	if( 0 != Util::IsVersionCompatible( msg->version() ) )
	{
		verifyDevicePasswordAck.set_code(-1);
		verifyDevicePasswordAck.set_message("version error!");
		net_send_message<VerifyDevicePasswordAck>(msgdata, verifyDevicePasswordAck);
		ERRORLOG("HandleBuileBlockBroadcastMsg IsVersionCompatible");
		return 0;
	}

	string  minerpasswd = Singleton<DevicePwd>::get_instance()->GetDevPassword();
	std::string passwordStr = generateDeviceHashPassword(msg->password());
	std::string password = msg->password();
    std::string hashOriPass = generateDeviceHashPassword(password);
    std::string targetPassword = Singleton<DevicePwd>::get_instance()->GetDevPassword();
    auto pCPwdAttackChecker = MagicSingleton<CPwdAttackChecker>::GetInstance(); 
   
    uint32_t minutescount ;
    bool retval = pCPwdAttackChecker->IsOk(minutescount);
    if(retval == false)
    {
        std::string minutescountStr = std::to_string(minutescount);
        verifyDevicePasswordAck.set_code(-31);
        verifyDevicePasswordAck.set_message(minutescountStr);
		
        ERRORLOG("Because there are 3 consecutive errors, you can enter {} seconds later.", minutescount);
		net_send_message<VerifyDevicePasswordAck>(msgdata, verifyDevicePasswordAck);
        return 0;
    }

    if(hashOriPass.compare(targetPassword))
    {
        DEBUGLOG("Begin to count, beacause password was entered incorrectly.");
       if(pCPwdAttackChecker->Wrong())
       {
            ERRORLOG("Password error.");
            verifyDevicePasswordAck.set_code(-2);
            verifyDevicePasswordAck.set_message("密码输入错误");
			net_send_message<VerifyDevicePasswordAck>(msgdata, verifyDevicePasswordAck);
            return 0;
       } 
	   else
	   {
			ERRORLOG("Enter the password incorrectly for the third time.");
			verifyDevicePasswordAck.set_code(-30);
			verifyDevicePasswordAck.set_message("第三次输入密码错误");
			net_send_message<VerifyDevicePasswordAck>(msgdata, verifyDevicePasswordAck);
			return 0;
	   }  
    }
    else 
    {
        DEBUGLOG("HandleVerifyDevicePassword Password count reset to 0.");
        pCPwdAttackChecker->Right();
		verifyDevicePasswordAck.set_code(0);
        verifyDevicePasswordAck.set_message("密码输入正确");
		net_send_message<VerifyDevicePasswordAck>(msgdata, verifyDevicePasswordAck);
    }

	if (hashOriPass != targetPassword) 
    {
        verifyDevicePasswordAck.set_code(-2);
        verifyDevicePasswordAck.set_message("password error!");
        net_send_message<VerifyDevicePasswordAck>(msgdata, verifyDevicePasswordAck);
        ERRORLOG("password error!");
        return 0;
    }
	return 0;
}

// 手机端连接矿机发起交易
int HandleCreateDeviceTxMsgReq(const std::shared_ptr<CreateDeviceTxMsgReq>& msg, const MsgData& msgdata)
{
	DEBUGLOG("HandleCreateDeviceTxMsgReq");
	// 手机端回执消息
	TxMsgAck txMsgAck;
	txMsgAck.set_version(getVersion());

	// 判断版本是否兼容
	if( 0 != Util::IsVersionCompatible( msg->version() ) )
	{
		txMsgAck.set_code(-1);
		txMsgAck.set_message("version error!");
		net_send_message<TxMsgAck>(msgdata, txMsgAck, net_com::Priority::kPriority_Middle_1);
		ERRORLOG("HandleCreateDeviceTxMsgReq IsVersionCompatible");
		return 0;
	}

	// 判断矿机密码是否正确
    std::string password = msg->password();
    std::string hashOriPass = generateDeviceHashPassword(password);
    std::string targetPassword = Singleton<DevicePwd>::get_instance()->GetDevPassword();
	auto pCPwdAttackChecker = MagicSingleton<CPwdAttackChecker>::GetInstance(); 
  
    uint32_t minutescount ;
    bool retval = pCPwdAttackChecker->IsOk(minutescount);
    if(retval == false)
    {
        std::string minutescountStr = std::to_string(minutescount);
        txMsgAck.set_code(-31);
        txMsgAck.set_message(minutescountStr);
		net_send_message<TxMsgAck>(msgdata, txMsgAck, net_com::Priority::kPriority_Middle_1);
        ERRORLOG("Because there are 3 consecutive errors, you can enter {} seconds later", minutescount);
        return 0;
    }

    if(hashOriPass.compare(targetPassword))
    {
        DEBUGLOG("Begin to count, beacause password was entered incorrectly.");
       if(pCPwdAttackChecker->Wrong())
       {
            ERRORLOG("Password error.");
            txMsgAck.set_code(-5);
            txMsgAck.set_message("密码输入错误");
			net_send_message<TxMsgAck>(msgdata, txMsgAck, net_com::Priority::kPriority_Middle_1);
            return 0;
       }
	   else
	   {
			txMsgAck.set_code(-30);
			txMsgAck.set_message("第三次密码输入错误");
			net_send_message<TxMsgAck>(msgdata, txMsgAck, net_com::Priority::kPriority_Middle_1);
			return 0;
	   }
    }
    else 
    {
        DEBUGLOG("HandleCreateDeviceTxMsgReq Input of password reset to 0.");
        pCPwdAttackChecker->Right();
    }

    if (hashOriPass != targetPassword) 
    {
        txMsgAck.set_code(-5);
        txMsgAck.set_message("password error!");
        net_send_message<TxMsgAck>(msgdata, txMsgAck, net_com::Priority::kPriority_Middle_1);
        ERRORLOG("password error!");
        return 0;
    }

	std::string strFromAddr = msg->from();  
    if(!strFromAddr.empty())
    {
        if (!g_AccountInfo.SetKeyByBs58Addr(g_privateKey, g_publicKey, strFromAddr.c_str())) {
            DEBUGLOG("Illegal account.");
            return -2;
        }
    }
    else
    {
        g_AccountInfo.SetKeyByBs58Addr(g_privateKey, g_publicKey, NULL);
    }

    std::vector<std::string> fromAddr;
    fromAddr.emplace_back(strFromAddr);

    std::map<std::string, int64_t> toAddrAmount;
	std::string strToAddr = msg->to();
	uint64_t amount = (stod(msg->amt()) + FIX_DOUBLE_MIN_PRECISION) * DECIMAL_NUM;
    toAddrAmount[strToAddr] = amount;
	uint32_t needVerifyPreHashCount = std::stoul(msg->needverifyprehashcount());
    uint64_t gasFee =  (stod(msg->minerfees()) + FIX_DOUBLE_MIN_PRECISION )* DECIMAL_NUM ;

	CTransaction outTx;
	std::vector<TxHelper::Utxo> outVin;
	if (TxHelper::CreateTxTransaction(fromAddr, toAddrAmount, needVerifyPreHashCount, gasFee, outTx, outVin) != 0)
	{
		ERRORLOG("CreateTxTransaction error!!");
		return -4;
	}

    std::string serTx;
    std::string encodeStrHash;
    if(TxHelper::SignTransaction(outVin, outTx, serTx, encodeStrHash) != 0)
    {
		ERRORLOG("SignTransaction error!!");
		return -5;        
    }

	TxMsg txMsg;
	txMsg.set_version(getVersion());
	txMsg.set_tx(serTx);
	txMsg.set_txencodehash(encodeStrHash);

	DBReader db_reader;
	uint64_t top = 0;
	db_reader.GetBlockTop(top);
	txMsg.set_top(top);

	std::string blockHash;
	if (DBStatus::DB_SUCCESS != db_reader.GetBestChainHash(blockHash))
	{
		ERRORLOG("GetBestChainHash return no zero");
		return -1;
	}
	txMsg.set_prevblkhash(blockHash);
	txMsg.set_trycountdown(CalcTxTryCountDown(needVerifyPreHashCount));

	auto tx_msg = make_shared<TxMsg>(txMsg);

	std::string txHash;
	int ret = DoHandleTx(tx_msg, txHash);
	DEBUGLOG("Transaction result，ret:{}  txHash：{}", ret, txHash);
	
	if(ret < 0)
	{
		txMsgAck.set_code(-4);
		txMsgAck.set_message("CreateTx failed!");
		net_send_message<TxMsgAck>(msgdata, txMsgAck, net_com::Priority::kPriority_Middle_1);

		ERRORLOG("HandleCreateDeviceTxMsgReq CreateTx failed!!");
		return 0;
	}

	txMsgAck.set_code(0);
	txMsgAck.set_message("CreateTx successful! Waiting for broadcast!");
	net_send_message<TxMsgAck>(msgdata, txMsgAck, net_com::Priority::kPriority_Middle_1);

	DEBUGLOG("HandleCreateDeviceTxMsgReq CreateTx successful! Waiting for broadcast! ");
	return 0;
}

std::map<int32_t, std::string> GetCreateDeviceMultiTxMsgReqCode()
{
	std::map<int32_t, std::string> errInfo = {make_pair(0, "成功"), 
												make_pair(-1, "版本不兼容"),
												make_pair(-2, "三次密码输入错误"),
												make_pair(-3, "密码输入错误"),
												make_pair(-4, "第三次密码输入错误"),
												make_pair(-5, "密码不正确"),
												make_pair(-6, "交易签名失败"),
												make_pair(-7, "获得主链错误"),
												};

	return errInfo;												
}

int HandleCreateDeviceMultiTxMsgReq(const std::shared_ptr<CreateDeviceMultiTxMsgReq>& msg, const MsgData& msgdata)
{
    TxMsgAck txMsgAck;
	auto errInfo = GetCreateDeviceMultiTxMsgReqCode();
    
    if( 0 != Util::IsVersionCompatible( msg->version() ) )
	{		
		ReturnAckCode<TxMsgAck>(msgdata, errInfo, txMsgAck, -1);
		return -1;
	}

    // 判断矿机密码是否正确
    std::string password = msg->password();
    std::string hashOriPass = generateDeviceHashPassword(password);
    std::string targetPassword = Singleton<DevicePwd>::get_instance()->GetDevPassword();
    auto pCPwdAttackChecker = MagicSingleton<CPwdAttackChecker>::GetInstance(); 
   
    uint32_t minutescount;
    bool retval = pCPwdAttackChecker->IsOk(minutescount);   
	if(retval == false)
    {
		ERRORLOG("Because there are 3 consecutive errors, you can enter {} seconds later.", minutescount);
        std::string minutescountStr = std::to_string(minutescount);
        ReturnAckCode<TxMsgAck>(msgdata, errInfo, txMsgAck, -2, minutescountStr);
        return -2;
    }

    if(hashOriPass.compare(targetPassword))
    {
        DEBUGLOG("Begin to count, beacause password was entered incorrectly.");
       if(pCPwdAttackChecker->Wrong())
       {
            ERRORLOG("Password error.");
            ReturnAckCode<TxMsgAck>(msgdata, errInfo, txMsgAck, -3);
            return -3;
       } 
       else
       {
			// 第三次密码输入错误
    		ReturnAckCode<TxMsgAck>(msgdata, errInfo, txMsgAck, -4);
            return -4;
       }
    }
    else 
    {
        DEBUGLOG("Input of password reset to 0.");
        pCPwdAttackChecker->Right();
    }
   
    if (hashOriPass != targetPassword) 
    {
		// 密码不正确
        ReturnAckCode<TxMsgAck>(msgdata, errInfo, txMsgAck, -5);
        return -5;
    }

    std::vector<std::string> fromAddr;
    for (int i = 0; i < msg->from_size(); ++i)
    {
        std::string fromAddrStr = msg->from(i);
        fromAddr.push_back(fromAddrStr);
    }
    std::map<std::string, int64_t> toAddr;
    for (int i = 0; i < msg->to_size(); ++i)
    {
        ToAddr toAddrInfo = msg->to(i);
        int64_t amount = (std::stod(toAddrInfo.amt()) + FIX_DOUBLE_MIN_PRECISION) * DECIMAL_NUM;
        toAddr.insert( make_pair(toAddrInfo.toaddr(), amount ) );
    }
    uint32_t needVerifyPreHashCount = stoi( msg->needverifyprehashcount() );
    uint64_t gasFee = (std::stod( msg->gasfees()) + FIX_DOUBLE_MIN_PRECISION) * DECIMAL_NUM;

    CTransaction outTx;
	std::vector<TxHelper::Utxo> outVin;
    int ret = TxHelper::CreateTxTransaction(fromAddr,toAddr, needVerifyPreHashCount, gasFee, outTx, outVin);
	if(ret != 0)
	{
		ret -= 1000;
		ReturnAckCode<TxMsgAck>(msgdata, errInfo, txMsgAck, ret);
		return ret;
	}

    std::string serTx;
    std::string encodeStrHash;
    if(TxHelper::SignTransaction(outVin, outTx, serTx, encodeStrHash) != 0)
    {
		ERRORLOG("SignTransaction error!!");
		ReturnAckCode<TxMsgAck>(msgdata, errInfo, txMsgAck, -6);
		return -6;        
    }

	TxMsg txMsg;
    txMsg.set_version(getVersion());

    txMsg.set_tx(serTx);
	txMsg.set_txencodehash( encodeStrHash );

	DBReader db_reader;
    std::string blockHash;
    if (DBStatus::DB_SUCCESS != db_reader.GetBestChainHash(blockHash) )
    {
        ReturnAckCode<TxMsgAck>(msgdata, errInfo, txMsgAck, -7);
        return -7;
    }
    txMsg.set_prevblkhash(blockHash);
    txMsg.set_trycountdown(CalcTxTryCountDown(needVerifyPreHashCount));
    
	uint64_t top = 0;
	db_reader.GetBlockTop(top);
	txMsg.set_top(top);

    auto pTxMsg = make_shared<TxMsg>(txMsg);
	std::string txHash;
	ret = DoHandleTx(pTxMsg, txHash);
	txMsgAck.set_txhash(txHash);
	if (ret != 0)
	{
		ret -= 2000;
	}
	DEBUGLOG("DoHandleTx ret:{}", ret);
	ReturnAckCode<TxMsgAck>(msgdata, errInfo, txMsgAck, ret);
    return 0;
}

std::map<int32_t, std::string> GetCreateMultiTxReqCode()
{
	std::map<int32_t, std::string> errInfo = {make_pair(0, "成功"), 
												make_pair(-1, "版本不兼容"), 
												};

	return errInfo;												
}

int HandleCreateMultiTxReq( const std::shared_ptr<CreateMultiTxMsgReq>& msg, const MsgData& msgdata )
{
    // 手机端回执消息体
    CreateMultiTxMsgAck createMultiTxMsgAck;
	auto errInfo = GetCreateMultiTxReqCode();

    // 判断版本是否兼容
    if( 0 != Util::IsVersionCompatible( msg->version() ) )
	{
		ReturnAckCode<CreateMultiTxMsgAck>(msgdata, errInfo, createMultiTxMsgAck, -1);
		return -1;
	}

    std::vector<std::string> fromAddr;
    for (int i = 0; i < msg->from_size(); ++i)
    {
        std::string fromAddrStr = msg->from(i);
        fromAddr.push_back(fromAddrStr);
    }
    std::map<std::string, int64_t> toAddr;
    for (int i = 0; i < msg->to_size(); ++i)
    {
        ToAddr toAddrInfo = msg->to(i);
        int64_t amount = std::stod(toAddrInfo.amt()) * DECIMAL_NUM;
		std::string to_addr= toAddrInfo.toaddr();
		toAddr.insert(make_pair(to_addr, amount));
	}
	uint32_t needVerifyPreHashCount = stoi(msg->needverifyprehashcount());
    uint64_t minerFees = stod( msg->minerfees() ) * DECIMAL_NUM;

	CTransaction outTx;
	std::vector<TxHelper::Utxo> outVin;
    int ret = TxHelper::CreateTxTransaction(fromAddr, toAddr, needVerifyPreHashCount, minerFees, outTx, outVin);
    if(ret != 0)
	{
		ret -= 1000;
        ReturnAckCode<CreateMultiTxMsgAck>(msgdata, errInfo, createMultiTxMsgAck, ret);
		return 0;
	}

    std::string serTx = outTx.SerializeAsString();

	size_t encodeLen = serTx.size() * 2 + 1;
	unsigned char encode[encodeLen] = {0};
	memset(encode, 0, encodeLen);
	long codeLen = base64_encode((unsigned char *)serTx.data(), serTx.size(), encode);
	std::string encodeStr( (char *)encode, codeLen );

	std::string encodeStrHash = getsha256hash(encodeStr);

    createMultiTxMsgAck.set_txdata(encodeStr);
    createMultiTxMsgAck.set_txencodehash(encodeStrHash);
    
	ReturnAckCode<CreateMultiTxMsgAck>(msgdata, errInfo, createMultiTxMsgAck, 0);
    return 0;
}


std::map<int32_t, std::string> GetMultiTxReqCode()
{
	std::map<int32_t, std::string> errInfo = {make_pair(0, "成功"), 
												make_pair(-1, "版本不兼容"),
												make_pair(-2, "签名不正确"),
												make_pair(-3, "获得主链错误"),
												};

	return errInfo;												
}
int HandleMultiTxReq( const std::shared_ptr<MultiTxMsgReq>& msg, const MsgData& msgdata )
{
    TxMsgAck txMsgAck;

	auto errInfo = GetMultiTxReqCode();

    if( 0 != Util::IsVersionCompatible( msg->version() ) )
	{
		ReturnAckCode<TxMsgAck>(msgdata, errInfo, txMsgAck, -1);
		return -1;
	}

    std::string serTxStr(base64Decode(msg->sertx()));

    CTransaction tx;
    tx.ParseFromString(serTxStr);

    std::vector<SignInfo> vSignInfo;
    for (int i = 0; i < msg->signinfo_size(); ++i)
    {
        SignInfo signInfo = msg->signinfo(i);
        vSignInfo.push_back(signInfo);
    }

    if (vSignInfo.size() <= 0)
    {
        ReturnAckCode<TxMsgAck>(msgdata, errInfo, txMsgAck, -2);
		return -2;
    }

    // 一对多交易只有一个发起方，取第0个
    SignInfo signInfo = msg->signinfo(0);
    unsigned char strsignatureCstr[signInfo.signstr().size()] = {0};
	unsigned long strsignatureCstrLen = base64_decode((unsigned char *)signInfo.signstr().data(), signInfo.signstr().size(), strsignatureCstr);

	unsigned char strpubCstr[signInfo.pubstr().size()] = {0};
	unsigned long strpubCstrLen = base64_decode((unsigned char *)signInfo.pubstr().data(), signInfo.pubstr().size(), strpubCstr);

    for (int i = 0; i < tx.vin_size(); ++i)
    {
        CTxin * txin = tx.mutable_vin(i);

        txin->mutable_scriptsig()->set_sign( strsignatureCstr, strsignatureCstrLen );
        txin->mutable_scriptsig()->set_pub( strpubCstr, strpubCstrLen );
    }

    auto extra = nlohmann::json::parse(tx.extra());
    int needVerifyPreHashCount = extra["NeedVerifyPreHashCount"].get<int>();

    DBReader db_reader;
    std::string blockHash;
    if (DBStatus::DB_SUCCESS != db_reader.GetBestChainHash(blockHash) )
    {
        ReturnAckCode<TxMsgAck>(msgdata, errInfo, txMsgAck, -3);
        return -3;
    }

	std::string serTx = tx.SerializeAsString();
	uint64_t top = 0;
	db_reader.GetBlockTop(top);
    
    TxMsg phoneToTxMsg;
    phoneToTxMsg.set_version(getVersion());
	phoneToTxMsg.set_tx(serTx);
	phoneToTxMsg.set_txencodehash(msg->txencodehash());
    phoneToTxMsg.set_prevblkhash(blockHash);
    phoneToTxMsg.set_trycountdown(CalcTxTryCountDown(needVerifyPreHashCount));
	phoneToTxMsg.set_top(top);

	auto txmsg = make_shared<TxMsg>(phoneToTxMsg);
	std::string txHash;
	int ret = DoHandleTx(txmsg, txHash);
	txMsgAck.set_txhash(txHash);
	if(ret != 0)
	{
		ret -= 1000;
	}
	DEBUGLOG("DoHandleTx ret:{}", ret);
	ReturnAckCode<TxMsgAck>(msgdata, errInfo, txMsgAck, ret);
	return 0;
}

std::map<int32_t, std::string> GetCreatePledgeTxMsgReqCode()
{
	std::map<int32_t, std::string> errInfo = {make_pair(0, "成功"), 
												make_pair(-1, "版本不兼容"), 
												};

	return errInfo;												
}

int HandleCreatePledgeTxMsgReq(const std::shared_ptr<CreatePledgeTxMsgReq>& msg, const MsgData &msgdata)
{
    CreatePledgeTxMsgAck createPledgeTxMsgAck; 
	auto errInfo = GetCreatePledgeTxMsgReqCode();

	if( 0 != Util::IsVersionCompatible( getVersion() ) )
	{
        ReturnAckCode<CreatePledgeTxMsgAck>(msgdata, errInfo, createPledgeTxMsgAck, -1);
		return -1;
	}

	std::string fromAddr = msg->addr();
    uint64_t amount = std::stod(msg->amt().c_str()) * DECIMAL_NUM;
    uint32_t needverifyprehashcount  = std::stoi(msg->needverifyprehashcount()) ;
	uint64_t gasFee = std::stod(msg->gasfees().c_str()) * DECIMAL_NUM;
	TxHelper::PledgeType pledgeType = (TxHelper::PledgeType)msg->type();

    CTransaction outTx;
	std::vector<TxHelper::Utxo> outVin;
    int ret = TxHelper::CreatePledgeTransaction(fromAddr, amount, needverifyprehashcount, gasFee, pledgeType, outTx, outVin);
	if(ret != 0)
	{
		ret -= 1000;
        ReturnAckCode<CreatePledgeTxMsgAck>(msgdata, errInfo, createPledgeTxMsgAck, ret);
		return 0;
	}

    std::string txData = outTx.SerializePartialAsString();

    size_t encodeLen = txData.size() * 2 + 1;
	unsigned char encode[encodeLen] = {0};
	memset(encode, 0, encodeLen);
	long codeLen = base64_encode((unsigned char *)txData.data(), txData.size(), encode);

    std::string encodeStr((char *)encode, codeLen);
	std::string txEncodeHash = getsha256hash(encodeStr);
    createPledgeTxMsgAck.set_txdata(encodeStr);
    createPledgeTxMsgAck.set_txencodehash(txEncodeHash);

	ReturnAckCode<CreatePledgeTxMsgAck>(msgdata, errInfo, createPledgeTxMsgAck, 0);
    
    return 0;
}


std::map<int32_t, std::string> GetPledgeTxMsgReqCode()
{
	std::map<int32_t, std::string> errInfo = {
												make_pair(0, "成功"),
												make_pair(-1, "版本不兼容"),
												make_pair(-2, "参数错误"),
												make_pair(-3, "交易转换失败"),
												make_pair(-4, "获得主链错误"),
												make_pair(-5, "获得最高高度错误"),
											};

	return errInfo;												
}
int HandlePledgeTxMsgReq(const std::shared_ptr<PledgeTxMsgReq>& msg, const MsgData &msgdata)
{
    TxMsgAck txMsgAck;

	auto errInfo = GetPledgeTxMsgReqCode();
   
	//判断版本是否兼容
	if( 0 != Util::IsVersionCompatible(msg->version() ) )
	{
		ReturnAckCode<TxMsgAck>(msgdata, errInfo, txMsgAck, -1);
		return -1;
	}

    if (msg->sertx().data() == nullptr || msg->sertx().size() == 0 || 
        msg->strsignature().data() == nullptr || msg->strsignature().size() == 0 || 
        msg->strpub().data() == nullptr || msg->strpub().size() == 0)
    {
        ReturnAckCode<TxMsgAck>(msgdata, errInfo, txMsgAck, -2);
        return -2;
    }

	// 将交易信息体，公钥，签名信息反base64
	unsigned char serTxCstr[msg->sertx().size()] = {0};
	unsigned long serTxCstrLen = base64_decode((unsigned char *)msg->sertx().data(), msg->sertx().size(), serTxCstr);
	std::string serTxStr((char *)serTxCstr, serTxCstrLen);

	CTransaction tx;
	if (!tx.ParseFromString(serTxStr))
	{
		ReturnAckCode<TxMsgAck>(msgdata, errInfo, txMsgAck, -3);
		return -3;
	}

	unsigned char strsignatureCstr[msg->strsignature().size()] = {0};
	unsigned long strsignatureCstrLen = base64_decode((unsigned char *)msg->strsignature().data(), msg->strsignature().size(), strsignatureCstr);

	unsigned char strpubCstr[msg->strsignature().size()] = {0};
	unsigned long strpubCstrLen = base64_decode((unsigned char *)msg->strpub().data(), msg->strpub().size(), strpubCstr);

	for (int i = 0; i < tx.vin_size(); i++)
	{
		CTxin * txin = tx.mutable_vin(i);
		txin->mutable_scriptsig()->set_sign( std::string( (char *)strsignatureCstr, strsignatureCstrLen ) );
		txin->mutable_scriptsig()->set_pub( std::string( (char *)strpubCstr, strpubCstrLen ) );  
	}

    std::string serTx = tx.SerializeAsString();
	
    auto extra = nlohmann::json::parse(tx.extra());
    int needVerifyPreHashCount = extra["NeedVerifyPreHashCount"].get<int>();

	TxMsg phoneToTxMsg;
    phoneToTxMsg.set_version(getVersion());
	phoneToTxMsg.set_tx(serTx);
	phoneToTxMsg.set_txencodehash(msg->txencodehash());

    DBReader db_reader;
    std::string blockHash;
    if (DBStatus::DB_SUCCESS != db_reader.GetBestChainHash(blockHash) )
    {
        ReturnAckCode<TxMsgAck>(msgdata, errInfo, txMsgAck, -4);
        return -4;
    }
    phoneToTxMsg.set_prevblkhash(blockHash);
    phoneToTxMsg.set_trycountdown(CalcTxTryCountDown(needVerifyPreHashCount));

	uint64_t top = 0;
	int db_status = db_reader.GetBlockTop(top);
    if (DBStatus::DB_SUCCESS != db_status)
    {
        ReturnAckCode<TxMsgAck>(msgdata, errInfo, txMsgAck, -5);
        return -5;
    }	
	phoneToTxMsg.set_top(top);

	auto txmsg = make_shared<TxMsg>(phoneToTxMsg);
	std::string txHash;
	int ret = DoHandleTx(txmsg, txHash);
	txMsgAck.set_txhash(txHash);
	if (ret != 0)
	{
		ret -= 1000;
	}
	DEBUGLOG("DoHandleTx ret:{}", ret);
	ReturnAckCode<TxMsgAck>(msgdata, errInfo, txMsgAck, ret);
    return 0;
}


std::map<int32_t, std::string> GetCreateRedeemTxMsgReqCode()
{
	std::map<int32_t, std::string> errInfo = {make_pair(0, "成功"), 
												make_pair(-1, "版本不兼容"), 
												};

	return errInfo;												
}

int HandleCreateRedeemTxMsgReq(const std::shared_ptr<CreateRedeemTxMsgReq>& msg,const MsgData &msgdata)
{
    CreateRedeemTxMsgAck createRedeemTxMsgAck;
	auto errInfo = GetCreateRedeemTxMsgReqCode();

    // 判断版本是否兼容
	if( 0 != Util::IsVersionCompatible( getVersion() ) )
	{
        ReturnAckCode<CreateRedeemTxMsgAck>(msgdata, errInfo, createRedeemTxMsgAck, -1);
		return -1;
	}
    
    std::string fromAddr = msg->addr();
   	std::string utxoStr = msg->txhash();
	uint32_t needverifyprehashcount = std::stoi(msg->needverifyprehashcount());
    uint64_t gasFee = std::stod(msg->gasfees().c_str()) * DECIMAL_NUM;
	
	CTransaction outTx;
	std::vector<TxHelper::Utxo> outVin;
	int ret = TxHelper::CreateRedeemTransaction(fromAddr,utxoStr, needverifyprehashcount, gasFee, outTx, outVin);
	if(ret != 0)
	{
		ret -= 1000;
        ReturnAckCode<CreateRedeemTxMsgAck>(msgdata, errInfo, createRedeemTxMsgAck, ret);
		return ret;
	}
    
	outTx.clear_signprehash();
	outTx.clear_hash();

	std::string txData = outTx.SerializePartialAsString();

    size_t encodeLen = txData.size() * 2 + 1;
	unsigned char encode[encodeLen] = {0};
	memset(encode, 0, encodeLen);
	long codeLen = base64_encode((unsigned char *)txData.data(), txData.size(), encode);

    std::string encodeStr((char *)encode, codeLen);
	std::string txEncodeHash = getsha256hash(encodeStr);
    createRedeemTxMsgAck.set_txdata(encodeStr);
    createRedeemTxMsgAck.set_txencodehash(txEncodeHash);
    
	ReturnAckCode<CreateRedeemTxMsgAck>(msgdata, errInfo, createRedeemTxMsgAck, 0);
    return 0;
}


std::map<int32_t, std::string> GetRedeemTxMsgReqCode()
{
	std::map<int32_t, std::string> errInfo = {make_pair(0, "成功"), 
												make_pair(-1, "版本不兼容"), 
												make_pair(-2, "交易转换失败"), 
												make_pair(-3, "获得最高高度错误"),
												make_pair(-4, "获得主链错误"),
												};

	return errInfo;												
}
int HandleRedeemTxMsgReq(const std::shared_ptr<RedeemTxMsgReq>& msg, const MsgData &msgdata )
{
   TxMsgAck txMsgAck; 
	auto errInfo = GetRedeemTxMsgReqCode();

    // 判断版本是否兼容
	if( 0 != Util::IsVersionCompatible(getVersion() ) )
	{
		ReturnAckCode<TxMsgAck>(msgdata, errInfo, txMsgAck, -1);
		return -1;
	}
	// 将交易信息体，公钥，签名信息反base64
	unsigned char serTxCstr[msg->sertx().size()] = {0};
	unsigned long serTxCstrLen = base64_decode((unsigned char *)msg->sertx().data(), msg->sertx().size(), serTxCstr);
	std::string serTxStr((char *)serTxCstr, serTxCstrLen);

	CTransaction tx;
	if (!tx.ParseFromString(serTxStr))
	{
		ReturnAckCode<TxMsgAck>(msgdata, errInfo, txMsgAck, -2);
		return -2;
	}

	unsigned char strsignatureCstr[msg->strsignature().size()] = {0};
	unsigned long strsignatureCstrLen = base64_decode((unsigned char *)msg->strsignature().data(), msg->strsignature().size(), strsignatureCstr);

	unsigned char strpubCstr[msg->strsignature().size()] = {0};
	unsigned long strpubCstrLen = base64_decode((unsigned char *)msg->strpub().data(), msg->strpub().size(), strpubCstr);

	for (int i = 0; i < tx.vin_size(); i++)
	{
		CTxin * txin = tx.mutable_vin(i);
		txin->mutable_scriptsig()->set_sign( std::string( (char *)strsignatureCstr, strsignatureCstrLen ) );
		txin->mutable_scriptsig()->set_pub( std::string( (char *)strpubCstr, strpubCstrLen ) );
	}

    std::string serTx = tx.SerializeAsString();
    auto extra = nlohmann::json::parse(tx.extra());
    int needVerifyPreHashCount = extra["NeedVerifyPreHashCount"].get<int>();

	TxMsg phoneToTxMsg;
    phoneToTxMsg.set_version(getVersion());
	phoneToTxMsg.set_tx(serTx);
	phoneToTxMsg.set_txencodehash(msg->txencodehash());

    DBReader db_reader;
	uint64_t top = 0;
    {
        auto db_status = db_reader.GetBlockTop(top);
        if (DBStatus::DB_SUCCESS != db_status)
        {
            ReturnAckCode<TxMsgAck>(msgdata, errInfo, txMsgAck, -3);
            return -3;
        }
    }
	phoneToTxMsg.set_top(top);

    std::string blockHash;
    if (DBStatus::DB_SUCCESS != db_reader.GetBestChainHash(blockHash) )
    {
        ReturnAckCode<TxMsgAck>(msgdata, errInfo, txMsgAck, -4);
        return -4;
    }
    phoneToTxMsg.set_prevblkhash(blockHash);
    phoneToTxMsg.set_trycountdown(CalcTxTryCountDown(needVerifyPreHashCount));

	auto txmsg = make_shared<TxMsg>(phoneToTxMsg);
	std::string txHash;
	int ret = DoHandleTx(txmsg, txHash);
	txMsgAck.set_txhash(txHash);
	if (ret != 0)
	{
		ret -= 1000;
	}
	DEBUGLOG("DoHandleTx ret:{}", ret);
	ReturnAckCode<TxMsgAck>(msgdata, errInfo, txMsgAck, ret);
    
    return 0;

}

std::map<int32_t, std::string> GetCreateDevicePledgeTxMsgReqCode()
{
	std::map<int32_t, std::string> errInfo = {make_pair(0, "成功"),
												make_pair(-1, "版本不兼容"),
												make_pair(-2, "密码错误倒计时未结束"),
												make_pair(-3, "密码错误"),
												make_pair(-4, "密码第三次输入错误"),
												make_pair(-5, "获得最高高度错误"),
												make_pair(-6, "获得主链错误"),
												};

	return errInfo;
}

int HandleCreateDevicePledgeTxMsgReq(const std::shared_ptr<CreateDevicePledgeTxMsgReq>& msg, const MsgData &msgdata )
{
    TxMsgAck txMsgAck;
	auto errInfo = GetCreateDevicePledgeTxMsgReqCode();

	// 判断版本是否兼容
	if( 0 != Util::IsVersionCompatible(getVersion() ) )
	{
		ReturnAckCode<TxMsgAck>(msgdata, errInfo, txMsgAck, -1);
		return -1;
	}

  	auto pCPwdAttackChecker = MagicSingleton<CPwdAttackChecker>::GetInstance();
    uint32_t minutescount = 0;
    bool retval = pCPwdAttackChecker->IsOk(minutescount);
    if(retval == false)
    {
        ReturnAckCode<TxMsgAck>(msgdata, errInfo, txMsgAck, -2, std::to_string(minutescount));
        return -2;
    }

	std::string hashOriPass = generateDeviceHashPassword(msg->password());
    std::string targetPassword = Singleton<DevicePwd>::get_instance()->GetDevPassword();
    if(hashOriPass.compare(targetPassword))
    {
        DEBUGLOG("Begin to count, beacause password was entered incorrectly.");
       if(pCPwdAttackChecker->Wrong())
       {
            ReturnAckCode<TxMsgAck>(msgdata, errInfo, txMsgAck, -3);
            return -3;
       }
       else
       {
            ReturnAckCode<TxMsgAck>(msgdata, errInfo, txMsgAck, -4);
            return -4;
       }
    }
    else
    {
        DEBUGLOG("Input of password reset to 0.");
        pCPwdAttackChecker->Right();
    }

	std::string fromAddr = msg->addr();
	uint64_t amount = std::stod(msg->amt()) * DECIMAL_NUM;
	uint32_t needVerifyPreHashCount = std::stoi(msg->needverifyprehashcount());
	uint64_t GasFee = std::stod(msg->gasfees()) * DECIMAL_NUM;
	TxHelper::PledgeType pledgeType = (TxHelper::PledgeType)msg->type();

	CTransaction outTx;
	std::vector<TxHelper::Utxo> outVin;
	int ret = TxHelper::CreatePledgeTransaction(fromAddr, amount, needVerifyPreHashCount, GasFee, pledgeType, outTx, outVin);
	if(ret != 0)
	{
		ret -= 1000;
		ReturnAckCode<TxMsgAck>(msgdata, errInfo, txMsgAck, ret);
		return ret;
	}	

	std::string serTx = outTx.SerializeAsString();

	size_t encodeLen = serTx.size() * 2 + 1;
	unsigned char encode[encodeLen] = {0};
	memset(encode, 0, encodeLen);
	long codeLen = base64_encode((unsigned char *)serTx.data(), serTx.size(), encode);
	std::string encodeStr( (char *)encode, codeLen );

	std::string encodeStrHash = getsha256hash(encodeStr);

	std::string signature;
	std::string strPub;
	GetSignString(encodeStrHash, signature, strPub);

	for (int i = 0; i < outTx.vin_size(); i++)
	{
		CTxin * txin = outTx.mutable_vin(i);
		txin->mutable_scriptsig()->set_sign(signature);
		txin->mutable_scriptsig()->set_pub(strPub);
	}

	serTx = outTx.SerializeAsString();

	TxMsg txMsg;
    txMsg.set_version( getVersion() );
	txMsg.set_tx(serTx);
	txMsg.set_txencodehash( encodeStrHash );

    DBReader db_reader;
	uint64_t top = 0;
	int db_status = db_reader.GetBlockTop(top);
    if (DBStatus::DB_SUCCESS != db_status)
    {
		ReturnAckCode<TxMsgAck>(msgdata, errInfo, txMsgAck, -6);
        return -5;
    }
	txMsg.set_top(top);

    std::string blockHash;
    if (DBStatus::DB_SUCCESS != db_reader.GetBestChainHash(blockHash) )
    {
		ReturnAckCode<TxMsgAck>(msgdata, errInfo, txMsgAck, -7);
        return -6;
    }
    txMsg.set_prevblkhash(blockHash);
    txMsg.set_trycountdown(CalcTxTryCountDown(needVerifyPreHashCount));

	auto tx_msg = make_shared<TxMsg>(txMsg);
	std::string txHash;
    ret = DoHandleTx(tx_msg, txHash);
    if (ret != 0)
	{
		ret -= 2000;
	}

	DEBUGLOG("DoHandleTx ret:{}", ret);
	txMsgAck.set_txhash(txHash);
	ReturnAckCode<TxMsgAck>(msgdata, errInfo, txMsgAck, ret);
    return 0;
}


std::map<int32_t, std::string> GetCreateDeviceRedeemTxMsgReqCode()
{
	std::map<int32_t, std::string> errInfo = {make_pair(0, "成功"),
												make_pair(-1, "版本不兼容"),
												make_pair(-2, "密码错误倒计时未结束"),
												make_pair(-3, "密码错误"),
												make_pair(-4, "密码第三次输入错误"),
												make_pair(-5, "获得最高高度错误"),
												make_pair(-6, "获得主链错误"),
												};

	return errInfo;
}

// markhere:modify
// 手机连接矿机发起解质押交易
int HandleCreateDeviceRedeemTxMsgReq(const std::shared_ptr<CreateDeviceRedeemTxMsgReq> &msg, const MsgData &msgdata )
{
	TxMsgAck txMsgAck;
	auto errInfo = GetCreateDeviceRedeemTxMsgReqCode();

	// 判断版本是否兼容
	if( 0 != Util::IsVersionCompatible(getVersion() ) )
	{
		ReturnAckCode<TxMsgAck>(msgdata, errInfo, txMsgAck, -1);
		return -1;
	}

  	auto pCPwdAttackChecker = MagicSingleton<CPwdAttackChecker>::GetInstance();
    uint32_t minutescount = 0;
    bool retval = pCPwdAttackChecker->IsOk(minutescount);
    if(retval == false)
    {
        ReturnAckCode<TxMsgAck>(msgdata, errInfo, txMsgAck, -2, std::to_string(minutescount));
        return -2;
    }

	std::string hashOriPass = generateDeviceHashPassword(msg->password());
    std::string targetPassword = Singleton<DevicePwd>::get_instance()->GetDevPassword();
    if(hashOriPass.compare(targetPassword))
    {
        DEBUGLOG("Begin to count, beacause password was entered incorrectly.");
       if(pCPwdAttackChecker->Wrong())
       {
            ReturnAckCode<TxMsgAck>(msgdata, errInfo, txMsgAck, -3);
            return -3;
       }
       else
       {
            ReturnAckCode<TxMsgAck>(msgdata, errInfo, txMsgAck, -4);
            return -4;
       }
    }
    else
    {
        DEBUGLOG("Input of password reset to 0.");
        pCPwdAttackChecker->Right();
    }

	std::string fromAddr = msg->addr();
	std::string utxo_hash = msg->utxo();
	uint32_t needVerifyPreHashCount = std::stoi(msg->needverifyprehashcount());
	uint64_t GasFee = std::stod(msg->gasfees()) * DECIMAL_NUM;

	CTransaction outTx;
	std::vector<TxHelper::Utxo> outVin;
    int ret = TxHelper::CreateRedeemTransaction(fromAddr,utxo_hash, needVerifyPreHashCount, GasFee, outTx, outVin);
	if(ret != 0)
	{
		ret -= 1000;
		ReturnAckCode<TxMsgAck>(msgdata, errInfo, txMsgAck, ret);
        return ret;
	}

	std::string serTx = outTx.SerializeAsString();

	size_t encodeLen = serTx.size() * 2 + 1;
	unsigned char encode[encodeLen] = {0};
	memset(encode, 0, encodeLen);
	long codeLen = base64_encode((unsigned char *)serTx.data(), serTx.size(), encode);
	std::string encodeStr( (char *)encode, codeLen );

	std::string encodeStrHash = getsha256hash(encodeStr);

	std::string signature;
	std::string strPub;
	GetSignString(encodeStrHash, signature, strPub);

	for (int i = 0; i < outTx.vin_size(); i++)
	{
		CTxin * txin = outTx.mutable_vin(i);
		txin->mutable_scriptsig()->set_sign(signature);
		txin->mutable_scriptsig()->set_pub(strPub);
	}

	serTx = outTx.SerializeAsString();

	TxMsg txMsg;
	txMsg.set_version( getVersion() );

	txMsg.set_tx(serTx);
	txMsg.set_txencodehash( encodeStrHash );

	DBReader db_reader;
	uint64_t top = 0;
    if(DBStatus::DB_SUCCESS != db_reader.GetBlockTop(top))
    {
		ReturnAckCode<TxMsgAck>(msgdata, errInfo, txMsgAck, -5);
        return -5;
    }
	txMsg.set_top(top);

    std::string blockHash;
    if (DBStatus::DB_SUCCESS != db_reader.GetBestChainHash(blockHash) )
    {
		ReturnAckCode<TxMsgAck>(msgdata, errInfo, txMsgAck, -6);
        return -6;
    }
    txMsg.set_prevblkhash(blockHash);
    txMsg.set_trycountdown(CalcTxTryCountDown(needVerifyPreHashCount));

	auto tx_msg = make_shared<TxMsg>(txMsg);
	std::string txHash;
    ret = DoHandleTx(tx_msg, txHash);
	if (ret != 0)
	{
		ret -= 2000;
	}
	
	DEBUGLOG("DoHandleTx ret:{}", ret);
	txMsgAck.set_txhash(txHash);
	ReturnAckCode<TxMsgAck>(msgdata, errInfo, txMsgAck, ret);
    return 0;
}

template<typename Ack>
void ReturnAckCode(const MsgData& msgdata, std::map<int32_t, std::string> errInfo, Ack & ack, int32_t code, const std::string & extraInfo)
{
	ack.set_version(getVersion());
	ack.set_code(code);
	if (extraInfo.size())
	{
		ack.set_message(extraInfo);
	}
	else
	{
		ack.set_message(errInfo[code]);
	}

	net_send_message<Ack>(msgdata, ack, net_com::Priority::kPriority_High_1); // ReturnAckCode大部分处理交易，默认优先级为high1
}

template<typename TxReq>
int CheckAddrs( const std::shared_ptr<TxReq>& req)
{
	if (req->from_size() > 1 && req->to_size() > 1)
    {
		return -1;
    }
	if (req->from_size() == 0 || req->to_size() == 0)
    {
		return -2;
    }
	return 0;
}

// Check time of the redeem, redeem time must be more than 30 days, add 20201208   LiuMingLiang
int IsMoreThan30DaysForRedeem(const std::string& utxo)
{
	DBReader db_reader;

    std::string strTransaction;
	DBStatus status = db_reader.GetTransactionByHash(utxo, strTransaction);
    if (status != DBStatus::DB_SUCCESS)
    {
        return -1;
    }

    CTransaction utxoPledge;
    utxoPledge.ParseFromString(strTransaction);
    uint64_t nowTime = Singleton<TimeUtil>::get_instance()->getlocalTimestamp();
    uint64_t DAYS30 = (uint64_t)1000000 * 60 * 60* 24 * 30;
    if(2 == g_testflag)
    {
        DAYS30 = (uint64_t)1000000 * 60;
    }
    if ((nowTime - utxoPledge.time()) >= DAYS30)
    {
        return 0;
    }
    else
    {
        return -1;
    }
}

// Description: handle the ConfirmTransactionReq from network,   20210309   Liu
int HandleConfirmTransactionReq(const std::shared_ptr<ConfirmTransactionReq>& msg, const MsgData& msgdata)
{
	std::string version = msg->version();
	std::string id = msg->id();
	std::string txHash = msg->tx_hash();

	bool success = false;
	CBlock block;

	{
        DBReader db_reader;
        std::string txRaw;
		int db_status = db_reader.GetTransactionByHash(txHash, txRaw);
		if (db_status == 0)
		{
			success = true;
			string blockHash;
			db_reader.GetBlockHashByTransactionHash(txHash, blockHash);
			string blockRaw;
			db_reader.GetBlockByBlockHash(blockHash, blockRaw);
			block.ParseFromString(blockRaw);
			DEBUGLOG("In confirm transaction, Get block success.", blockHash);
		}
	}

	ConfirmTransactionAck ack;
	ack.set_version(getVersion());
	ack.set_id(net_get_self_node_id());
	ack.set_tx_hash(txHash);
	ack.set_flag(msg->flag());

	if (success)
	{
		ack.set_success(true);
		std::string blockRaw = block.SerializeAsString();
		ack.set_block_raw(blockRaw);
	}
	else
	{
		ack.set_success(false);
	}

	net_send_message<ConfirmTransactionAck>(id, ack);
    return 0;
}

int HandleConfirmTransactionAck(const std::shared_ptr<ConfirmTransactionAck>& msg, const MsgData& msgdata)
{
	std::string ver = msg->version();
	std::string id = msg->id();
	std::string tx_hash = msg->tx_hash();
	bool success = msg->success();
	DEBUGLOG("Receive confirm: id:{} tx_hash:{} success:{} ", id, tx_hash, success);
	if (success)
	{
		std::string blockRaw = msg->block_raw();
		CBlock block;
		block.ParseFromString(blockRaw);

		if (msg->flag() == ConfirmTxFlag)
		{
			MagicSingleton<TransactionConfirmTimer>::GetInstance()->update_count(tx_hash, block);
		}
		else if (msg->flag() == ConfirmRpcFlag)
		{
			if (g_RpcTransactionConfirm.is_not_exist_id(tx_hash, id))
			{
				g_RpcTransactionConfirm.update_count(tx_hash, block);
				g_RpcTransactionConfirm.update_id(tx_hash,id);
			}
		}
	}
	else
	{
		if (msg->flag() == ConfirmTxFlag)
		{
			MagicSingleton<TransactionConfirmTimer>::GetInstance()->update_failed_count(tx_hash);
		}
		else if (msg->flag() == ConfirmRpcFlag)
		{
			if (g_RpcTransactionConfirm.is_not_exist_id(tx_hash, id))
			{
				g_RpcTransactionConfirm.update_failed_count(tx_hash);
				g_RpcTransactionConfirm.update_id(tx_hash,id);
			}
		}
	}
    return 0;
}

int SendConfirmTransaction(const std::string& tx_hash, uint64_t prevBlkHeight, ConfirmCacheFlag flag, const uint32_t confirmCount)
{
	if (confirmCount == 0)
	{
		ERRORLOG("confirmCount is empty");
		return -1;
	}

	if (tx_hash.empty())
	{
		ERRORLOG("tx_hash is empty");
		return -2;
	}

	ConfirmTransactionReq req;
	req.set_version(getVersion());
	req.set_id(net_get_self_node_id());
	req.set_tx_hash(tx_hash);
	req.set_flag(flag);

	std::vector<Node> list;
	if (Singleton<PeerNode>::get_instance()->get_self_node().is_public_node)
	{
		list = Singleton<PeerNode>::get_instance()->get_nodelist();
	}
	else
	{
		list = Singleton<NodeCache>::get_instance()->get_nodelist();
	}

	{
		// 过滤满足高度的节点
		std::vector<Node> tmpList;
		if (prevBlkHeight != 0)
		{
			for (auto & item : list)
			{
				if (item.height >= prevBlkHeight)
				{
					tmpList.push_back(item);
				}
			}
			list = tmpList;
		}
	}

	std::random_device device;
	std::mt19937 engine(device());

	int send_size = std::min(list.size(), (size_t)confirmCount);

	int count = 0;
	while (count < send_size && !list.empty())
	{
		int index = engine() % list.size();

		net_send_message<ConfirmTransactionReq>(list[index].base58address, req);
		++count;
		DEBUGLOG("Send to confirm: {} {} {} ", index, list[index].base58address, count);

		list.erase(list.begin() + index);
	}
	return 0;
}

// Count of public node, 20211116  Liu
int GetPublicNodeCount()
{
	const Node selfNode = Singleton<PeerNode>::get_instance()->get_self_node();
	std::vector<Node> nodelist;
	if (selfNode.is_public_node)
	{
		nodelist = Singleton<PeerNode>::get_instance()->get_nodelist();
	}
	else
	{
		nodelist = Singleton<NodeCache>::get_instance()->get_nodelist();
	}

	std::vector<string> pledgeAddrs; // 待选的已质押地址
	{
        DBReader db_reader;
        db_reader.GetPledgeAddress(pledgeAddrs);
    }

	int publicCount = 0;
	for (const auto& node : nodelist)
	{
		if (!node.is_public_node)
		{
			continue ;
		}
		if (node.base58address == selfNode.base58address)
		{
			continue ;
		}
		uint64_t pledgeAmount = 0;
		if (SearchPledge(node.base58address, pledgeAmount) == 0 && pledgeAmount >= g_TxNeedPledgeAmt)
		{
			++publicCount;
		}
		else if (pledgeAddrs.size() < g_minPledgeNodeNum)
		{
			++publicCount;
		}
	}

	return publicCount;
}

// Has a public node, 20211116  Liu
bool HasPublicNode()
{
	return (GetPublicNodeCount() > 0);
}

// Notify node height to change, 20211129  Liu
void NotifyNodeHeightChange()
{
	net_send_node_height_changed();
}

int GetUtxos(const std::string & address, std::vector<TxHelper::Utxo>& utxos)
{
	if (address.empty())
	{
		return -1;
	}

	utxos.clear();

	DBReader db_reader;
    std::vector<std::string> utxoHashs;
    auto status = db_reader.GetUtxoHashsByAddress(address, utxoHashs);
    if(DBStatus::DB_SUCCESS != status)
    {
        return -2;
    }
    
	// Remove duplication
    std::sort(utxoHashs.begin(), utxoHashs.end());
    utxoHashs.erase(unique(utxoHashs.begin(), utxoHashs.end()), utxoHashs.end());

    for (const auto& hash : utxoHashs)
    {
        std::string strTxRaw;
        if (db_reader.GetTransactionByHash(hash, strTxRaw) != DBStatus::DB_SUCCESS)
        {
            continue ;
        }
        CTransaction utxoTx;
        utxoTx.ParseFromString(strTxRaw);

		TxHelper::Utxo utxo;
		utxo.hash = utxoTx.hash();
		utxo.scriptpubkey = address;
		utxo.value = 0;

        for (int i = 0; i < utxoTx.vout_size(); i++)
        {
            CTxout txout = utxoTx.vout(i);
            if (txout.scriptpubkey() == address)
            {
				utxo.value += txout.value();
				utxo.n = i;
            }
        }
		utxos.push_back(utxo);
    }
	return 0;
}

static const size_t MAX_NODE_SIZE = 100;
static size_t InsertNodeToAck(const vector<Node>& nodes, GetNodeCacheAck& ack, size_t existCount = 0)
{
	if (nodes.empty())
	{
		return 0;
	}
	int restCount = MAX_NODE_SIZE - existCount;
	if (restCount <= 0)
	{
		return 0;
	}
	size_t count = std::min((size_t)restCount, nodes.size());

	std::random_device rd;
	std::default_random_engine rng(rd());
	std::uniform_int_distribution<size_t> dist(0, nodes.size()-1);
	std::unordered_set<size_t> rand_index;
	while (rand_index.size() < count)
	{
		rand_index.insert(dist(rng));
	}

	for (auto& i : rand_index)
	{
		Node node = nodes[i];

		NodeCacheItem *nodeHeight = ack.add_nodes_height();
		nodeHeight->set_height(node.height);
		nodeHeight->set_base58addr(node.base58address);
		nodeHeight->set_fee(node.sign_fee);
		nodeHeight->set_is_public(node.is_public_node);
	}

	return count;
}

int HandleGetNodeCacheReq(const std::shared_ptr<GetNodeCacheReq> &heightReq, const MsgData &from)
{
	uint32 subnodeHeight = heightReq->node_height();
	std::string subnodeId = heightReq->id();

	// Add node: first add pledge node, if node is less than 100, add unpledge, 20211020  Liu	
	GetNodeCacheAck heightAck;

	// Get public node of pledge
	vector<Node> publicPledgeNodes;
	MagicSingleton<PledgeCache>::GetInstance()->GetPublicPledge(publicPledgeNodes);
	size_t publicPledgeCount = InsertNodeToAck(publicPledgeNodes, heightAck);
	if (publicPledgeNodes.empty())
	{
		vector<Node> publicUnpledgeNodes;
		MagicSingleton<PledgeCache>::GetInstance()->GetPublicUnpledge(publicUnpledgeNodes);
		size_t publicUnpledgeCount = InsertNodeToAck(publicUnpledgeNodes, heightAck);
	}

	vector<Node> pledgeNodes;
	MagicSingleton<PledgeCache>::GetInstance()->GetPledge(subnodeHeight, pledgeNodes);
	size_t pledgeCount = InsertNodeToAck(pledgeNodes, heightAck);
	if (pledgeCount < MAX_NODE_SIZE)
	{
		vector<Node> unpledgeNodes;
		MagicSingleton<PledgeCache>::GetInstance()->GetUnpledge(subnodeHeight, unpledgeNodes);
		size_t unpledgeCount = InsertNodeToAck(unpledgeNodes, heightAck, pledgeCount);
	}

	// Is fetch the public node.
	if (heightReq->is_fetch_public())
	{
		vector<Node> publicnodes = Singleton<PeerNode>::get_instance()->get_public_node();
		for (auto &node : publicnodes)
		{
			NodeInfo *nodeinfo = heightAck.add_public_nodes();
			nodeinfo->set_base58addr(node.base58address);
			nodeinfo->set_pub(node.pub);
			nodeinfo->set_listen_ip(node.listen_ip);
			nodeinfo->set_listen_port(node.listen_port);
			nodeinfo->set_public_ip(node.public_ip);
			nodeinfo->set_public_port(node.public_port);
			nodeinfo->set_is_public_node(node.is_public_node);
			nodeinfo->set_sign_fee(node.sign_fee);
			nodeinfo->set_package_fee(node.package_fee);
			nodeinfo->set_height(node.height);
			nodeinfo->set_public_base58addr(node.public_base58addr);
		}
	}

	net_com::send_message(heightReq->id(), heightAck);
	return 0;
}

int HandleGetNodeCacheAck(const std::shared_ptr<GetNodeCacheAck> &heightAck, const MsgData &from)
{
	vector<Node> nodelist;
	auto self_id = Singleton<PeerNode>::get_instance()->get_self_id();
	for (int i = 0; i < heightAck->nodes_height_size(); i++)
	{
		const NodeCacheItem &nodeHeight = heightAck->nodes_height(i);
		if (nodeHeight.base58addr() == self_id)
		{
			continue ;
		}

		Node node;
		bool find = Singleton<PeerNode>::get_instance()->find_node(nodeHeight.base58addr(), node);
		if (find)
		{
			node.height = nodeHeight.height();
			node.base58address = nodeHeight.base58addr();
			node.sign_fee = nodeHeight.fee();
			node.is_public_node = nodeHeight.is_public();
			nodelist.push_back(node);
		}
		else
		{
			node.height = nodeHeight.height();
			node.base58address = nodeHeight.base58addr();
			node.sign_fee = nodeHeight.fee();
			node.is_public_node = nodeHeight.is_public();
			nodelist.push_back(node);
		}
	}
	if (nodelist.empty())
	{
		INFOLOG("GetNodeCacheAck list is empty!");
		return 0;
	}

	DEBUGLOG("Get height ack: {}", nodelist.size());
	Singleton<NodeCache>::get_instance()->reset_node(nodelist);

	// Add public node
	if (heightAck->public_nodes_size() > 0)
	{
		std::vector<Node> newNodeList;
		for (int i = 0; i < heightAck->public_nodes_size(); i++)
		{
			const NodeInfo &nodeinfo = heightAck->public_nodes(i);
			Node node;
			node.base58address = nodeinfo.base58addr();
			node.pub = nodeinfo.pub();
			node.sign = nodeinfo.sign();
			node.listen_ip = nodeinfo.listen_ip();
			node.listen_port = nodeinfo.listen_port();
			node.public_ip = nodeinfo.public_ip();
			node.public_port = nodeinfo.public_port();
			node.is_public_node = nodeinfo.is_public_node();
			node.sign_fee = nodeinfo.sign_fee();
			node.package_fee = nodeinfo.package_fee();
			node.height = nodeinfo.height();
			node.public_base58addr = nodeinfo.public_base58addr();

			newNodeList.push_back(node);
		}

		const Node &selfNode = Singleton<PeerNode>::get_instance()->get_self_node();
		std::vector<Node> oldNodeList = Singleton<PeerNode>::get_instance()->get_public_node();

		// 把自身所连接公网节点放入同步过来的公网节点中
		for (auto &node : oldNodeList)
		{
			if (selfNode.public_base58addr == node.base58address)
			{
				newNodeList.push_back(node);
			}
		}

		// 除去自身连的公网节点外其他公网节点全部删除
		for (auto &node : oldNodeList)
		{
			if (node.base58address == selfNode.public_base58addr)
			{
				continue;
			}
			Singleton<PeerNode>::get_instance()->delete_node(node.base58address);
		}

		// 把同步过来的公网节点放入自己的K桶中
		for (auto &node : newNodeList)
		{

			Singleton<PeerNode>::get_instance()->add_public_node(node);
			Singleton<PeerNode>::get_instance()->add(node);
		}
	}
	return 0;
}
