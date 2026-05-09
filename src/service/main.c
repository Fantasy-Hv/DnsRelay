#include <stdio.h>
#include "dns/config.h"
#include "dns/server.h"
#include "infra/logger.h"
/**
 *
 * @brief 程序入口
 * @return
 */
int main(int argc,char* argv[]) {
    load_config();
    //todo 解析命令行参数
    //todo 注入命令行参数到配置层
    log_init();
    server_start();
    return 0;
}
