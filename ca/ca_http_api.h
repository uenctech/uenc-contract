/*
 * @Author: your name
 * @Date: 2021-04-02 16:21:32
 * @LastEditTime: 2021-12-15 11:07:43
 * @LastEditors: Please set LastEditors
 * @Description: In User Settings Edit
 * @FilePath: \ebpc\ca\ca_http_api.h
 */
#include "../net/http_server.h"
#include "ca_test.h"


void ca_register_http_callbacks();



void api_jsonrpc(const Request & req, Response & res);

void api_print_block(const Request & req, Response & res);
void api_info(const Request & req, Response & res);
void api_node_cache(const Request &req, Response &res);
void api_info_queue(const Request & req, Response & res);
void api_get_block(const Request & req, Response & res);
void api_get_block_hash(const Request & req, Response & res);
void api_get_block_by_hash(const Request & req, Response & res);

void api_get_tx_owner(const Request & req, Response & res);
void api_get_gas(const Request & req, Response & res);
void api_cache_info(const Request & req, Response & res);
void api_black_list(const Request & req, Response & res);

void api_pub(const Request & req, Response & res);


void test_create_multi_tx(const Request & req, Response & res);
void api_get_db_key(const Request & req, Response & res);
void add_block_callback_test(const Request & req, Response & res);

nlohmann::json jsonrpc_test(const nlohmann::json & param);

nlohmann::json jsonrpc_get_height(const nlohmann::json & param);
nlohmann::json jsonrpc_get_basic_info(const nlohmann::json & param);
nlohmann::json jsonrpc_get_balance(const nlohmann::json & param);
nlohmann::json jsonrpc_get_txids_by_height(const nlohmann::json & param);
nlohmann::json jsonrpc_get_tx_by_txid(const nlohmann::json & param);
nlohmann::json jsonrpc_get_tx_by_txid_and_nodeid(const nlohmann::json & param);
nlohmann::json jsonrpc_create_tx_message(const nlohmann::json & param);
nlohmann::json jsonrpc_send_tx(const nlohmann::json & param);
nlohmann::json jsonrpc_send_multi_tx(const nlohmann::json & param);
nlohmann::json jsonrpc_get_avg_fee(const nlohmann::json & param);
nlohmann::json jsonrpc_generate_wallet(const nlohmann::json & param);
nlohmann::json jsonrpc_generate_sign(const nlohmann::json & param);
nlohmann::json jsonrpc_get_pending_transaction(const nlohmann::json & param);
nlohmann::json jsonrpc_get_failure_transaction(const nlohmann::json & param);
nlohmann::json jsonrpc_get_block_info_list(const nlohmann::json & param);
nlohmann::json jsonrpc_confirm_transaction(const nlohmann::json & param);
nlohmann::json jsonrpc_deploy_contract(const nlohmann::json & param);
nlohmann::json jsonrpc_execute_contract(const nlohmann::json & param);
nlohmann::json jsonrpc_execute_contract_rpc(const nlohmann::json & param);
nlohmann::json jsonrpc_get_db_prefix_key(const nlohmann::json & param);
nlohmann::json jsonrpc_get_contract_data(const nlohmann::json & param);
nlohmann::json jsonrpc_get_pledge_redeem_tx_by_addr(const nlohmann::json & param);
nlohmann::json jsonrpc_get_sign_tx_by_addr_and_height(const nlohmann::json & param);
nlohmann::json jsonrpc_get_tx_by_addr_and_height(const nlohmann::json & param);
nlohmann::json jsonrpc_get_utxo(const nlohmann::json & param);
