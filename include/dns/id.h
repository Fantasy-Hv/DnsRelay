//
// Created by yian on 2026/5/10.
//

#ifndef DNSRELAY_ID_H
#define DNSRELAY_ID_H
#include <stdint.h>
/**
 * 现在面临dns转发id的管理问题。id空间[0,65535]
 * 一个id的生命周期：1.未被分发 2.已被分发.
 * 关键约束：id状态的转移只有两种情况 1->2 （分配）,2->1（回收）
 *  那么，哪一层，谁来跟踪、转移id的状态？
 * 一个id副本可能被以下实体持有
 * 1.DnsPack relay_pack
 * 2.Session
 * 这些实体持有relay_id的生命周期：
 * Session：从请求[转发开始]到请求结束
 * DnsPack: 转发包从构造到请求结束
 * 可以看到，relay_id在Session和relay包上的存在时间是同步的,
 * relay_id分配  ——  cook_relay_pack —— session_open
 * relay_id回收 —— session_close —— retry_cache_remove
 * 现在可以回答此前的问题了：id的状态转移由server决定，id的状态记录由本层决定，而session层持有id的拷贝。
    id的申请释放时机由调用者决定，本层只需要维护id状态即可。
*/
int id_alloc(uint16_t *);
void id_free(uint16_t id);
#endif //DNSRELAY_ID_H

