#ifndef EBPC_CA_H
#define EBPC_CA_H

#include <iostream>
#include <thread>
#include <shared_mutex>

#include "proto/ca_protomsg.pb.h"
#include "net/msg_queue.h"

extern std::shared_mutex g_NodeInfoLock;
extern bool bStopTx;
extern bool bIsCreateTx;

void InitFee();
void RegisterCallback();
void TestCreateTx(const std::vector<std::string> & addrs, const int & sleepTime, const std::string & signFee);
std::string PrintTime(uint64_t time);

/**
* @description: ca 初始化
* @param 无
* @return: 成功返回ture，失败返回false
*/
bool ca_init();


/**
 * @description: ca 清理函数
 * @param 无
 * @return: 无
 */
void ca_cleanup();

/**
 * @description: ca 菜单
 * @param 无
 * @return: 无
 */
void ca_print();


/**
 * @description: ca 版本
 * @param 无
 * @return: 返回字符串格式版本号
 */
const char * ca_version();


/**
 * @description: 获取某个节点的最高块的高度和hash
 * @param id 要获取的节点的id
 * @return: 无
 */
void SendDevInfoReq(const std::string id);


/* ====================================================================================  
 # @description:  处理接收到的节点信息
 # @param msg  : 接收的协议数据
 # @param msgdata : 网络传输所需的数据
 ==================================================================================== */
int HandleGetDevInfoAck( const std::shared_ptr<GetDevInfoAck>& msg, const MsgData& msgdata );


/* ====================================================================================  
 # @description: 处理接收到的获取节点信息请求
 # @param msg  : 接收的协议数据
 # @param msgdata : 网络传输所需的数据
 ==================================================================================== */
int HandleGetDevInfoReq( const std::shared_ptr<GetDevInfoReq>& msg, const MsgData& msgdata );


/**
 * @description: 主菜单使用的相关实现函数
 * @create: 20201104   LiuMingLiang
 */
void ca_print_basic_info();
int get_device_signature_fee(uint64_t &minerFee);
int set_device_signature_fee(uint64_t fee);
int get_device_package_fee(uint64_t& packageFee);
int set_device_package_fee(uint64_t fee);

void handle_transaction();
void handle_pledge();
void handle_redeem_pledge();
void handle_change_password();
void handle_set_config();
void handle_set_signature_fee();
void handle_set_package_fee();
void handle_set_fee();
void handle_export_private_key();
void handle_print_pending_transaction_in_cache();

//NTP检验
int checkNtpTime();

bool isPublicIp(const string& ip);
int UpdatePublicNodeToConfigFile();

int get_chain_height(unsigned int & chainHeight);

#endif
