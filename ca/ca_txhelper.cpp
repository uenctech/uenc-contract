#include "ca_txhelper.h"

#include <algorithm>
#include <iterator>

#include "include/logging.h"
#include "ca_transaction.h"
#include "ca_global.h"
#include "ca_hexcode.h"
#include "MagicSingleton.h"
#include "utils/string_util.h"
#include "utils/time_util.h"
#include "utils/json.hpp"
#include "ca_base64.h"
#include "db/db_api.h"
#include "utils/base64.h"
#include "db/db_api.h"
#include <google/protobuf/util/json_util.h>

#include "wasm/wasm_context.hpp"
using namespace google::protobuf;

std::vector<std::string> TxHelper::GetTxOwner(const std::string tx_hash)
{
	DBReader db_read;
	std::string strTxRaw;
	if (DBStatus::DB_SUCCESS != db_read.GetTransactionByHash(tx_hash, strTxRaw))
	{
		return std::vector<std::string>();
	}

	CTransaction Tx;
	Tx.ParseFromString(strTxRaw);

	return GetTxOwner(Tx);
}

std::string TxHelper::GetTxOwnerStr(const std::string tx_hash)
{
	std::vector<std::string> address = GetTxOwner(tx_hash);
	return StringUtil::concat(address, "_");
}

std::vector<std::string> TxHelper::GetTxOwner(const CTransaction& tx)
{
	std::vector<std::string> address;
	for (int i = 0; i < tx.vin_size(); i++)
	{
		CTxin txin = tx.vin(i);
		auto pub = txin.mutable_scriptsig()->pub();
		std::string addr = GetBase58Addr(pub);
		auto res = std::find(std::begin(address), std::end(address), addr);
		if (res == std::end(address))
		{
			address.push_back(addr);
		}
	}

	return address;
}

std::string TxHelper::GetTxOwnerStr(const CTransaction& tx)
{
	std::vector<std::string> address = GetTxOwner(tx);
	return StringUtil::concat(address, "_");
}

std::vector<std::string> TxHelper::GetUtxosByAddresses(std::vector<std::string> addresses)
{
	std::vector<std::string> vUtxoHashs;

	DBReader db_reader;

	for (auto& addr : addresses)
	{
		std::vector<std::string> tmp;
		DBStatus status = db_reader.GetUtxoHashsByAddress(addr, tmp);
		if (status != DBStatus::DB_SUCCESS)
		{
			DEBUGLOG("(GetUtxosByAddresses) GetUtxoHashsByAddress:{}", addr);
			return vUtxoHashs;
		}
		std::for_each(tmp.begin(), tmp.end(),
			[&](std::string& s) { s = s + "_" + addr;}
		);

		vUtxoHashs.insert(vUtxoHashs.end(), tmp.begin(), tmp.end());
	}
	return vUtxoHashs;
}

std::vector<std::string> TxHelper::GetUtxosByTx(const CTransaction& tx)
{
	std::vector<std::string> v1;
	for (int j = 0; j < tx.vin_size(); j++)
	{
		CTxin vin = tx.vin(j);
		std::string hash = vin.prevout().hash();

		if (hash.size() > 0)
		{
			v1.push_back(hash + "_" + GetBase58Addr(vin.scriptsig().pub()));
		}
	}
	return v1;
}

uint64_t TxHelper::GetUtxoAmount(std::string tx_hash, std::string address)
{
	CTransaction tx;
	{
		DBReader db_reader;
		std::string strTxRaw;
		if (DBStatus::DB_SUCCESS != db_reader.GetTransactionByHash(tx_hash, strTxRaw))
		{
			return 0;
		}
		tx.ParseFromString(strTxRaw);
	}

	uint64_t amount = 0;
	for (int j = 0; j < tx.vout_size(); j++)
	{
		CTxout txout = tx.vout(j);
		if (txout.scriptpubkey() == address)
		{
			amount += txout.value();
		}
	}
	return amount;
}

