//
// Created by yian on 2026/5/9.
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
    T value;
}ConfigValue;

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
    // K=char* ,T = ConfigValue*
    HashMap* entries;
}ConfigSection;

// K=char* ,T=ConfigSection*
static HashMap* configs_sections;
// K = char* ， T=ConfigParser
static HashMap* config_parsers;
// K = char* ， T=ConfigCleaner
static HashMap* config_cleaners;
int func_compare(T a,T b) {
    return a==b;
}
// key最长32字节
#define KEY_SIZE 32
#define VALUE_STR_SIZE 128
ConfigValue* entry_create() {
    ConfigValue* entry = malloc(sizeof(ConfigValue));
    entry->is_cook = 0;
    return entry;
}
void entry_free(ConfigValue*entry) {
    free(entry);
}


ConfigSection* create_section() {
    ConfigSection* sec = malloc(sizeof(ConfigSection));
    sec->entries = hash_map_create(hash_func_str,compare_cstr);
    return sec;
}

void config_register_parser(const char* section,ConfigParser parser) {
    char sec[KEY_SIZE];strcpy(sec,section);
    hash_map_put(config_parsers,sec,parser);
}

void config_register_cleaner(const char* section,ConfigCleaner cleaner) {
    char sec[KEY_SIZE];strcpy(sec,section);
    hash_map_put(config_cleaners,sec,cleaner);
}

/**
 * 初始化配置容器
 * @return
 */
int config_init() {
    config_parsers = hash_map_create(hash_func_str,compare_cstr);
    config_cleaners = hash_map_create(hash_func_str,compare_cstr);
    configs_sections = hash_map_create(hash_func_str,compare_cstr);
    return !(configs_sections&&config_parsers&&config_cleaners);
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
    if (hash_map_get(configs_sections,sk,(T*)&sec))
        return 1;
    // 获取配置项
    ConfigValue* entry = NULL;
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
            ex_throw("when config_get:[%s,%s]", section, key);
            return -1;
        }
    }
    *value = entry->value;
    return 0;
}

int config_set(const char* section,const char *key,T value) {
    // 配置节
    ConfigSection* sec = NULL;
    if (hash_map_get(configs_sections,(T)section,(T*)&sec)) { // 没有
        sec = create_section();
        hash_map_put(configs_sections,strdup(section),sec);
    }

    ConfigValue* config_value;
    // 如果有旧的配置值，需要清理
    if (!hash_map_get(sec->entries,(K)key,(T*)&config_value)&&config_value) {
        if (config_value->is_cook) {
            ConfigCleaner cleaner;
            if (!hash_map_get(config_cleaners,(K)section,(T*)&cleaner))
                cleaner(key,config_value->value); // 如果没有，1.该配置不需要清理内存所以上层不注册，2.该配置需要清理但上层没注册，不是本层的问题
        }
        else free(config_value->value);
    }
    else config_value = entry_create();

    config_value->is_cook = 1;
    config_value->value = value;
    // put 是幂等的
    K new_key = strdup(key);
    if (hash_map_put(sec->entries,new_key,config_value)) // 如果原来已经存有一份相等的key
        free(new_key);
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
    // 配置节
    ConfigSection* sec = NULL;
    if (hash_map_get(configs_sections,(T)section,(T*)&sec)) {
        sec = create_section();
        hash_map_put(configs_sections,strdup(section),sec); // 显然里面没有key的内存副本
    }
    // 配置值
    ConfigValue* config_value;
    if (!hash_map_get(sec->entries,(K)key,(T*)&config_value)) {  // 如果有配置值了，
        // 释放旧数据
        ConfigCleaner cleaner;
        if (config_value->is_cook) {
            if (!hash_map_get(config_cleaners,(K)section,(T*)&cleaner))
                cleaner(key,config_value->value);
            // 没有析构函数的话，说明是基本类型，不用释放，直接覆盖就行
        }
        else free(config_value->value);
    }  // 没有配置值，新建一项
    else config_value = entry_create();
    config_value->is_cook = 0;
    config_value->value = strdup(value);

    K new_key = strdup(key);
    if (hash_map_put(sec->entries,new_key,config_value)) // 如果原来已经存有一份相等的key
        free(new_key);
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
            cur_value_str[token_len]='\0';
            //put
            config_inject(cur_section_str,cur_key_str,cur_value_str);
        }
    }
    fclose(fd);
    return 0;
}
