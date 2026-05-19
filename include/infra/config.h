//
// Created by yian on 2026/5/8.
//
/**
 *配置文件格式(只支持ascii字符)：
 #this is comment.
 [section1]
 key1=value2
 key2=value2
 [section2]
  ...
 * config只负责读取原始字符串键值对，不关心配置项具体内容，因此如果上游模块对配置项值的类型有要求
 * 就需要注册配置解析函数，由于c没有反射，config模块在初始化时没办法主动获取各个模块的配置解析函数
 * 因此需要上游模块在自己的初始化方法中调用配置模块的注册函数，把配置解析函数注册到配置系统中
 * 如果不注册，config_get拿到的默认就是配置文件中的原始字符串，或者上次通过config_set设定的值
 */

#ifndef DNSRELAY_CONFIG_H
#define DNSRELAY_CONFIG_H
#include "stl.h"
#ifdef _WIN32
#define CONFIG_FILE_PATH ".\\config.ini"
#else
#define CONFIG_FILE_PATH "./config.ini"
#endif
/**
 * 配置项解析器，读入k-v,生成解析后的值
 * 0解析成功，-1解析失败
 */
typedef int (*ConfigParser)(const char* key,const char* value,T* result);

/**
 * 为某个节注册配置解析器
 * @param section
 * @param parser
 */
void config_register_parser(const char* section,ConfigParser parser);

/**
 * 初始化配置中心状态
 * @return
 */
int config_init();
/**
 * 获取配置属性，如果属性不存在，不应该修改value内容
 * @param key 键
 * @param value 值指针,必须指向一块有效内存,如果配置存在，会将配置的值拷贝到该内存中，否则不会改变内容
 * @return 是否存在该配置，0为存在，1为不存在,-1解析出错
 */
int config_get(const char* section,const char *key,T* value);

/**
 * 以可用形式设置配置值，将来读取时不会解析
 * @param section 配置项所属的节
 * @param key 键字符串
 * @param value 指向值的指针
 * @return
 */
int config_set(const char* section,const char *key,T value);/**
 /**
  *以原始字符串形式设置配置值，将来读取时会进行解析
 * @param section 配置项所属的节
 * @param key 键字符串
 * @param value 指向值的指针
 * @return
 */
int config_inject(const char* section,const char *key,const char* value);
/**
 * 加载配置文件
 * @return
 */
int config_load_file(const char *);

#endif //DNSRELAY_CONFIG_H