int TxHelper::Check(const std::vector<std::string>& fromAddr,
					const std::map<std::string, int64_t> toAddr,
					uint32_t needVerifyPreHashCount,
					uint64_t minerFees,
					TxHelper::TransactionType type)
{
	if (fromAddr.empty())
	{
		ERRORLOG("TxHelper Check: from is empty");
		return -1;
	}

	if(toAddr.empty())
	{
		ERRORLOG("TxHelper Check: to is empty");
		return -2;
	}

	for (auto& addr : fromAddr)
	{
		if (addr.empty())
		{
			ERRORLOG("TxHelper Check: from address is empty");
			return -3;
		}
	}

	for (auto& addr : toAddr)
	{
		if (addr.first.empty() || addr.second == 0)
		{
			ERRORLOG("TxHelper Check: to address is empty or value is zero");
			return -4;
		}
	}

	if(type == TxHelper::TransactionType::kTransactionType_Tx)
	{
		std::set<std::string> fromSet;
		for (auto& from : fromAddr)
		{
			if (!CheckBase58Addr(from))
			{
				ERRORLOG("TxHelper Check: to address is not base58 address");
				return -10;
			}

			fromSet.insert(from);
		}
		if (fromSet.size() != fromAddr.size())
		{
			ERRORLOG("TxHelper Check: repeated addr");
			return -11;
		}	

		std::set<std::string> toSet;
		for (auto& to : toAddr)
		{
			if (!CheckBase58Addr(to.first))
			{
				ERRORLOG("TxHelper Check: to address is not base58 address");
				return -12;
			}

			toSet.insert(to.first);
		}
		if (toSet.size() != toAddr.size())
		{
			ERRORLOG("TxHelper Check: repeated addr");
			return -13;
		}

		for (auto& to : toAddr)
		{
			for (auto& from : fromAddr)
			{
				if (from == to.first)
				{
					ERRORLOG("TxHelper Check: from address and to address is equal");
					return -14;
				}
			}
		}
	}
	else if(type == TxHelper::TransactionType::kTransactionType_Pledge)
	{
		if(fromAddr.size() != 1)
		{
			ERRORLOG("TxHelper Check: pledge from address is not only one address");
			return -20;
		}

		if(toAddr.size() != 1)
		{
			ERRORLOG("TxHelper Check: pledge to address is not only one address");
			return -21;
		}

		if(*fromAddr.cbegin() == toAddr.cbegin()->first)
		{
			ERRORLOG("TxHelper Check: pledge form address is equal to address");
			return -22;
		}

		if (!CheckBase58Addr(*fromAddr.cbegin()))
		{
			ERRORLOG("TxHelper Check: pledge from address is not base58 address");
			return -23;
		}

		if(toAddr.cbegin()->first != VIRTUAL_ACCOUNT_PLEDGE)
		{
			ERRORLOG("TxHelper Check: pledge to address is not virtual pledge address");
			return -24;
		}
	}
	else if(type == TxHelper::TransactionType::kTransactionType_Redeem)
	{
		if(fromAddr.size() != 1)
		{
			ERRORLOG("TxHelper Check: redeem from address is not only one address");
			return -30;
		}

		if(toAddr.size() != 1)
		{
			ERRORLOG("TxHelper Check: redeem to address is not only one address");
			return -31;
		}

		if(*fromAddr.cbegin() != toAddr.cbegin()->first)
		{
			ERRORLOG("TxHelper Check: redeem from address must equal to address");
			return -32;
		}

		if (!CheckBase58Addr(*fromAddr.cbegin()))
		{
			ERRORLOG("TxHelper Check: from address is not base58");
			return -33;
		}
	}
	else if(type == TxHelper::TransactionType::kTransactionType_Contract_Deploy)
	{

	}
	else if(type == TxHelper::TransactionType::kTransactionType_Contract_Execute)
	{

	}
	else
	{
		ERRORLOG("TxHelper Check: Wrong type");
		return -5;
	}

	if (MagicSingleton<TxVinCache>::GetInstance()->IsConflict(fromAddr))
	{
		ERRORLOG("TxHelper Check: fromAddr is in Pending Cache!");
		return -6;
	}

	if (needVerifyPreHashCount < (uint32_t)g_MinNeedVerifyPreHashCount || needVerifyPreHashCount > (uint32_t)g_MaxNeedVerifyPreHashCount)
	{
		ERRORLOG("TxHelper Check: needVerifyPreHashCount is invalid");
		return -7;
	}

	if ((uint64_t)minerFees < g_minSignFee || (uint64_t)minerFees > g_maxSignFee)
	{
		ERRORLOG("TxHelper Check: minerFees is invalid");
		return -8;
	}

	return 0;
}

int TxHelper::FindUtxo(const std::vector<std::string>& fromAddr,
						const uint64_t expend,
						const uint64_t need_utxo_amount,
						uint64_t& total,
						std::multiset<TxHelper::Utxo, TxHelper::UtxoCompare>& setOutUtxos)
{
	// 统计所有utxo
	std::multiset<TxHelper::Utxo, TxHelper::UtxoCompare> setNonZeroUtxos;
	std::multiset<TxHelper::Utxo, TxHelper::UtxoCompare> setZeroUtxos;

	DBReader db_reader;
	for (const auto& addr : fromAddr)
	{
		std::vector<std::string> vecUtxoHashs;
		if (DBStatus::DB_SUCCESS != db_reader.GetUtxoHashsByAddress(addr, vecUtxoHashs))
		{
			ERRORLOG("TxHelper FindUtxo: GetUtxoHashsByAddress failed");
			return -1;
		}
		std::sort(vecUtxoHashs.begin(), vecUtxoHashs.end());
		vecUtxoHashs.erase(std::unique(vecUtxoHashs.begin(), vecUtxoHashs.end()), vecUtxoHashs.end()); //去重

		for (const auto& hash : vecUtxoHashs)
		{
			std::string strTx;
			if (db_reader.GetTransactionByHash(hash, strTx) != 0)
			{
				ERRORLOG("TxHelper FindUtxo: GetTransactionByHash failed!");
				continue;
			}

			CTransaction tx;
			tx.ParseFromString(strTx);

			TxHelper::Utxo utxo;
			utxo.hash = tx.hash();
			utxo.scriptpubkey = addr;
			utxo.value = 0;

			for (int i = 0; i < tx.vout_size(); i++)
			{
				if (tx.vout(i).scriptpubkey() == addr)
				{
					utxo.value += tx.vout(i).value();
					utxo.n = i;
				}
			}
			if (utxo.value == 0)
			{
				setZeroUtxos.insert(utxo);
			}
			else
			{
				setNonZeroUtxos.insert(utxo);
			}
		}
	}

	total = 0;
	bool isOverHalf = ((double)setZeroUtxos.size() / (setNonZeroUtxos.size() + setZeroUtxos.size())) >= 0.5;
	if (isOverHalf)
	{
		auto it = setNonZeroUtxos.rbegin();
		while (it != setNonZeroUtxos.rend())
		{
			if (total >= expend || setOutUtxos.size() == need_utxo_amount)
			{
				break;
			}
			total += it->value;
			setOutUtxos.insert(*it);
			++it;
		}
	}
	else
	{
		while (!setNonZeroUtxos.empty())
		{
			if (total >= expend || setOutUtxos.size() == need_utxo_amount)
			{
				break;
			}

			total += setNonZeroUtxos.begin()->value;
			setOutUtxos.insert(*setNonZeroUtxos.begin());
			setNonZeroUtxos.erase(setNonZeroUtxos.begin());
		}

		if (total < expend && setOutUtxos.size() == need_utxo_amount)
		{
			while (!setNonZeroUtxos.empty())
			{
				if (total >= expend)
				{
					break;
				}

				if (setNonZeroUtxos.rbegin()->value < setOutUtxos.begin()->value)
				{
					break;
				}

				total = total - setOutUtxos.begin()->value + setNonZeroUtxos.rbegin()->value;
				setOutUtxos.erase(setOutUtxos.begin()); 		//去小
				setOutUtxos.insert(*setNonZeroUtxos.rbegin());  //取大
				setNonZeroUtxos.erase((++setNonZeroUtxos.rbegin()).base());
			}
		}
	}

	if (total < expend)
	{
		if(setOutUtxos.size() < need_utxo_amount)
		{
			ERRORLOG("TxHelper FindUtxo: total < expend and utxos less than Max utxo Size");
			return -2;
		}
		else
		{
			ERRORLOG("TxHelper FindUtxo: total < expend and utxos more than Max utxo Size");
			return -3;
		}
	}

	if (setOutUtxos.size() < need_utxo_amount)
	{
		// 剩余位置用0填充
		auto it = setZeroUtxos.begin();
		while (it != setZeroUtxos.end())
		{
			if (setOutUtxos.size() == need_utxo_amount)
			{
				break;
			}

			setOutUtxos.insert(*it);
			++it;
		}
	}

	return 0;
}

