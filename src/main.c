#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/infra/config.h"
#include "../include/server/server.h"
#include "dns/cache.h"
#include "dns/id.h"
#include "infra/logger.h"
#include "server/session.h"
char * config_file = "./dnsrelay.txt";
void print_help() {
    printf("usage:\n");
    printf("-c <config_file_path>\n");
    printf("-d : run in debug mode\n");
    printf("-dd : run in trace mode\n");
    printf("<ip list> : name server ips, separated by space\n");
}
int parse_param(int argc,char* argv[]) {
    int i=1;
    char ips[1024] = {0};
    while (i<argc) {
        if (strcmp(argv[i],"-d")) {
            config_set(LOG_SECTION,KEY_LOG_LEVEL,(T)DEBUG);
            i++;
        }else if (strcmp(argv[i],"-dd")) {
            config_set(LOG_SECTION,KEY_LOG_LEVEL,(T)TRACE);
            i++;
        } else if (strcmp(argv[i],"-c")) {
            // 那么下一个字符串是配置文件位置
            if (++i<argc)
                config_file = argv[i++];
            else return 1;
        } else { //什么标记都不带
            // dns域名服务器的ip
            strcat(ips,",");
            strcat(ips,argv[i++]);
        }
    }
    if (*ips!=0)
        config_inject(SERV_SECTION,KEY_UPSTREAMS,ips); // 配置会自己拷贝一份字符串，所以ips用栈存储
    return 0;
}


/**
 * @param -c <path> : 指定配置文件路径
 * 日志级别：
 *   -d debug
 *   -dd trace
 * @param -n : 域名服务器，后接域名服务器ip
 *
 * @brief 程序入口,负责解析命令行参数，以及各个模块的初始化
 * @return
 */
int main(int argc,char* argv[]) {
    if (config_init()) {
        printf("fatal : config_init failed\n");
        return 1;
    }
    //解析命令行参数
    //注入命令行参数到配置层
    if (parse_param(argc,argv)) {
        print_help();
        return 1;
    }
    if (config_load_file(config_file)) {
        printf("warn: config file load err");
    }
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
