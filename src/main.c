#include "../include/infra/config.h"
#include "../include/server/server.h"
#include "dns/protocol.h"
/**
 * @param -c <path> : 指定配置文件路径
 * @param -d : 调试模式
 *
 * @brief 程序入口
 * @return
 */
int main(int argc,char* argv[]) {

    config_load();
    //todo 解析命令行参数
    //todo 注入命令行参数到配置层
    server_start();
    return 0;
}