int TxHelper::CreateTxTransaction(const std::vector<std::string>& fromAddr,
									const std::map<std::string, int64_t> & toAddr,
									uint32_t needVerifyPreHashCount,
									uint64_t minerFees,
									CTransaction& outTx,
									std::vector<TxHelper::Utxo> & outVin)
{
	// 检查
	int ret = TxHelper::Check(fromAddr, toAddr, needVerifyPreHashCount, minerFees, TxHelper::TransactionType::kTransactionType_Tx);
	if (ret != 0)
	{
		ERRORLOG("TxHelper CreateTxTransaction: Check failed");
		ret -= 100;
		return ret;
	}

	// 计算总支出
	uint64_t minerfee = (needVerifyPreHashCount - 1) * minerFees; //签名费

	DBReader db_reader;   
	uint64_t publicNodePackageFee = 0;//打包费
	if (IsNeedPackage(fromAddr))
	{
		if (DBStatus::DB_SUCCESS != db_reader.GetDevicePackageFee(publicNodePackageFee))
		{
			ERRORLOG("TxHelper CreateTxTransaction: GetDevicePackageFee failed");
			return -1;
		}
	}

	uint64_t txfee = 0;//交易费
	for (auto& i : toAddr)
	{
		txfee += i.second;    
	}
	uint64_t expend = minerfee + publicNodePackageFee + txfee;

	// 寻找utxo
	uint64_t total = 0;
	std::multiset<TxHelper::Utxo, TxHelper::UtxoCompare> setOutUtxos;
	ret = FindUtxo(fromAddr, expend, g_MaxVinSize, total, setOutUtxos);
	if (ret != 0)
	{
		ERRORLOG("TxHelper CreateTxTransaction: FindUtxo failed");
		ret -= 200;
		return ret;
	}

	if (setOutUtxos.empty())
	{
		ERRORLOG("TxHelper CreateTxTransaction: utxo is zero");
		return -2;
	}

	// 填充交易体
	uint64_t remain = total - expend;

	// 填充vin
	std::set<string> setTxowners;
	uint32_t n = 0;
	for (auto & utxo : setOutUtxos)
	{
		setTxowners.insert(utxo.scriptpubkey);
		CTxin* txin = outTx.add_vin();
		txin->set_sequence(n++);
		CTxprevout* prevout = txin->mutable_prevout();
		prevout->set_hash(utxo.hash);
		prevout->set_n(utxo.n);
		outVin.push_back(utxo);
	}

	if (setTxowners.empty())
	{
		ERRORLOG("TxHelper CreateTxTransaction: tx owner is zero");
		return -3;
	}

	// 填充vout
	for (auto& to : toAddr)
	{
		CTxout* txoutToAddr = outTx.add_vout();
		txoutToAddr->set_scriptpubkey(to.first);
		txoutToAddr->set_value(to.second);
	}

	CTxout* txoutFromAddr = outTx.add_vout();
	txoutFromAddr->set_value(remain);
	txoutFromAddr->set_scriptpubkey(*setTxowners.rbegin());

	outTx.set_time(Singleton<TimeUtil>::get_instance()->getlocalTimestamp());

	std::string strTxOwners;
	for (auto& addr : setTxowners)
	{
		strTxOwners += addr;
		strTxOwners += "_";
	}
	strTxOwners.erase(strTxOwners.end() - 1);
	outTx.set_owner(strTxOwners);
	outTx.set_version(1);
	outTx.set_identity(net_get_self_node_id());

	nlohmann::json extra;
	extra["NeedVerifyPreHashCount"] = needVerifyPreHashCount;
	extra["SignFee"] = minerFees;
	extra["PackageFee"] = publicNodePackageFee;
	extra["TransactionType"] = TXTYPE_TX;
	outTx.set_extra(extra.dump());

	return 0;
}

