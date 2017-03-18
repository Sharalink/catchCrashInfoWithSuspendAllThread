//
//  MachExceptionHandler.c
//  SwiftCrashReport
//
//  Created by yingdong.guo on 16/7/8.
//  Copyright © 2016年 pintec.com. All rights reserved.
//

#include "MachExceptionHandler.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <mach/mach.h>
#include <mach/exc.h>
#include <sys/sysctl.h>
#include <pthread.h>
#include <unistd.h>

// lldb also use mach exception port for debugging, therefore this implementation may affect debugging.
// the crash handler installation will not continue if debugger being presented.
//
// enable this macro will force install mach exception handler even being debugged.
// for tracing use only, may break the debugger
//#define __MACH_EXCEPTION_CONTINUE_WITH_DEBUGGER__ 1

#ifdef __MACH_EXCEPTION_HANDLER_DEBUG_LOG__
#define _MEDebugLog(msg, ...) printf(msg, ##__VA_ARGS__)
#else
#define _MEDebugLog(msg, ...)
#endif

#pragma mark - defs

// copied from <mach/exc.h>
typedef struct {
    mach_msg_header_t Head;
    /* start of the kernel processed data */
    mach_msg_body_t msgh_body;
    mach_msg_port_descriptor_t thread;
    mach_msg_port_descriptor_t task;
    /* end of the kernel processed data */
    NDR_record_t NDR;
    exception_type_t exception;
    mach_msg_type_number_t codeCnt;
    integer_t code[2];
    int flavor;
    mach_msg_type_number_t old_stateCnt;
    natural_t old_state[224];
} MachExceptionMessage;

typedef struct {
    mach_msg_header_t Head;
    NDR_record_t NDR;
    kern_return_t RetCode;
} MachExceptionReply;

#pragma mark - data

static struct {
    mach_msg_type_number_t masksCnt;
    exception_mask_t masks[EXC_TYPES_COUNT];
    exception_handler_t handlers[EXC_TYPES_COUNT];
    exception_behavior_t behaviors[EXC_TYPES_COUNT];
    thread_state_flavor_t flavors[EXC_TYPES_COUNT];
} g_originalExceptionPorts;

static volatile bool g_exceptionHandlerInstalled = false;
static mach_port_t g_exceptionPort = 0;

static char const *machExceptionHandlerThreadName = "MachExceptionHandler";

static pthread_t g_machExceptionHandlerThread = 0;
static volatile bool g_isHandlingMachException = false;

static MachExceptionMessageHandler g_machExceptionMessageHandler;

#pragma mark - error handling thread entry

static void *machExceptionHandlerEntry(void *_) {
    pthread_setname_np(machExceptionHandlerThreadName);
    
    MachExceptionMessage *msg = malloc(sizeof(MachExceptionMessage));
    
    g_isHandlingMachException = true;
    kern_return_t kret;
    while(1) {
        kret = mach_msg((mach_msg_header_t *)msg,
                                      MACH_RCV_MSG,
                                      0,
                                      sizeof(MachExceptionMessage),
                                      g_exceptionPort,
                                      0,
                                      MACH_PORT_NULL);
        
        if (kret == KERN_SUCCESS) break;
    }
    
    _MEDebugLog("catched mach exception\n");
    
    mach_msg_type_number_t threadCount;
    thread_act_array_t threadList;
    kret = task_threads(mach_task_self(), &threadList, &threadCount);
    if (kret != KERN_SUCCESS) {
        _MEDebugLog("get thread list failed: %s\n", mach_error_string(kret));
        //goto _finishHandling;
        // fail to enumerate thread list, running thread's stack may not be accurate.
    }
    // suspend all thread, then get crashing stack for accuracy (for those formatters logging all thread's stack)
    thread_t selfThread = mach_thread_self();
    for (int i = 0; i < threadCount; ++i) {
        if (threadList[i] != selfThread) {
            thread_suspend(threadList[i]);
        }
    }

    MachExceptionContext *context = malloc(sizeof(MachExceptionContext));
    context->code = msg->code[0];
    context->subCode = msg->code[1];
    context->thread = msg->thread.name;
    context->type = msg->exception;
    
    g_machExceptionMessageHandler(context);
    
    // awake suspended thread
    for (int i = 0; i < threadCount; ++i) {
        thread_resume(threadList[i]);
    }

    g_isHandlingMachException = false;
    machExceptionHandlerUninstall();
    
    // mark message as "not handled", message will resend to system handler
    MachExceptionReply *replyMsg = malloc(sizeof(MachExceptionReply));
    replyMsg->Head = msg->Head;
    replyMsg->NDR = msg->NDR;
    replyMsg->RetCode = KERN_FAILURE;
    
    free(msg);
    
    mach_msg((mach_msg_header_t *)replyMsg,
             MACH_SEND_MSG,
             sizeof(MachExceptionReply),
             0,
             MACH_PORT_NULL,
             0,
             MACH_PORT_NULL);
    return NULL;
}

