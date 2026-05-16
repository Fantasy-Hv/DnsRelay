//
// Created by yian on 2026/5/9.
/*
 *目前想不到简单通用的配置文件解析方法
*/
//
#include <stdlib.h>
#include <string.h>

#include "infra/config.h"
#include "infra/stl.h"

typedef struct {
    char is_cook;
    char* key;
    T value;
}Entry;
/**
 * 获取配置属性，如果属性不存在，不应该修改value内容
 * @param key 键
 * @param value 值
 * @return 是否存在该配置，0为存在，1为不存在
 */
typedef struct {
    // K=char* ,T = Entry*
    HashMap* key_values;
}ConfigSection;

// K=char* ,T=ConfigSection*
static HashMap* configs;
// K = char* ， T=ConfigParser
static HashMap* config_parsers;
Entry* create_entry(const char* key,T value) {
    Entry* entry = malloc(sizeof(Entry));
    entry->is_cook = 0;
    entry->key = malloc(12);
    strcpy(entry->key,key);
    entry->value = value;
    return entry;
}

int simp_compare(T value1,T value2) {
    return value1==value2;
}

ConfigSection* create_section(const char* section) {
    ConfigSection* sec = malloc(sizeof(ConfigSection));
    sec->key_values = hash_map_create(hash_str,simp_compare);
    return sec;
}

void config_register_parser(const char* section,ConfigParser parser) {
    char sec[16];strcpy(sec,section);
    hash_map_put(config_parsers,sec,parser);
}

int config_init() {
    config_parsers = hash_map_create(hash_str,simp_compare);
    configs = hash_map_create(hash_str,simp_compare);
    return !(configs&&config_parsers);
}
int config_get(const char* section,const char *key,void* value) {
    if (!value)return 1;
    char k[16]; strcpy(k,key);
    char sk[16]; strcpy(sk,section);
    ConfigSection* sec = NULL;
    if (hash_map_get(configs,k,(T*)&sec))
        return 1;
    Entry* entry = NULL;
    if (hash_map_get(sec->key_values,k,(T*)&entry))
        return 1;
    if (!entry->is_cook) { //延迟解析
        ConfigParser parser ;
        if (!hash_map_get(config_parsers,sk,(T*)&parser)) {
            T old_v = entry->value;
            entry->value = parser(k,entry->value); //那旧的value怎么办呢？free
            free(old_v);
            entry->is_cook=1;
        }
    }
    *(T*)value =entry->value;
    return 0;
}

int config_set(const char* section,const char *key,const void* value) {
    ConfigSection* sec = NULL;
    char k[16]; strcpy(k,key);
    char sk[16]; strcpy(sk,section);
    hash_map_get(configs,k,(T*)&sec);
    if (!sec) {
        sec = create_section(section);
        hash_map_put(configs,sk,sec);
    }
    T data = malloc(sizeof(*value));
    memcpy(data,value,sizeof(*value));
    hash_map_put(sec->key_values,k,data);
    return 0;
}

/**
 * 加载配置文件
 * @return
 */
int config_load() {
    return 0;
}