int TxHelper::CreatePledgeTransaction(const std::string & fromAddr,
										uint64_t pledge_amount,
										uint32_t needVerifyPreHashCount,
										uint64_t minerFees,
										TxHelper::PledgeType pledgeType,
										CTransaction & outTx,
										std::vector<TxHelper::Utxo> & outVin)
{
	// 检查参数
	std::vector<std::string> vecfromAddr;
	vecfromAddr.push_back(fromAddr);
	std::map<std::string, int64_t> toAddr;
	toAddr.insert(std::make_pair(VIRTUAL_ACCOUNT_PLEDGE, pledge_amount));

	int ret = Check(vecfromAddr, toAddr, needVerifyPreHashCount, minerFees, TxHelper::TransactionType::kTransactionType_Pledge);
	if(ret != 0)
	{
		ERRORLOG("TxHelper CreatePledgeTransaction: Check failed");
		ret -= 100;
		return ret;
	}

	std::string strPledgeType;
	if (pledgeType == TxHelper::PledgeType::kPledgeType_Node)
	{
		strPledgeType = PLEDGE_NET_LICENCE;
	}
	else if (pledgeType == TxHelper::PledgeType::kPledgeType_PublicNode)
	{
		strPledgeType = PLEDGE_PUBLIC_NET_LICENCE;
	}
	else
	{
		return -1;
	}

	// 计算总支出
	uint64_t minerfee = (needVerifyPreHashCount - 1) * minerFees; //签名费

	DBReader db_reader;   
	uint64_t publicNodePackageFee = 0;//打包费
	if (IsNeedPackage(vecfromAddr))
	{
		if (DBStatus::DB_SUCCESS != db_reader.GetDevicePackageFee(publicNodePackageFee))
		{
			ERRORLOG("TxHelper CreatePledgeTransaction: GetDevicePackageFee failed");
			return -2;
		}
	}

	uint64_t txfee = pledge_amount;//交易费
	uint64_t expend = minerfee + publicNodePackageFee + txfee;

	// 寻找utxo
	uint64_t total;
	std::multiset<TxHelper::Utxo, TxHelper::UtxoCompare> setOutUtxos;
	ret = FindUtxo(vecfromAddr, expend, g_MaxVinSize, total, setOutUtxos);
	if (ret != 0)
	{
		ERRORLOG("TxHelper CreatePledgeTransaction: FindUtxo failed");
		ret -= 200;
		return ret;
	}

	if (setOutUtxos.empty())
	{
		ERRORLOG("TxHelper CreatePledgeTransaction: utxo is zero");
		return -3;
	}

	// 填充交易体
	uint64_t remain = total - expend;

	// 填充vin
	std::set<string> setTxowners;
	uint32_t n = 0;
	for (auto & utxo : setOutUtxos)
	{
		setTxowners.insert(utxo.scriptpubkey);
		CTxin* txin = outTx.add_vin();
		txin->set_sequence(n++);
		CTxprevout* prevout = txin->mutable_prevout();
		prevout->set_hash(utxo.hash);
		prevout->set_n(utxo.n);
		outVin.push_back(utxo);
	}

	if (setTxowners.empty())
	{
		ERRORLOG("TxHelper CreatePledgeTransaction: tx owner is zero");
		return -4;
	}

	// 填充vout
	CTxout* txoutToAddr = outTx.add_vout();
	txoutToAddr->set_scriptpubkey(toAddr.cbegin()->first); // 给虚拟账号
	txoutToAddr->set_value(toAddr.cbegin()->second);

	txoutToAddr = outTx.add_vout();
	txoutToAddr->set_scriptpubkey(*setTxowners.rbegin());           // 剩余给自己账号
	txoutToAddr->set_value(remain);

	outTx.set_time(Singleton<TimeUtil>::get_instance()->getlocalTimestamp());

	std::string strTxOwners;
	for (auto& addr : setTxowners)
	{
		strTxOwners += addr;
		strTxOwners += "_";
	}
	strTxOwners.erase(strTxOwners.end() - 1);
	outTx.set_owner(strTxOwners);
	outTx.set_version(1);
	outTx.set_identity(net_get_self_node_id());

	nlohmann::json txInfo;
	txInfo["PledgeType"] = strPledgeType;
	txInfo["PledgeAmount"] = pledge_amount;

	nlohmann::json extra;
	extra["TransactionInfo"] = txInfo;
	extra["NeedVerifyPreHashCount"] = needVerifyPreHashCount;
	extra["SignFee"] = minerFees;
	extra["PackageFee"] = publicNodePackageFee;
	extra["TransactionType"] = TXTYPE_PLEDGE;
	outTx.set_extra(extra.dump());

	return 0;
}

