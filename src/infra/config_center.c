//
// Created by yian on 2026/5/9.
//
#include "infra/config.h"
/**
 * 获取配置属性，如果属性不存在，不应该修改value内容
 * @param key 键
 * @param value 值
 * @return 是否存在该配置，0为存在，1为不存在
 */
int config_get(const char *key,void* value) {
    return 0;
}

int config_set(const char *key,void* value) {
    return 0;
}
/**
 * 加载配置文件
 * @return
 */
int config_load() {
    return 0;
}