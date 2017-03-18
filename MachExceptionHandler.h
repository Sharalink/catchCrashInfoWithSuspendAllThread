//
//  MachExceptionHandler.h
//  SwiftCrashReport
//
//  Created by yingdong.guo on 16/7/14.
//  Copyright © 2016年 pintec.com. All rights reserved.
//

#ifndef MachExceptionHandler_h
#define MachExceptionHandler_h

#include <mach/mach.h>

/**
 * Exception context.
 *
 * when an exception is caught, this type of data is sent to exception message handler.
 */
typedef struct {
    /**
     * exception type. (eg. EXC_BAD_ACCESS)
     */
    int type;
    
    /**
     * exception code.
     */
    int code;
    
    /**
     * exception subcode.
     */
    int subCode;
    
    /**
     * the crash thread
     */
    thread_t thread;
} MachExceptionContext;

/**
 * Mach exception handler type.
 *
 * Swift signature is `typealias (UnsafeMutablePointer<MachExceptionContext>) -> () = MachExceptionMessageHandler`
 */
typedef void (*MachExceptionMessageHandler)(MachExceptionContext * _Nonnull);

/**
 * Install mach exception handler to this process.
 *
 *  @param exceptionHandler callback when crash occurs
 */
void machExceptionHandlerInstall(MachExceptionMessageHandler _Nonnull exceptionHandler);

/**
 * Uninstall mach exception handler to this process.
 */
void machExceptionHandlerUninstall();

#endif /* MachExceptionHandler_h */