int TxHelper::CreateRedeemTransaction(const std::string fromAddr,
										const std::string utxo_hash,
										uint32_t needVerifyPreHashCount,
										uint64_t minerFees,
										CTransaction& outTx,
										std::vector<TxHelper::Utxo> & outVin)
{
	// 检查
	std::vector<std::string> vecfromAddr;
	vecfromAddr.push_back(fromAddr);

	DBReader db_reader; 
	std::string strPledgeTx;
	if (DBStatus::DB_SUCCESS != db_reader.GetTransactionByHash(utxo_hash, strPledgeTx))
	{
		ERRORLOG("TxHelper CreateRedeemTransaction: pledge tx not found");
		return -1;
	}

	CTransaction PledgeTx;
	if (!PledgeTx.ParseFromString(strPledgeTx))
	{
		return -2;
	}

	std::uint64_t pledge_amount = 0;
	for (int i = 0; i < PledgeTx.vout_size(); i++)
	{
		if (PledgeTx.vout(i).scriptpubkey() == VIRTUAL_ACCOUNT_PLEDGE)
		{
			pledge_amount = PledgeTx.vout(i).value();
			break;
		}
	}

	if (pledge_amount == 0)
	{
		ERRORLOG("TxHelper CreateRedeemTransaction: no pledge value");
		return -3;
	}

	std::map<std::string, int64_t> toAddr;
	toAddr.insert(std::make_pair(fromAddr, pledge_amount));
	int ret = Check(vecfromAddr, toAddr, needVerifyPreHashCount, minerFees, TxHelper::TransactionType::kTransactionType_Redeem);
	if(ret != 0)
	{
		ERRORLOG("TxHelper CreateRedeemTransaction: Check failed");
		ret -= 100;
		return ret;
	}

	// 查询账号是否已经质押资产
	std::vector<string> addresses;
	if (db_reader.GetPledgeAddress(addresses) != DBStatus::DB_SUCCESS)
	{
		ERRORLOG("TxHelper CreateRedeemTransaction: Get all pledge address failed");
		return -4;
	}
	if (std::find(addresses.begin(), addresses.end(), fromAddr) == addresses.end())
	{
		ERRORLOG("TxHelper CreateRedeemTransaction: pledge address not found");
		return -5;
	}

	// 查询要解质押的utxo是不是在已经质押的utxo里面
	std::vector<string> utxos;
	if (db_reader.GetPledgeAddressUtxo(fromAddr, utxos) != DBStatus::DB_SUCCESS)
	{
		ERRORLOG("TxHelper CreateRedeemTransaction: find pledge utxo from address failed");
		return -6;
	}
	if (std::find(utxos.begin(), utxos.end(), utxo_hash) == utxos.end())
	{
		ERRORLOG("TxHelper CreateRedeemTransaction: pledge utxo not in address");
		return -7;
	}

	// 查询质押是否超过三十天
	if (IsMoreThan30DaysForRedeem(utxo_hash) != 0)
	{
		ERRORLOG("TxHelper CreateRedeemTransaction: redeem utxo not more than 30 days");
		return -8;
	}

	// 计算总支出
	uint64_t minerfee = (needVerifyPreHashCount - 1) * minerFees; //签名费

	uint64_t publicNodePackageFee = 0;//打包费
	if (IsNeedPackage(vecfromAddr))
	{
		if (DBStatus::DB_SUCCESS != db_reader.GetDevicePackageFee(publicNodePackageFee))
		{
			ERRORLOG("TxHelper CreateRedeemTransaction: GetDevicePackageFee failed");
			return -9;
		}
	}

	uint64_t txfee = 0;//交易费
	uint64_t expend = minerfee + publicNodePackageFee + txfee;

	// 查找未使用的utxo
	uint64_t total;
	std::multiset<TxHelper::Utxo, TxHelper::UtxoCompare> setOutUtxos;
	ret = FindUtxo(vecfromAddr, expend, g_MaxVinSize - 1, total, setOutUtxos); //这里寻找的utxo数量需要减1，是因为解质押的一个Vin是固有的，所以只要找99个
	if (ret != 0)
	{
		ERRORLOG("TxHelper CreateRedeemTransaction: GetDevicePackageFee failed");
		ret -= 200;
		return ret;
	}

	if (setOutUtxos.empty())
	{
		ERRORLOG("TxHelper CreateRedeemTransaction: utxo is zero");
		return -10;
	}

	// 填充交易体
	uint64_t remain = total - expend;

	// 填充vin
	CTxin* txin = outTx.add_vin();
	txin->set_sequence(0);
	CTxprevout* prevout = txin->mutable_prevout();
	prevout->set_hash(utxo_hash);
	prevout->set_n(0);
	
	Utxo utxo;
	utxo.hash = utxo_hash;
	utxo.scriptpubkey = fromAddr;
	utxo.n = 0;
	utxo.value = pledge_amount;
	outVin.push_back(utxo);
	
	std::set<string> setTxowners;
	uint32_t n = 0;
	for (auto & utxo : setOutUtxos)
	{
		setTxowners.insert(utxo.scriptpubkey);
		CTxin* txin = outTx.add_vin();
		txin->set_sequence(++n) ;
		CTxprevout* prevout = txin->mutable_prevout();
		prevout->set_hash(utxo.hash);
		prevout->set_n(utxo.n);
		outVin.push_back(utxo);
	}

	if (setTxowners.empty())
	{
		ERRORLOG("TxHelper CreateRedeemTransaction: tx owner is zero");
		return -11;
	}

	CTxout* txoutToAddr = outTx.add_vout();
	txoutToAddr->set_scriptpubkey(toAddr.cbegin()->first);      // 解质押的给自己账号
	txoutToAddr->set_value(toAddr.cbegin()->second);

	txoutToAddr = outTx.add_vout();
	txoutToAddr->set_scriptpubkey(*setTxowners.rbegin());  // 剩余的给自己账号
	txoutToAddr->set_value(remain);

	outTx.set_time(Singleton<TimeUtil>::get_instance()->getlocalTimestamp());

	std::string strTxOwners;
	for (auto& addr : setTxowners)
	{
		strTxOwners += addr;
		strTxOwners += "_";
	}
	strTxOwners.erase(strTxOwners.end() - 1);
	outTx.set_owner(strTxOwners);
	outTx.set_version(1);
	outTx.set_identity(net_get_self_node_id());

	nlohmann::json txInfo;
	txInfo["RedeemptionUTXO"] = utxo_hash;
	txInfo["ReleasePledgeAmount"] = pledge_amount;
	
	nlohmann::json extra;
	extra["TransactionInfo"] = txInfo;
	extra["NeedVerifyPreHashCount"] = needVerifyPreHashCount;
	extra["SignFee"] = minerFees;
	extra["PackageFee"] = publicNodePackageFee;
	extra["TransactionType"] = TXTYPE_REDEEM;
	outTx.set_extra(extra.dump());

	return 0;
}

int TxHelper::SignTransaction(const std::vector<TxHelper::Utxo> &outVin,
							  CTransaction &tx,
                              std::string &serTx,
                              std::string &encodeStrHash)
{
	serTx = tx.SerializeAsString();
	if(serTx.empty())
	{
		return -1;
	}

	encodeStrHash = getsha256hash(base64Encode(serTx));

	//每一个拥有vin的账号对交易体签名
	for (int i = 0; i < tx.vin_size(); i++)
	{
		CTxin * txin = tx.mutable_vin(i);
		for (auto & item : outVin)
		{
			if (item.hash == txin->prevout().hash() && item.n == txin->prevout().n())
			{
				std::string addr = item.scriptpubkey;
				std::string signature;
				std::string strPub;
				g_AccountInfo.Sign(addr.c_str(), encodeStrHash, signature);
				g_AccountInfo.GetPubKeyStr(addr.c_str(), strPub);
				
				txin->mutable_scriptsig()->set_sign(signature);
				txin->mutable_scriptsig()->set_pub(strPub);
				break;
			}
		}
	}

	serTx = tx.SerializeAsString();
	if(serTx.empty())
	{
		return -2;
	}

	return 0;
}



