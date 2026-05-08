#include <stdio.h>
#include "dns/config.h"
#include "dns/server.h"
/**
 *
 * @brief 程序入口
 * @return
 */
int main(int argc,char* argv[]) {
    load_config();

    server_start();
    return 0;
}
