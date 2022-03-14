#include "main.h"

#include <regex>
#include <iostream>

#include "include/all.h"
#include "include/logging.h"
#include "common/devicepwd.h"
#include "common/setting_wizard.h"
#include "utils/singleton.h"
#include "utils/time_util.h"
#include "net/net_api.h"
#include "ca/ca_interface.h"
#include "ca/ca_hexcode.h"
#include "ca/ca.h"
#include "ca/ca_global.h"
#include "ca/ca_AdvancedMenu.h"

void menu()
{
	ca_print_basic_info();
	while (true)
	{
		std::cout << std::endl << std::endl;
		std::cout << "1.Transaction" << std::endl;
		std::cout << "2.Pledge" << std::endl;
		std::cout << "3.Redeem pledge" << std::endl;
		std::cout << "4.Change password" << std::endl;
		std::cout << "5.Set config" << std::endl;
		std::cout << "6.Set fee" << std::endl;
		std::cout << "7.Export private key" << std::endl;
		std::cout << "8.Print pending transaction in cache" << std::endl;
		std::cout << "0.Exit" << std::endl;

		std::string strKey;
		std::cout << "Please input your choice: "<< std::endl;
		std::cin >> strKey;	    
		std::regex pattern("[0-9]");
		if(!std::regex_match(strKey, pattern))
        {
            std::cout << "Invalid input." << std::endl;
            continue;
        }
        int key = std::stoi(strKey);

		switch (key)
		{			
			case 0:
				std::cout << "Exiting, bye!" << std::endl;
				ca_cleanup();
				exit(0);
				return;				
			case 1:
				handle_transaction();
				break;
			case 2:
				handle_pledge();
				break;
			case 3:
				handle_redeem_pledge();
				break;
			case 4:
				handle_change_password();
				break;
			case 5:
				handle_set_config();
				break;
			case 6:
				handle_set_fee();
				break;
			case 7:
				handle_export_private_key();
				break;			
			case 8:
				handle_print_pending_transaction_in_cache();
				break;
			case 9:
                menu_advanced();
                break;
		}

		sleep(1);
	}
}
	
bool init()
{
	if (! InitConfig() )
	{
		return false;
	}


	std::string logLevel = Singleton<Config>::get_instance()->GetLogLevel();
	if (logLevel.empty())
	{
		logLevel = "OFF";
	}

	std::string logPath = Singleton<Config>::get_instance()->GetLogPath();
	if (logPath.empty())
	{
		logPath = "./logs";
	}
	auto logConsoleOut = Singleton<Config>::get_instance()->GetLogConsoleOut();
	if (logPath.empty())
	{
		logPath = "false";
	}
	if(!LogInit("./logs", logConsoleOut, logLevel))
	{
		std::cout << "log init fail, exit" << std::endl;
	}



	if(!InitDevicePwd())
	{
		return false;
	}
	
	return net_com::net_init() && ca_init();
}

bool InitConfig()
{
    Singleton<SettingWizard>::get_instance()->Init(Singleton<Config>::get_instance());
	return true;
}

bool InitDevicePwd()
{
	//配置文件初始化
	if (false == Singleton<DevicePwd>::get_instance()->InitPWDFile())
	{
		std::cout << "init Config failed" << std::endl;
		cout<<"init Config failed get_instance()->InitPWDFile())"<<std::endl;
		return false;
	}
	if (false == Singleton<DevicePwd>::get_instance()->UpdateDevPwdConfig())
	{
		std::cout << "update devpwd config failed" << std::endl;
		cout<<"update devpwd config get_instance()->UpdateDevPwdConfig())"<<std::endl;
		return false;
	}
	return true;
}