int TxHelper::CreateDeployContractMessage(const std::string & owneraddr, 
	const std::string &contract_name,
	const std::string &contract_raw, 
	const std::string &abi, 
	const std::string &contractversion, 
	const std::string &description, 
	uint32_t needVerifyPreHashCount, 
	uint64_t minerFees, 
	uint64_t amount, 
	uint64_t impcnt,
	CTransaction & outTx,
	std::vector<TxHelper::Utxo> & outVin
	)
{
	std::vector<std::string> fromAddr;
	fromAddr.push_back(owneraddr);
	std::map<std::string, int64_t> toAddr;
	toAddr.insert(std::make_pair(VIRTUAL_ACCOUNT_DEPLOY, amount));
	int ret = Check(fromAddr, toAddr, needVerifyPreHashCount, minerFees, TxHelper::TransactionType::kTransactionType_Contract_Deploy);
	if(ret != 0)
	{
		ERRORLOG("TxHelper CreatePledgeTransaction: Check failed");
		ret -= 100;
		return ret;
	}
	// 计算总支出
	uint64_t minerfee = (needVerifyPreHashCount - 1) * minerFees; //签名费

	DBReader db_reader;   
	uint64_t publicNodePackageFee = 0;//打包费
	if (IsNeedPackage(fromAddr))
	{
		if (DBStatus::DB_SUCCESS != db_reader.GetDevicePackageFee(publicNodePackageFee))
		{
			ERRORLOG("TxHelper CreateTxTransaction: GetDevicePackageFee failed");
			return -1;
		}
	}

	uint64_t txfee = 0;//交易费
	for (auto& i : toAddr)
	{
		txfee += i.second;    
	}
	uint64_t expend = minerfee + publicNodePackageFee + txfee;

	// 寻找utxo
	uint64_t total = 0;
	std::multiset<TxHelper::Utxo, TxHelper::UtxoCompare> setOutUtxos;
	ret = FindUtxo(fromAddr, expend, g_MaxVinSize, total, setOutUtxos);
	if (ret != 0)
	{
		ERRORLOG("TxHelper CreateTxTransaction: FindUtxo failed");
		ret -= 200;
		return ret;
	}

	if (setOutUtxos.empty())
	{
		ERRORLOG("TxHelper CreateTxTransaction: utxo is zero");
		return -2;
	}

	// 填充交易体
	uint64_t remain = total - expend;

	// 填充vin
	std::set<string> setTxowners;
	uint32_t n = 0;
	for (auto & utxo : setOutUtxos)
	{
		setTxowners.insert(utxo.scriptpubkey);
		CTxin* txin = outTx.add_vin();
		txin->set_sequence(n++);
		CTxprevout* prevout = txin->mutable_prevout();
		prevout->set_hash(utxo.hash);
		prevout->set_n(utxo.n);
		outVin.push_back(utxo);
	}

	if (setTxowners.empty())
	{
		ERRORLOG("TxHelper CreateTxTransaction: tx owner is zero");
		return -3;
	}

	// 填充vout
	for (auto& to : toAddr)
	{
		CTxout* txoutToAddr = outTx.add_vout();
		txoutToAddr->set_scriptpubkey(to.first);
		txoutToAddr->set_value(to.second);
	}

	CTxout* txoutFromAddr = outTx.add_vout();
	txoutFromAddr->set_value(remain);
	txoutFromAddr->set_scriptpubkey(*setTxowners.rbegin());

	outTx.set_time(Singleton<TimeUtil>::get_instance()->getlocalTimestamp());

	std::string strTxOwners;
	for (auto& addr : setTxowners)
	{
		strTxOwners += addr;
		strTxOwners += "_";
	}
	strTxOwners.erase(strTxOwners.end() - 1);
	outTx.set_owner(strTxOwners);
	outTx.set_version(1);
	outTx.set_identity(net_get_self_node_id());


	uint64_t  ContractImplementFee = amount/impcnt;
	if(ContractImplementFee <= g_minSignFee)
	{
		ContractImplementFee = g_minSignFee;
	}
	nlohmann::json txInfo;
	txInfo["ExecuteCount"] = impcnt;
	txInfo["DeployAmount"] = amount;
	nlohmann::json extra;
	extra["TransactionInfo"] = txInfo;
	extra["owneraddr"]  = owneraddr;
	extra["NeedVerifyPreHashCount"] = needVerifyPreHashCount;
	extra["SignFee"] = minerFees;
	extra["PackageFee"] = publicNodePackageFee;
	extra["TransactionType"] = TXTYPE_CONTRACT_DEPLOY;
	extra["ContractName"] = contract_name;
	extra["ContractImplementFee"] = ContractImplementFee;
	extra["Contract"] = base64Encode(contract_raw);
	extra["Abi"] = base64Encode(abi);
	extra["contractversion"] = contractversion;
	extra["description"] = description;
	outTx.set_extra(extra.dump());
	return 0;		
}


void TxHelper::DoDeployContract(const std::string & owneraddr, 
	const std::string &contract_name,
	const std::string &contract_raw, 
	const std::string &abi,
	const std::string &contractversion,
	const std::string &description,
	uint32_t needVerifyPreHashCount,
	uint64_t gasFee,
	uint64_t  amount,
	uint64_t impcnt,
	std::string &txhash)
 { 
	// 检查参数
	CTransaction outTx;
	std::vector<TxHelper::Utxo> outVin;
    int ret = TxHelper::CreateDeployContractMessage(owneraddr, contract_name, contract_raw, abi,contractversion ,description,needVerifyPreHashCount, gasFee, amount,impcnt,outTx,outVin);
	if(ret != 0)
	{
		ERRORLOG("DoDeployContract: TxHelper::CreateDeployContractMessage error!!");
		return;
	}

	util::JsonPrintOptions options;
	options.add_whitespace = true;
	options.always_print_primitive_fields = true;
	options.preserve_proto_field_names = true;

	std::string out_str;
	util::MessageToJsonString(outTx, &out_str, options);
	//std::cout << "CTransaction:" << std::endl;	
	//std::cout << out_str << std::endl;	
	
	std::string serTx;
    std::string encodeStrHash;
    if(TxHelper::SignTransaction(outVin, outTx, serTx, encodeStrHash) != 0)
    {
		ERRORLOG("SignTransaction error!!");
		return;        
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
		return;
	}
	txMsg.set_prevblkhash(blockHash);
	txMsg.set_trycountdown(CalcTxTryCountDown(needVerifyPreHashCount));

	auto msg = make_shared<TxMsg>(txMsg);
	ret = DoHandleTx(msg, txhash);
	DEBUGLOG("交易处理结果，ret:{}  txHash：{}", ret, txhash);
	cout<<"txhash=---------txhash="<<txhash<<endl;
}


int TxHelper::DeployContractToDB(const CTransaction & tx, DBReadWriter &db_writer)
{
    auto extra = nlohmann::json::parse(tx.extra());
    std::string type = extra["TransactionType"].get<std::string>();
	if(type != TXTYPE_CONTRACT_DEPLOY)
	{
		return -1;
	}
	
	auto transaction_type = CheckTransactionType(tx);
	if(transaction_type != TransactionType::kTransactionType_Tx)
	{
		return -2;
	}
	std::string addr = extra["owneraddr"].get<std::string>();
    std::string contract_name = extra["ContractName"].get<std::string>();
    std::string contract = extra["Contract"].get<std::string>();
	contract =  base64Decode(contract);
    std::string abi = extra["Abi"].get<std::string>();
	cout<<"base64abi = "<<abi<<endl;
	//abi = base64Decode(abi);
	std::string txhash = tx.hash();
	cout<<"DeployContractToDB---> txhash = "<<txhash<<endl;
	db_writer.SetDeployContractTxHashByAddress(addr,contract_name,txhash);
	db_writer.SetAddressByContractName(addr,contract_name);
	db_writer.SetContractNameAbiName(contract_name,abi);     
	return 0;	
}	

