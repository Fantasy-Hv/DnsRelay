//
// Created by yian on 2026/5/8.
//

#ifndef DNSRELAY_CONFIG_H
#define DNSRELAY_CONFIG_H

/**
 * 获取配置属性，如果属性不存在，不应该修改value内容
 * @param key 键
 * @param value 值
 * @return 是否存在该配置，0为存在，1为不存在
 */
int config_get(const char *key,void* value);

int config_set(const char *key,void* value);
/**
 * 加载配置文件
 * @return
 */
int config_load();
#endif //DNSRELAY_CONFIG_H