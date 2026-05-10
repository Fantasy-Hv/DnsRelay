//
// Created by yian on 2026/5/8.
//
#ifdef __linux__
#include <pthread.h>
#include "infra/thread.h"
#include "infra/sys.h"
/**
 * 开启一个新线程
  @param thread 如果成功，返回线程引用
  @param run 线程要执行的函数
  @param run_arg run 函数的参数
 *@return 成功返回 0；失败返回错误码
 */
int thread_create(Thread* thread,void *(*run)(void *),void* run_arg) {
 return 0;
}
/**
 * 当前线程休眠指定毫秒
 */
void thread_sleep_ms(ms sleep_time) {

}
#endif