int TxHelper::ExecuteContractToDB(const CTransaction & tx, DBReadWriter &db_writer)
{
	auto extra = nlohmann::json::parse(tx.extra());
    std::string type = extra["TransactionType"].get<std::string>();
	if(type != TXTYPE_CONTRACT_EXECUTE)
	{
		return -1;
	}
	
	auto transaction_type = CheckTransactionType(tx);
	if(transaction_type != TransactionType::kTransactionType_Tx)
	{
		return -2;
	}
	std::string addr = extra["addr"].get<std::string>();
    std::string contract_name = extra["Contract"].get<std::string>();
	std::string txhash = tx.hash();
	// cout<<"ExecuteContractToDB---> txhash = "<<txhash<<endl;
	db_writer.SetExecuteContractTxHashByAddress(addr,contract_name,txhash);
	return 0;
}

int TxHelper::CreateExecuteContractMessage(const std::string & addr, 
	const std::string &contract, 
	const std::string &action,
	const std::string &params,
	uint32_t needVerifyPreHashCount, 
	uint64_t minerFees, 
	CTransaction & outTx,
	std::vector<TxHelper::Utxo> & outVin)
{
	std::vector<std::string> fromAddr;
	fromAddr.push_back(addr);
	DBReader db_reader;
	std::string transferaddr;
	uint64_t ContractImplementFee;
	std::string txhash;
	std::string blockhash;
	std::string blockraw;
	if (DBStatus::DB_SUCCESS != db_reader.GetAddressByContractName( contract, transferaddr))
	{
		cout<<"	CreateExecuteContractMessage-->db_reader.GetAddressByContractName( contract, transferaddr)"<<endl;
		ERRORLOG("b_reader.GetAddressByContractName error!!");
		return false;
	}
	if (DBStatus::DB_SUCCESS != db_reader.GetDeployContractTXHashByAddress(transferaddr,contract,txhash) )
	{
		cout<<"CreateExecuteContractMessage-->pRocksDb->GetContractByAddress(transferaddr,contractname,txhash)"<<endl;
		ERRORLOG("db_reader.GetDeployContractTXHashByAddress error!!");
		return false;
	}
	if(DBStatus::DB_SUCCESS != db_reader.GetBlockHashByTransactionHash(txhash,blockhash))
	{
		cout<<"CreateExecuteContractMessage-->	GetBlockHashByTransactionHash( const std::string & txHash, std::string & blockHash)"<<endl;
		ERRORLOG("db_reader.GetBlockHashByTransactionHash error!!");
		return false;
	}
	if (DBStatus::DB_SUCCESS != db_reader.GetBlockByBlockHash(blockhash,  blockraw) )
	{
		cout<<"CreateExecuteContractMessage-->GetBlockByBlockHash(txn,  blockhash,  blockraw)"<<endl;
		ERRORLOG("db_reader.GetBlockByBlockHash error!!");
		return false;
	}
	
	CBlock Block;
	Block.ParseFromString(blockraw);
	for (int j = 0; j < Block.txs_size(); j++) 
	{
		CTransaction txcontract = Block.txs(j);
		auto transaction_type = CheckTransactionType(txcontract);
		if(transaction_type == kTransactionType_Tx)
		{
			{
				nlohmann::json extratx = nlohmann::json::parse(txcontract.extra());
				ContractImplementFee = extratx["ContractImplementFee"];
			} 
		}
	}        

	std::map<std::string, int64_t> toAddr;
	toAddr.insert(std::make_pair(transferaddr, ContractImplementFee));
	int ret = Check(fromAddr, toAddr, needVerifyPreHashCount, minerFees, TxHelper::TransactionType::kTransactionType_Contract_Execute);
	if(ret != 0)
	{
		ERRORLOG("TxHelper CreatePledgeTransaction: Check failed");
		ret -= 100;
		return ret;
	}
	// 计算总支出
	uint64_t minerfee = (needVerifyPreHashCount - 1) * minerFees; //签名费

	uint64_t publicNodePackageFee = 0;//打包费
	if (IsNeedPackage(fromAddr))
	{
		if (DBStatus::DB_SUCCESS != db_reader.GetDevicePackageFee(publicNodePackageFee))
		{
			ERRORLOG("TxHelper CreateTxTransaction: GetDevicePackageFee failed");
			return -1;
		}
	}

	uint64_t txfee = 0;//交易费
	for (auto& i : toAddr)
	{
		txfee += i.second;    
	}
	uint64_t expend = minerfee + publicNodePackageFee + txfee;

	// 寻找utxo
	uint64_t total = 0;
	std::multiset<TxHelper::Utxo, TxHelper::UtxoCompare> setOutUtxos;
	ret = FindUtxo(fromAddr, expend, g_MaxVinSize, total, setOutUtxos);
	if (ret != 0)
	{
		ERRORLOG("TxHelper CreateTxTransaction: FindUtxo failed");
		ret -= 200;
		return ret;
	}

	if (setOutUtxos.empty())
	{
		ERRORLOG("TxHelper CreateTxTransaction: utxo is zero");
		return -2;
	}

	// 填充交易体
	uint64_t remain = total - expend;

	// 填充vin
	std::set<string> setTxowners;
	uint32_t n = 0;
	for (auto & utxo : setOutUtxos)
	{
		setTxowners.insert(utxo.scriptpubkey);
		CTxin* txin = outTx.add_vin();
		txin->set_sequence(n++);
		CTxprevout* prevout = txin->mutable_prevout();
		prevout->set_hash(utxo.hash);
		prevout->set_n(utxo.n);
		outVin.push_back(utxo);
	}

	if (setTxowners.empty())
	{
		ERRORLOG("TxHelper CreateTxTransaction: tx owner is zero");
		return -3;
	}

	// 填充vout
	for (auto& to : toAddr)
	{
		CTxout* txoutToAddr = outTx.add_vout();
		txoutToAddr->set_scriptpubkey(to.first);
		txoutToAddr->set_value(to.second);
	}

	CTxout* txoutFromAddr = outTx.add_vout();
	txoutFromAddr->set_value(remain);
	txoutFromAddr->set_scriptpubkey(*setTxowners.rbegin());

	outTx.set_time(Singleton<TimeUtil>::get_instance()->getlocalTimestamp());

	std::string strTxOwners;
	for (auto& addr : setTxowners)
	{
		strTxOwners += addr;
		strTxOwners += "_";
	}
	strTxOwners.erase(strTxOwners.end() - 1);
	outTx.set_owner(strTxOwners);
	outTx.set_version(1);
	outTx.set_identity(net_get_self_node_id());
	std::string execret;
	std::string owneraddr;
	if (DBStatus::DB_SUCCESS != db_reader.GetAddressByContractName( contract,  owneraddr))
	{
		ERRORLOG("db_reader.GetAddressByContractName error");
		return false;
	}
	TxHelper::ExecutePreContractToBlock( owneraddr, contract, action,params, db_reader,execret);
	nlohmann::json extra;
	extra["addr"]  = addr;
	extra["NeedVerifyPreHashCount"] = needVerifyPreHashCount;
	extra["SignFee"] = minerFees;
	extra["TransactionType"] = TXTYPE_CONTRACT_EXECUTE;
	extra["PackageFee"] = publicNodePackageFee;
	extra["Contract"] = contract;
	extra["Action"] = action;	
	extra["Param"] = base64Encode(params);
	extra["Executionresult"] = execret;
	
	outTx.set_extra(extra.dump());
	return 0;		
}		


