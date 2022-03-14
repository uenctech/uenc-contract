#include <iostream>
#include <time.h>
#include <stdio.h>
#include <string>
#include <fstream>
#include <stdlib.h>
#include <stdarg.h>
#include <array>
#include <fcntl.h>
#include <unistd.h>
#include "ca_test.h"
#include "ca_transaction.h"
#include "ca_buffer.h"
#include "ca_interface.h"
#include "ca_console.h"
#include "ca_hexcode.h"
#include "ca_serialize.h"

#include "proto/block.pb.h"
#include "proto/transaction.pb.h"
#include "MagicSingleton.h"
#include "../include/logging.h"
#include "../include/ScopeGuard.h"

#include "db/db_api.h"

void time_format_info(char * ps, uint64_t time, bool ms, CaTestFormatType type)
{
	if (ps == nullptr)
	{
		return;
	}
	
	time_t s = (time_t)(ms ? (time / 1000000) : time);
    struct tm * gm_date;
    gm_date = localtime(&s);
	ca_console tmColor(kConsoleColor_White, kConsoleColor_Black, true);

	if(type == kCaTestFormatType_Print)
	{
    	sprintf(ps, "%s%d-%d-%d %d:%d:%d (%zu) %s\n", tmColor.color().c_str(), gm_date->tm_year + 1900, gm_date->tm_mon + 1, gm_date->tm_mday, gm_date->tm_hour, gm_date->tm_min, gm_date->tm_sec, time, tmColor.reset().c_str());
	}
	else if (type == kCaTestFormatType_File)
	{
		sprintf(ps, "%d-%d-%d %d:%d:%d (%zu) \n", gm_date->tm_year + 1900, gm_date->tm_mon + 1, gm_date->tm_mday, gm_date->tm_hour, gm_date->tm_min, gm_date->tm_sec, time);
    }
}

void blkprint(CaTestFormatType type, int fd, const char *format, ...)
{
    auto len = strlen(format) + 1024 * 32;
    auto buffer = new char[len]{0};
	va_list valist;

	va_start(valist, format);

	vsnprintf(buffer, len, format, valist);

	if (type == kCaTestFormatType_Print) 
	{
		printf( "%s", buffer);
	}
	else if (type == kCaTestFormatType_File)
 	{
		if(fd < 0)
		{
            close(fd);
			return;
		}

		write(fd, buffer, strlen(buffer));
	}

	va_end(valist);
    delete[] buffer;
}

