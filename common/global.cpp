/*
 * @Author: your name
 * @Date: 2021-03-27 14:46:07
 * @LastEditTime: 2021-03-27 14:46:20
 * @LastEditors: Please set LastEditors
 * @Description: In User Settings Edit
 * @FilePath: \ebpc\common\global.cpp
 */

#include "global.h"

#ifdef PRIMARYCHAIN
int g_testflag = 0; //主网版本
#elif TESTCHAIN
int g_testflag = 1; //测试网版本
#else
int g_testflag = 2; //开发网版本
#endif
