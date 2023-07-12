/**
* @Function: Handle error signals
*
* @Author  : Cheng Chi
* @Email   : chichengcn@sjtu.edu.cn
*
* Copyright (C) 2023 by Cheng Chi, All rights reserved.
**/
#include "gici/utility/signal_handle.h"

#include <glog/logging.h>

namespace gici {

// Handle broken pipe exception from TCP/IP
static void handlePipe(int sig)
{
  LOG(INFO) << "Received a pipe exception!";
}

// Handle invalid access to storage
static void handleSegv(int sig) 
{
  LOG(FATAL) << "Received a segment fault exception!";
  exit(EXIT_FAILURE);
}

// Initialize all signal handles
/**
   * By: ChatGPT
   * SIGPIPE信号（Broken Pipe）：---- 管道写入异常
   * 当一个进程尝试向一个已经关闭的管道（或者另一端已经关闭的管道）写入数据时，
   * 操作系统会发送 SIGPIPE 信号给该进程。这个信号的发生通常表示管道的写入端出现了异常，
   * 例如读取端已经关闭或者无法读取数据。默认情况下，进程接收到 SIGPIPE 信号后会终止运行。
   * 在处理网络编程或管道通信时，合理地处理 SIGPIPE 信号很重要。
   * 通常的做法是在程序中显式地捕获 SIGPIPE 信号并进行相应处理，
   * 避免程序因为 SIGPIPE 信号的默认行为而异常终止。
   * SIGSEGV信号（Segmentation Fault）：---- 段错误
   * 当一个进程访问非法内存区域时，例如读取或写入一个未分配或无效的内存地址，
   * 操作系统会发送 SIGSEGV 信号给该进程。这个信号的发生通常表示程序出现了段错误，
   * 即访问了不属于它的内存空间。默认情况下，进程接收到 SIGSEGV 信号后会终止运行。
   * SIGSEGV 信号常常是由于编程错误、内存泄漏或指针操作错误引起的。
   * 处理 SIGSEGV 信号的一般方法是通过调试和修复程序中的错误，
   * 确保程序正确地访问内存，并避免导致段错误的情况发生。
  */
extern void initializeSignalHandles()
{
  struct sigaction sa_pipe;
  struct sigaction sa_segv;

  sa_pipe.sa_handler = handlePipe;
  sigemptyset(&sa_pipe.sa_mask);
  sa_pipe.sa_flags = 0;
  sigaction(SIGPIPE, &sa_pipe, NULL);

  sa_segv.sa_handler = handleSegv;
  sigemptyset(&sa_segv.sa_mask);
  sa_segv.sa_flags = 0;
  sigaction(SIGSEGV, &sa_segv, NULL);
}

}