void printRocksdb(uint64_t start, uint64_t end) 
{
    int fd = -1;

    if(start > end)
    {
        ERRORLOG("start > end");
        return ;
    }

    if (end - start > 5) 
    {
        std::string fileName = "print_blk_" + std::to_string(start) + "_" + std::to_string(end) + ".txt";
        fd = open(fileName.c_str(), O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    }
    DBReader db_reader;
    
    CaTestFormatType formatType;
    if (fd == -1) {
        formatType = kCaTestFormatType_Print;
    } else {
        formatType = kCaTestFormatType_File;
    }

    uint64_t height = 0;
    ca_console bkColor(kConsoleColor_Blue, kConsoleColor_Black, true);
    size_t b_count {0};//交易总数
    for (auto i = end; i >= start; --i) {
        height = i;

        std::vector<std::string> vBlockHashs;
        db_reader.GetBlockHashsByBlockHeight(height, vBlockHashs);
        std::vector<CBlock> blocks;
        for (auto hash : vBlockHashs)
        {
            string strHeader;
            db_reader.GetBlockByBlockHash(hash, strHeader);
            CBlock block;
            block.ParseFromString(strHeader);
            blocks.push_back(block);
        }
        std::sort(blocks.begin(), blocks.end(), [](CBlock & a, CBlock & b){
            return a.time() < b.time();
        });
        for (auto header : blocks)
        {
            ++b_count;
			blkprint(formatType, fd, "\nBlockInfo ---------------------- > height [%ld]\n", height);
            CBlockHeader block;
            block.set_hash(header.hash());
            block.set_height(height);
            blkprint(formatType, fd, "HashMerkleRoot      -> %s\n",  header.merkleroot().c_str());
            blkprint(formatType, fd, "HashPrevBlock       -> %s\n", header.prevhash().c_str());
            if (formatType == kCaTestFormatType_File) 
            {
                blkprint(formatType, fd, "BlockHash           -> %s\n", block.hash().c_str());
            } 
            else if (formatType == kCaTestFormatType_Print) 
            {
                blkprint(formatType, fd, "BlockHash           -> %s%s%s\n", bkColor.color().c_str(), block.hash().c_str(), bkColor.reset().c_str());
            }

            blkprint(formatType, fd, "nTime			-> ");	
            char blockTime[256] = {};
            time_format_info(blockTime, header.time(), CA_TEST_NTIME, formatType);
            blkprint(formatType, fd, blockTime);

            for (int i = 0; i < header.txs_size(); i++) 
            {
                CTransaction tx = header.txs(i);
                blkprint(formatType, fd, "TX_INFO  -----------> index[%d] start to print \n", i);
                ca_console txColor(kConsoleColor_Red, kConsoleColor_Black, true);
                if (formatType == kCaTestFormatType_File) 
                {
                    blkprint(formatType, fd, "tx[%d] TxHash   -> %s\n", i, tx.hash().c_str());
                } 
                else if (formatType == kCaTestFormatType_Print) 
                {
                    blkprint(formatType, fd, "tx[%d] TxHash   -> %s%s%s\n", i, txColor.color().c_str(), tx.hash().c_str(), txColor.reset().c_str());
                }
                blkprint(formatType, fd, "tx[%d] SignPreHash Length[%zu]   -> \n", i, tx.signprehash_size());

                ca_console greenColor(kConsoleColor_Green, kConsoleColor_Black, true);
                for (int k = 0; k < tx.signprehash_size(); k++) 
                {
                    int signLen = tx.signprehash(k).sign().size();
                    int pubLen = tx.signprehash(k).pub().size();
                    char * hexSign = new char[signLen * 2 + 2]{0};
                    char * hexPub = new char[pubLen * 2 + 2]{0};

                    encode_hex(hexSign, tx.signprehash(k).sign().c_str(), signLen);
                    encode_hex(hexPub, tx.signprehash(k).pub().c_str(), pubLen);

                    // std::string base58Addr = GetBase58Addr(std::string(tx.signprehash(k).pub(), pubLen));
                    char buf[2048] = {0};
                    size_t buf_len = sizeof(buf);
                    GetBase58Addr(buf, &buf_len, 0x00, tx.signprehash(k).pub().c_str(), tx.signprehash(k).pub().size());

                    blkprint(formatType, fd, "SignPreHash %s : %s[%s%s%s] \n", hexSign, hexPub, greenColor.color().c_str(), buf, greenColor.reset().c_str());

                    delete[] hexSign;
                    delete[] hexPub;
                }

                for (int k = 0; k < tx.signprehash_size(); k++) 
                {
                    char buf[2048] = {0};
                    size_t buf_len = sizeof(buf);
                    GetBase58Addr(buf, &buf_len, 0x00, tx.signprehash(k).pub().c_str(), tx.signprehash(k).pub().size());
                    if (formatType == kCaTestFormatType_File) 
                    {
                        blkprint(formatType, fd, "Transaction signer [%s]\n", buf);
                    } 
                    else if (formatType == kCaTestFormatType_Print) 
                    {
                        blkprint(formatType, fd, "Transaction signer   -> [%s%s%s]\n", greenColor.color().c_str(), buf, greenColor.reset().c_str());
                    }
                }

                for (int j = 0; j < tx.vin_size(); j++) 
                {
                    CTxin vtxin= tx.vin(j);
                    blkprint(formatType, fd, "vin[%d] scriptSig Length[%zu]    -> \n", j, tx.vin_size());

                    int signLen = vtxin.scriptsig().sign().size();
                    int pubLen = vtxin.scriptsig().pub().size();
                    char * hexSign = new char[signLen * 2 + 2]{0};
                    char * hexPub = new char[pubLen * 2 + 2]{0};
                    encode_hex(hexSign, vtxin.scriptsig().sign().c_str(), signLen);
                    encode_hex(hexPub, vtxin.scriptsig().pub().c_str(), pubLen);

                    // std::string base58Addr = GetBase58Addr(std::string(vtxin.scriptsig().pub(), pubLen));
                    char buf[2048] = {0};
                    size_t buf_len = sizeof(buf);
                    GetBase58Addr(buf, &buf_len, 0x00, vtxin.scriptsig().pub().c_str(), vtxin.scriptsig().pub().size());

                    if( vtxin.scriptsig().pub().size() == 0 )
                    {
                        blkprint(formatType, fd, "scriptSig %s \n", vtxin.scriptsig().sign().c_str());
                    }
                    else
                    {
                        blkprint(formatType, fd, "scriptSig %s : %s[%s%s%s]\n", hexSign, hexPub, greenColor.color().c_str(), buf, greenColor.reset().c_str());
                    }

                    delete[] hexSign;
                    delete[] hexPub;

                    blkprint(formatType, fd, "vin[%d] PrevoutHash   -> %s\n", j, vtxin.prevout().hash().c_str());
                }	

                ca_console pubkeyColor(kConsoleColor_Green, kConsoleColor_Black, true);
                for (int i = 0; i <tx.vout_size(); i++) {
                    CTxout vtxout = tx.vout(i);
                    ca_console amount(kConsoleColor_Yellow, kConsoleColor_Black, true);
                    if (formatType == kCaTestFormatType_File) {
                        blkprint(formatType, fd, "vout[%d] scriptPubKey[%zu]  -> %s\n", i, vtxout.scriptpubkey().size(), vtxout.scriptpubkey().c_str());
						blkprint(formatType, fd, "vout[%d] mount[%ld] \n", i, vtxout.value());
                    } else if (formatType == kCaTestFormatType_Print) {
                        blkprint(formatType, fd,"vout[%d] scriptPubKey[%zu]  [%s%s%s]\n", i, vtxout.scriptpubkey().size(), pubkeyColor.color().c_str(), vtxout.scriptpubkey().c_str(), pubkeyColor.reset().c_str());
						blkprint(formatType, fd,"vout[%d] mount[%s%ld%s] \n", i, amount.color().c_str(), vtxout.value(), amount.reset().c_str());
                    }
                }
                
                blkprint(formatType, fd, "nLockTime:  ");	
                char txTime[256] {0};
                time_format_info(txTime, tx.time(), CA_TEST_NTIME, formatType);
                blkprint(formatType, fd, txTime);	

                std::vector<std::pair<std::string, std::string>> extraMap;
                std::string strExtra;
                try
                {
                    nlohmann::json extra_json = nlohmann::json::parse(tx.extra());
                    std::string txType = extra_json["TransactionType"].get<std::string>();
                    extraMap.push_back(std::make_pair("TransactionType", txType));
                    extraMap.push_back(std::make_pair("NeedVerifyPreHashCount", to_string(extra_json["NeedVerifyPreHashCount"].get<uint32_t>())));
                    extraMap.push_back(std::make_pair("SignFee", to_string(extra_json["SignFee"].get<uint32_t>())));
                    extraMap.push_back(std::make_pair("PackageFee", to_string(extra_json["PackageFee"].get<uint32_t>())));
             

                    if (txType == TXTYPE_TX)
                    {
                        // nothing to do
                    }
                    else if (txType == TXTYPE_PLEDGE)
                    {
                        extraMap.push_back(std::make_pair("PledgeType", extra_json["TransactionInfo"]["PledgeType"].get<std::string>()));
                        extraMap.push_back(std::make_pair("PledgeAmount", to_string(extra_json["TransactionInfo"]["PledgeAmount"].get<uint64_t>())));
                    }
                    else if (txType == TXTYPE_REDEEM)
                    {
                        extraMap.push_back(std::make_pair("RedeemptionUTXO", extra_json["TransactionInfo"]["RedeemptionUTXO"].get<std::string>()));
                        extraMap.push_back(std::make_pair("ReleasePledgeAmount", to_string(extra_json["TransactionInfo"]["ReleasePledgeAmount"].get<uint64_t>())));
                    }
                    
                    for (auto & item : extraMap)
                    {
                        strExtra += item.first + " : " + item.second + "\n";
                    }
                }
                catch (...)
                {
                    
                }

                blkprint(formatType, fd, "extra   ->\n%s", strExtra.c_str());
                blkprint(formatType, fd, "nVersion   -> %d\n", tx.version());
            }
        }
        if (i == 0) break;
    }
    DEBUGLOG("count = {}", b_count);
    close(fd);
}

std::string printBlocks(int num, bool pre_hash_flag)
{
    DBReader db_read;
    uint64_t top = 0;
    db_read.GetBlockTop(top);
    std::string str = "top:\n";
    str += "--------------\n";
    int j = 0;
    for(int i = top; i >= 0; i--){
        str += (std::to_string(i) + "\t");
        std::vector<std::string> vBlockHashs;
        db_read.GetBlockHashsByBlockHeight(i, vBlockHashs);
        std::sort(vBlockHashs.begin(), vBlockHashs.end());
        for (auto hash : vBlockHashs) {   
            string strHeader;
            db_read.GetBlockByBlockHash(hash, strHeader);
            CBlock header;
            header.ParseFromString(strHeader);
            if(pre_hash_flag)
            {
                str = str + hash.substr(0,6) + "(" + header.prevhash().substr(0,6) + ")" + " ";
            }else{
                str = str + hash.substr(0,6) + " ";
            }
        } 
        str += "\n";
        j++;
        if(num > 0 && j >= num)
        {
            break;
        }
    }
    str += "--------------\n";   
    return str;
}

void SplitString(const std::string& s, std::vector<std::string>& v, const std::string& c)
{
    string::size_type pos1, pos2;
    pos2 = s.find(c);
    pos1 = 0;
    while(std::string::npos != pos2)
    {
        v.push_back(s.substr(pos1, pos2-pos1));
         
        pos1 = pos2 + c.size();
        pos2 = s.find(c, pos1);
    }
    if(pos1 != s.length())
        v.push_back(s.substr(pos1));
}
