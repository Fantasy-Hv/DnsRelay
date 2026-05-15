#include <stdio.h>
#include <bits/in.h>

#include "../include/infra/config.h"
#include "../include/server/server.h"
#include "dns/id.h"
#include "infra/logger.h"
#include "server/session.h"
/**
 * @param -c <path> : 指定配置文件路径
 * @param -d : 调试模式
 *
 * @brief 程序入口,负责解析命令行参数，以及各个模块的初始化
 * @return
 */
int main(int argc,char* argv[]) {
    if (config_init()) {
        printf("fatal : config_init failed\n");
        return 1;
    }
    //todo 解析命令行参数
    //todo 注入命令行参数到配置层
    if (!logger_init()) {
        printf("error : logger_init failed\n");
    }
    if (!id_pool_init()) {
        printf("fatal : id_pool_init failed\n");
        return 1;
    }
    if (!dns_cache_init()) {
        printf("fatal : dns_cache_init failed\n");
        return 1;
    }
    if (!session_factory_init()) {
        printf("fatal : session_factory_init failed\n");
        return 1;
    }

    return server_start();
}