void TxHelper::DoeExecuteContract(const std::string & addr, 
	const std::string &contract, 
	const std::string &action,
	const std::string &params,
	uint32_t needVerifyPreHashCount, 
	uint64_t gasFee,
	std::string &txhash)
{
	CTransaction outTx;
	std::vector<TxHelper::Utxo> outVin;
    int ret = TxHelper::CreateExecuteContractMessage(addr, contract, action, params, needVerifyPreHashCount, gasFee, outTx,outVin);
	if(ret != 0)
	{
		ERRORLOG("DoeExecuteContract: TxHelper::CreateDeployContractMessage error!!");
		return;
	}

	util::JsonPrintOptions options;
	options.add_whitespace = true;
	options.always_print_primitive_fields = true;
	options.preserve_proto_field_names = true;

	std::string out_str;
	util::MessageToJsonString(outTx, &out_str, options);
	// std::cout << "CTransaction:" << std::endl;	
	// std::cout << out_str << std::endl;	
	std::string serTx;
    std::string encodeStrHash;
    if(TxHelper::SignTransaction(outVin, outTx, serTx, encodeStrHash) != 0)
    {
		ERRORLOG("SignTransaction error!!");
		return;        
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
	if (DBStatus::DB_SUCCESS != db_reader.GetBestChainHash(blockHash) )
    {
		ERRORLOG("DoeExecuteContract: GetBestChainHash error!!");
        return ;
    }
    txMsg.set_prevblkhash(blockHash);	
    txMsg.set_trycountdown(CalcTxTryCountDown(needVerifyPreHashCount));

	auto msg = make_shared<TxMsg>(txMsg);
		
	ret = DoHandleTx(msg, txhash);
	DEBUGLOG("交易处理结果，ret:{}  txHash：{}", ret, txhash);
	cout<<"txhash = "<<txhash<<endl;
}	

int TxHelper::ExecutePreContractToBlock( const std::string & addr,  
        const std::string &contractname, 
        const std::string &action,
        const std::string &params, 
		DBReader &db_reader,std::string &execret)
{
	// 从链上获取contract， abi
	std::string contract_raw;
	std::string abi_raw;
	std::string txhash;
	std::string blockhash;
	std::string blockraw;
	std::string txRaw;
	
	if (DBStatus::DB_SUCCESS != db_reader.GetDeployContractTXHashByAddress(addr,contractname,txhash) )
	{
		cout<<"ExecutePreContractToBlock--->pRocksDb->GetContractByAddress(addr,contractname,txhash)"<<endl;
		ERRORLOG("db_reader.GetDeployContractTXHashByAddress error");
		return false;
	}
	if(DBStatus::DB_SUCCESS != db_reader.GetBlockHashByTransactionHash(txhash,blockhash))
	{
		cout<<"ExecutePreContractToBlock-->GetBlockHashByTransactionHash( const std::string & txHash, std::string & blockHash)"<<endl;
		ERRORLOG("db_reader.GetBlockHashByTransactionHash error");
		return false;
	}
	if (DBStatus::DB_SUCCESS != db_reader.GetBlockByBlockHash(blockhash,  blockraw) )
	{
		cout<<"ExecutePreContractToBlock-->GetBlockByBlockHash(txn,  blockhash,  blockraw)"<<endl;
		ERRORLOG("db_reader.GetBlockByBlockHash error");
		return false;
	}
	
	CBlock Block;
	Block.ParseFromString(blockraw);
	for (int j = 0; j < Block.txs_size(); j++) 
	{
		CTransaction txcontract = Block.txs(j);
		auto transaction_type = CheckTransactionType(txcontract);
		if(transaction_type == kTransactionType_Tx)
		{
			{
				nlohmann::json extratx = nlohmann::json::parse(txcontract.extra());
				contract_raw = extratx["Contract"];
				contract_raw = base64Decode(contract_raw);
				abi_raw = extratx["Abi"];
				abi_raw = base64Decode(abi_raw);
				//cout<<"ExecutePreContractToBlock contract_raw size()= "<<contract_raw.size()<<endl;
				//cout<<"ExecutePreContractToBlock abi_raw size()= "<<abi_raw.size()<<endl;
			} 
		}
	}        
    wasm::wasm_context cnt(wasm::ExecuteType::block, contractname, "", contract_raw, abi_raw, action, params, 0);
    cnt.execute();
	execret = cnt.get_execresult();
	cout<<"execret = "<<execret<<endl;
	return 0;	
}

	
