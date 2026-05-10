//
// Created by yian on 2026/5/8.
//
//线程库抽象层，提供简洁方便的线程管理api
#ifndef DNSRELAY_THREAD_H
#define DNSRELAY_THREAD_H
#include "infra/sys.h"
#ifdef __linux__
#include <pthread.h>
typedef pthread_t Thread;
#else
#include <windows.h>
typedef HANDLE Thread;
#endif

/**
 * 开启一个新线程
  @param thread 如果成功，返回线程引用
  @param run 线程要执行的函数
  @param run_arg run 函数的参数
 *@return 成功返回 0；失败返回错误码
 */
int thread_create(Thread* thread,void *(*run)(void *),void* run_arg);

/**
 * 终止当前线程（即调用这个方法的线程），确保你在释放了该释放的内存（.eg释放锁）后再调用
 */
void thread_exit();

/**
 * 停止目标线程todo 是否需要还待定
 */
void thread_shutdown(Thread victim);

/**
 * 当前线程休眠指定毫秒
 */
void thread_sleep_ms(ms sleep_time);
#endif //DNSRELAY_THREAD_H