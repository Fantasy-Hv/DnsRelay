#include <stdio.h>
#include <string.h>
#include <infra/exception.h>

#include "../include/infra/config.h"
#include "../include/server/server.h"
#include "dns/cache.h"
#include "dns/id.h"
#include "infra/logger.h"
#include "server/session.h"
char * config_file = "./dnsrelay.ini";
void print_help() {
    printf("usage:\n");
    printf("-c <config_file_path>\n");
    printf("-d : run in debug mode\n");
    printf("-dd : run in trace mode\n");
    printf("<ip_list> : name server ips, separated by space\n");
}
// 获取参数中的配置文件路径
int param_get_config_file(int argc,char* argv[]) {
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-c")) {
            // 那么下一个字符串是配置文件位置
            if (++i < argc) {
                config_file = argv[i];
                return 0;
            }
            break;
        }
    }
    return 1;
}
int param_inject_config(int argc,char* argv[]) {
    char ips[1024] = {0};

    for (int i = 1;i < argc;i++) {


        if (!strcmp(argv[i], "-d"))
            config_set(LOG_SECTION,KEY_LOG_LEVEL, (T) DEBUG);

        else if (!strcmp(argv[i], "-dd"))
            config_set(LOG_SECTION,KEY_LOG_LEVEL, (T) TRACE);

        else if (!strcmp(argv[i], "-c"))
            i++;

        else {
            //什么标记都不带 ,就是dns域名服务器的ip
            strcat(ips, ",");
            strcat(ips, argv[i]);
        }
    }

    if (*ips != '\0')
        config_inject(SERV_SECTION,KEY_UPSTREAMS, ips); // 配置会自己拷贝一份字符串，所以ips用栈存储
    return 0;
}



int main(int argc,char* argv[]) {

    //初始化配置系统
    if (config_init()) {
        printf("[FATAL] config_init failed\n");
        return 1;
    }
    // 加载配置文件
    param_get_config_file(argc, argv);
    if (config_load_file(config_file))
        printf("[WARN] config file load err,use default config\n");

    //解析命令行参数，注入命令行参数到配置层
    param_inject_config(argc, argv);

    if (logger_init()) {
        printf("[ERROR] logger_init failed\n");
        return 1;
    }

    if (id_pool_init()) {
        printf("[FATAL]  id_pool_init failed\n");
        return 1;
    }

    ex_try();
    if (dns_cache_init()) {
        do_log(ERROR,"cache init failed: %s",ex_end());
        return 1;
    }

    ex_try();
    if (dns_resolver_init())
        do_log(ERROR,"dns_resolver init failed : %s",ex_end());

    ex_try();
    if (session_factory_init()) {
        do_log( ERROR, "session_factory_init failed:%s",ex_end());
        return 1;
    }

    return server_start();
}
