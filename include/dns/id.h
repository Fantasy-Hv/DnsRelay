
#ifndef DNSRELAY_ID_H
#define DNSRELAY_ID_H
#include <stdint.h>

/**
 *申请分配id,
 *@return 0-分配成功 1-分配失败
 */
int id_pool_init();
int id_alloc(uint16_t *);
void id_free(uint16_t id);
#endif //DNSRELAY_ID_H

