//
//  MachThreadBacktrace.h
//  SwiftCrashReport
//
//  Created by yingdong.guo on 16/7/15.
//  Copyright © 2016年 pintec.com. All rights reserved.
//

#ifndef MachThreadBacktrace_h
#define MachThreadBacktrace_h

#include <mach/mach.h>

/**
 *  fill a backtrace call stack array of given thread
 *
 *  @param thread   mach thread for tracing
 *  @param stack    caller space for saving stack trace info
 *  @param maxCount max stack array count
 *
 *  @return call stack address array
 */
int backtraceForMachThread(thread_t thread, void** stack, int maxCount);

#endif /* MachThreadBacktrace_h */