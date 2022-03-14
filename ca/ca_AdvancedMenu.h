#ifndef __CA_ADVANCEDMENU_H_
#define __CA_ADVANCEDMENU_H_

#include <cstdint>

#include "db/db_api.h"


#pragma region 一级菜单
//root菜单
void menu_advanced();
#pragma endregion

#pragma region 二级菜单
//ca菜单
void menu_ca();
void gen_key();
void in_private_key();
void out_private_key();
void get_account_balance();
void create_coinbase_tx();
void print_all_block();
void rollback();
void synchronize();
void CompPublicNodeHash();

//net菜单
void menu_net();
void send_message_to_user();
void show_my_k_bucket();
void kick_out_node();
void test_echo();
void print_req_and_ack();
void print_peer_node();
#pragma endregion

#pragma region 三级菜单
//blockinfo菜单
void menu_blockinfo();
void get_all_tx_number(uint64_t& top, DBReader& reader);
void get_tx_block_info(uint64_t& top);
void view_block_award(uint64_t& top, DBReader& reader);
void view_total_money(uint64_t& top, DBReader& reader);
void get_device_password();
void set_device_password();
void get_device_prikey();


//test菜单
void menu_test();
void gen_mnemonic();
void get_hashlist();
void get_balance_by_utxo();
void set_signfee();
void get_hash_height_mac();
void imitate_create_tx_struct();
void get_pledge();
void multi_tx();
void get_tx_list();
void get_tx_info();
void get_block_list();
void get_block_info();
void get_all_pledge_addr();
void get_all_block_num_in_5min();
void set_packagefee();
void get_all_pubnode_packagefee();
void auto_tx();
void get_former100_block_award();
void get_blockinfo_by_txhash();
void get_failure_tx_list_info();
void get_nodeheight_isvalid();
void get_pledge_redeem();
void get_qrcode();
void get_node_cache();
void get_block_cache_txcount_and_award_summary();// abort
void get_block_cache_hash();
void get_abnormal_list();
void get_sign_and_award();
void get_block_num_and_block_award_total();
void get_utxo();



//node菜单
void menu_node();
void get_a_node_info();
void get_all_node_info();

#pragma endregion
#endif