#pragma mark - private functions

static bool debuggerPresent() {
    struct kinfo_proc procInfo;
    size_t pInfoSize = sizeof(procInfo);
    int mib[] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid() };
    
    int ret = sysctl(mib, sizeof(mib) / sizeof(*mib), &procInfo, &pInfoSize, NULL, 0);
    if(ret != 0)
    {
        _MEDebugLog("sysctl: %s", strerror(errno));
        return false;
    }
    
    return (procInfo.kp_proc.p_flag & P_TRACED) != 0;
}

#pragma mark - export functions

void machExceptionHandlerInstall(MachExceptionMessageHandler handler) {
#ifndef __MACH_EXCEPTION_CONTINUE_WITH_DEBUGGER__
    if (debuggerPresent()) {
        printf("Process is being debugged. Exception handler will not be installed.\n");
        return;
    }
#endif
    
    if (g_exceptionHandlerInstalled) {
        return;
    }
    g_exceptionHandlerInstalled = true;
    
    _MEDebugLog("install mach exception handler\n");
    g_machExceptionMessageHandler = handler;
    
    task_t selfTask = mach_task_self();
    exception_mask_t exceptionToCatch = EXC_MASK_BAD_ACCESS
                                      | EXC_MASK_BAD_INSTRUCTION
                                      | EXC_MASK_ARITHMETIC
#ifndef __MACH_EXCEPTION_CONTINUE_WITH_DEBUGGER__
                                      // 真机（ARM64）是靠 `brk #1` 指令断下的
                                      // 如果在调试模式中拦下这个异常，断点就会失效
                                      | EXC_MASK_BREAKPOINT
#endif
                                      | EXC_MASK_SOFTWARE;
    
    kern_return_t kret;
    
    // 1. 保存原来的异常处理器
    kret = task_get_exception_ports(selfTask,
                                    exceptionToCatch,
                                    g_originalExceptionPorts.masks,
                                    &g_originalExceptionPorts.masksCnt,
                                    g_originalExceptionPorts.handlers,
                                    g_originalExceptionPorts.behaviors,
                                    g_originalExceptionPorts.flavors);
    if (kret != KERN_SUCCESS) {
        _MEDebugLog("get exception ports failed: %s\n", mach_error_string(kret));
        goto _clean;
    }
    
    // 2. 创建异常接收端口
    if (!g_exceptionPort) {
        kret = mach_port_allocate(selfTask, MACH_PORT_RIGHT_RECEIVE, &g_exceptionPort);
        if (kret != KERN_SUCCESS) {
            _MEDebugLog("allocate exception port failed: %s\n", mach_error_string(kret));
            goto _clean;
        }
        
        kret = mach_port_insert_right(selfTask, g_exceptionPort, g_exceptionPort, MACH_MSG_TYPE_MAKE_SEND);
        if (kret != KERN_SUCCESS) {
            _MEDebugLog("set port right failed: %s\n", mach_error_string(kret));
            goto _clean;
        }
    }
    
    // 3. 替换异常处理端口
    kret = task_set_exception_ports(selfTask, exceptionToCatch, g_exceptionPort, EXCEPTION_DEFAULT, THREAD_STATE_NONE);
    if (kret != KERN_SUCCESS) {
        _MEDebugLog("substitute exception port failed: %s\n", mach_error_string(kret));
        goto _clean;
    }
    
    // 4. 启动异常处理线程
    pthread_create(&g_machExceptionHandlerThread, NULL, &machExceptionHandlerEntry, NULL);
    return;
_clean:
    machExceptionHandlerUninstall();
}

void machExceptionHandlerUninstall() {
    _MEDebugLog("uninstall mach exception handler\n");
    
    kern_return_t kret;
    if (g_machExceptionHandlerThread) {
        if (g_isHandlingMachException) {
            thread_t machThread = pthread_mach_thread_np(g_machExceptionHandlerThread);
            kret = thread_terminate(machThread);
            if (kret != KERN_SUCCESS) {
                _MEDebugLog("kill exception thread failed: %s\n", mach_error_string(kret));
            }
        }
        else {
            pthread_cancel(g_machExceptionHandlerThread);
        }
    }
    
    // set back exception handler
    task_t selfTask = mach_task_self();
    if (g_originalExceptionPorts.masksCnt) {
        for (int i = 0; i < g_originalExceptionPorts.masksCnt; ++i) {
            kret = task_set_exception_ports(selfTask,
                                            g_originalExceptionPorts.masks[i],
                                            g_originalExceptionPorts.handlers[i],
                                            g_originalExceptionPorts.behaviors[i],
                                            g_originalExceptionPorts.flavors[i]);
            if (kret != KERN_SUCCESS) {
                _MEDebugLog("set back original exception port failed: %s\n", mach_error_string(kret));
            }
        }
    }
    
    // this breaks original exception handling
    //kret = mach_port_destroy(selfTask, g_exceptionPort);
    
    g_exceptionHandlerInstalled = false;
}