#include "string.h"
#include "unistd.h"

#include <iostream>

#include "main/main.h"
#include "utils/singleton.h"
#include "utils/time_util.h"
#include "common/version.h"
#include "common/config.h"
#include "ca/ca.h"
#include "ca/ca_global.h"
#include "ca/ca_transaction.h"

int main(int argc, char* argv[])
{
	if (argc >= 6)
	{
		std::cout << "Command error!!!" << std::endl;
		return 0;
	}

	bool showMenu = false;
	float signFee = 0;
	float packFee = 0;
	for (int i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "--help") == 0)
		{
			std::cout << argv[0] << ": --help version:" << getEbpcVersion() << " \n -m show menu\n -s value, signature fee\n -p value, package fee" << std::endl;
			exit(0);
			return 0;
		}
		else if (strcmp(argv[i], "-s") == 0) // strcasecmp()
		{
			if (i + 1 < argc)
			{
				i++;
				signFee = atof(argv[i]);
				std::cout << "Set signature fee: " << signFee << std::endl;
			}
		}
		else if (strcmp(argv[i], "-p") == 0)
		{
			if (i + 1 < argc)
			{
				i++;
				packFee = atof(argv[i]);
				std::cout << "Set package fee: " << packFee << std::endl;
			}
		}
		else if (strcmp(argv[i], "-m") == 0)
		{
			showMenu = true;
			std::cout << "Show menu." << std::endl;
		}
	}

	if (!init())
	{
		return 1;
	}

	InitAccount(&g_AccountInfo, "./cert");
    srand(Singleton<TimeUtil>::get_instance()->getlocalTimestamp());

	if (signFee > 0.0001)
	{
		set_device_signature_fee(signFee);
	}
	if (packFee > 0.0001)
	{
		set_device_package_fee(packFee);
	}
	Singleton<Config>::get_instance()->Removefile();
	Singleton<Config>::get_instance()->RenameConfig();
	if (showMenu)
	{
		menu();
	}
	while (true)
	{
		sleep(9999);
	}
	return 0;
}
