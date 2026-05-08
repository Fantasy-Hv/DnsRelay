//
// Created by yian on 2026/5/8.
//

#ifndef DNSRELAY_SERVER_H
#define DNSRELAY_SERVER_H

typedef struct {
    int client_id;
}Session;
#define KEY_CACHE_SIZE "cache_size"
#define VALUE_DEFAULT_CACHE_SIZE 1024
int server_setup();
int server_shutdown();
#endif //DNSRELAY_SERVER_H