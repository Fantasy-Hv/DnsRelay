//
// Created by yian on 2026/5/8.
//

#ifndef DNSRELAY_CONFIG_H
#define DNSRELAY_CONFIG_H
//
/**
 * 获取配置属性，如果属性不存在，不应该修改value内容
 * @param key 键
 * @param value 值
 * @return 是否存在该配置，0为存在，1为不存在
 */
int config_get(const char *key,void* value);

int config_set(const char *key,void* value);
/**
 * 加载配置文件，todo,要不要实现配置文件，还是个待定的问题，如果配置少的话完全可以通过命令行参数获取，
 * 而且有些数值也可以用默认值，
 * @return
 */
int config_load();
/**以这条语句为例
LinkedList *upstreams = linked_list_create();
config_get(KEY_UPSTREAMS,upstreams);
配置层怎么解析配置？怎么知道要解析成链表？
 一个方案是让需要配置的模块都提供一个本模块的配置解析函数，config模块在运行时扫描这些函数，
 建立一个<section,parse_func>的表，
 然后读取配置文件，在对应的节，调用对应的解析函数。
*/
#endif //DNSRELAY_CONFIG_H