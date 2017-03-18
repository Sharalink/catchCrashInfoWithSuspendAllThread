//
//  SwiftCrashDefaultFormatter.m
//  SwiftCrashReport
//
//  Created by yingdong.guo on 16/7/18.
//  Copyright © 2016年 pintec.com. All rights reserved.
//

#import "SwiftCrashDefaultFormatter.h"
#import "MachExceptionHandler.h"
#import "MachThreadBacktrace.h"

#import <execinfo.h>

/**
 *  implement this in objc since <execinfo.h> can neither be included privately nor export to public in Swift.
 */
@implementation SwiftCrashDefaultFormatter

+ (NSString * _Nonnull)formatCrashMessage:(MachExceptionContext)crashContext {
    int const maxStackDepth = 128;
    
    void **backtraceStack = calloc(maxStackDepth, sizeof(void *));
    int backtraceCount = backtraceForMachThread(crashContext.thread, backtraceStack, maxStackDepth);
    char **backtraceStackSymbols = backtrace_symbols(backtraceStack, backtraceCount);
    
    NSMutableString *stackTrace = [NSMutableString string];
    for (int i = 0; i < backtraceCount; ++i) {
        char *currentStackInfo = backtraceStackSymbols[i];
        
        [stackTrace appendString:[NSString stringWithUTF8String:currentStackInfo]];
        [stackTrace appendString:@"\n"];
    }
    
    return stackTrace;
}

@end
