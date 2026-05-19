//
// Created by yian on 2026/5/9.
/*
 *目前想不到简单通用的配置文件解析方法
*/
//
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "infra/config.h"
#include "infra/stl.h"
#include <stdio.h>

#include "infra/exception.h"

typedef struct {
    char is_cook;
    char* key;
    T value;
}Entry;

/**
 *配置文件格式
 *[seciton]
 * key = value
 * # comment
 */


/**
 * 获取配置属性，如果属性不存在，不应该修改value内容
 * @param key 键
 * @param value 值
 * @return 是否存在该配置，0为存在，1为不存在
 */
typedef struct {
    // K=char* ,T = Entry*
    HashMap* entries;
}ConfigSection;

// K=char* ,T=ConfigSection*
static HashMap* configs;
// K = char* ， T=ConfigParser
static HashMap* config_parsers;
int func_compare(T a,T b) {
    return a==b;
}
// key最长32字节
#define KEY_SIZE 32
#define VALUE_STR_SIZE 128
Entry* entry_create(const char* key,T value) {
    Entry* entry = malloc(sizeof(Entry));
    entry->is_cook = 0;
    entry->key = malloc(KEY_SIZE);
    strcpy(entry->key,key);
    entry->value = value;
    return entry;
}
void entry_free(Entry*entry) {
    free(entry->key);
    free(entry);
}
int entry_compare(T value1,T value2) {
    Entry* e1 = value1;
    Entry * e2 = value2;
    if (strcmp(e1->key,e2->key))
        return 0;
    return *e1->key-*e2->key; // 比较首字符
}

ConfigSection* create_section() {
    ConfigSection* sec = malloc(sizeof(ConfigSection));
    sec->entries = hash_map_create(hash_str,entry_compare);
    return sec;
}

void config_register_parser(const char* section,ConfigParser parser) {
    char sec[KEY_SIZE];strcpy(sec,section);
    hash_map_put(config_parsers,sec,parser);
}


/**
 * 初始化配置容器
 * @return
 */
int config_init() {
    config_parsers = hash_map_create(hash_str,func_compare);
    configs = hash_map_create(hash_str,entry_compare);
    return !(configs&&config_parsers);
}
int config_get(const char* section,const char *key,T* value) {
    if (!value) {
        ex_throw("config_get:param value null");
        return -1;
    }
    char k[KEY_SIZE]; strcpy(k,key);
    char sk[KEY_SIZE]; strcpy(sk,section);
    // 获取对应的配置节
    ConfigSection* sec = NULL;
    if (hash_map_get(configs,k,(T*)&sec))
        return 1;
    // 获取配置项
    Entry* entry = NULL;
    if (hash_map_get(sec->entries,k,(T*)&entry))
        return 1;
    // 对配置值延迟解析
    ConfigParser parser ;
    if (!entry->is_cook&&!hash_map_get(config_parsers,sk,(T*)&parser)) { // 存储的是直接从配置文件读取的字符串，需要解析
        T tmp;
        if (!parser(k, entry->value, &tmp)) {
            //解析成功
            free(entry->value); // 释放旧的字符串
            entry->value = tmp; // 改为解析后的值
            entry->is_cook = 1;
        }
        //解析出错
        else {
            ex_throw("config_get:[%s,%s]", section, key);
            return -1;
        }
    }
    *value = entry->value;
    return 0;
}

int config_set(const char* section,const char *key,T value) {
    ConfigSection* sec = NULL;
    char k[KEY_SIZE]; strcpy(k,key);
    char sk[KEY_SIZE]; strcpy(sk,section);
    hash_map_get(configs,k,(T*)&sec);
    if (!sec) {
        sec = create_section();
        hash_map_put(configs,sk,sec);
    }

    Entry* entry;
    // 如果有配置值并且是原始字符串
    if (!hash_map_get(sec->entries,(K)key,(T*)&entry)&&!entry->is_cook)
            free(entry->value);
    entry->is_cook = 1;
    hash_map_put(sec->entries,k,value);
    return 0;
}
/**
 *以原始字符串形式设置配置值,和config_set的唯一区别就是不设置cook字段。
* @param section 配置项所属的节
* @param key 键字符串
* @param value 指向值的指针
* @return
*/
int config_inject(const char* section,const char *key,const char* value) {
    ConfigSection* sec = NULL;
    char k[KEY_SIZE]; strcpy(k,key);
    char sk[KEY_SIZE]; strcpy(sk,section);
    hash_map_get(configs,k,(T*)&sec);
    if (!sec) {
        sec = create_section();
        hash_map_put(configs,sk,sec);
    }

    Entry* entry;
    if (!hash_map_get(sec->entries,(K)key,(T*)&entry)) {  // 如果有配置值了，
        // 释放旧数据
        if (!entry->is_cook)
            free(entry->value); // 释放旧的原始字符串
    }
    hash_map_put(sec->entries,k,strdup(value));
    return 0;
}
/**
 * 加载配置文件
 * @return 0-加载成功，-1加载失败
 */
int config_load_file(const char * filepath) {
    FILE* fd = fopen(filepath,"r");
    if (fd==NULL)return -1;
    /**状态机
   * 读一行 ：
   *      注释——跳过
   *      节标题——创建节，设置节上下文
   *      键值对——放入节的map中
   */
    char line[256];
    char cur_section_str[KEY_SIZE] = {'\0'};
    char cur_key_str[KEY_SIZE] = {'\0'};
    char cur_value_str[VALUE_STR_SIZE] = {'\0'};

    while (fgets(line, sizeof(line), fd)) {
        int token_len = 0;
        char* nc = line; //line末尾有\n\0
        while (isspace((unsigned char)*nc))nc++; //跳过空白符

        if (*nc=='#') continue;
        if (*nc=='[') {
            nc++;
            while (isspace((unsigned char)*nc))nc++; //跳过空白符
            while (nc[token_len]!=']'&&!isspace((unsigned char)nc[token_len])) token_len++;
            if (nc[token_len]=='\0') break; // 节必须以[ 开头， 以 ] 结尾
            // len为节的字符个数
            memcpy(cur_section_str,nc,token_len);
            cur_section_str[token_len]='\0';
        }
        else { // k-v对
            //读key
            while (nc[token_len]!='='&&!isspace((unsigned char)nc[token_len]))token_len++;
            if (nc[token_len]=='\0') break;
            memcpy(cur_key_str,nc,token_len);
            cur_key_str[token_len] = '\0';
            nc+=token_len+1;
            //读value
            token_len = 0;
            while (!isspace((unsigned char)nc[token_len])&&nc[token_len]!='\0') token_len++;
            if (token_len==0) break;
            memcpy(cur_value_str,nc,token_len);
            //put
            config_inject(cur_section_str,cur_key_str,cur_value_str);
        }
    }
    fclose(fd);
    return 0;
}
