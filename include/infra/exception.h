//
// Created by yian on 2026/5/18.
//

/*
 *错误处理
 *需要解决两个维度的问题：如何传递错误的控制信息，如何记录错误的调试信息
    如果只用返回值，错误控制是传递上去了，但是错误信息没法跟踪
    如果每层调用发生错误都打日志，那么一个完整错误的上下文就会被拆成多条单独的日志
    在多线程环境下，可能会导致不同线程的错误上下文混杂输出，增加调试难度
    因此这里设计方案是采用类似try-catch的主动异常捕获
    在最上层，拥有完整错误上下文的地方，try-开启一个错误上下文
    下层（容器、系统调用）通过errno/返回值向中间层传递错误的控制和调试信息，
    在中间层，如果发生了错误，就hook调试信息，
    上层通过catch/函数返回值主动查看是否发生错误。并通过end获取错误信息。
    这里有两种错误传递控制传递方式：接口返回值和catch，
    二者区别在错误的粒度上
      catch表明自上次try之后是否有发生异常，
      而函数返回值表明本次调用的结果
 *
 */
#ifndef DNSRELAY_EXCEPTION_H

#define DNSRELAY_EXCEPTION_H
/**
 *用于追踪错误信息
  三状态的状态机,线程局部
  close ：未开启错误记录
  open ： 开启错误记录
  err : 开启错误记录并且有错误
  在err状态，catch() = 1
  其他状态 catch() = 0
  状态转移：
  close -> close by end , 重复的调用end会得到""
  close -> open by try  , 用try开启记录
  close -> err by throw ， 如果上层没有显式try,下层throw的信息可能不会被消费
  open  -> open by try ， try是可重入的
  open  -> err by throw ， 用throw记录错误信息
  open  -> close by end ， 没有意义
  err   -> err by throw ， 追加错误的上下文信息
  err   -> close by end , if(catch()) end;
  err   -> open by try 。 这样会导致错误信息丢失。

 *
 */
//开启错误链捕获，可重入
void ex_try();

// 用于在错误传播链上添加错误信息,
void ex_throw(const char* msg_format,...);
//是否发生了错误
int ex_catch();
// 获取上一次错误的调用栈信息,返回只读字符串，重复读返回空串""
const char* ex_end(void);
#endif //DNSRELAY_EXCEPTION_H