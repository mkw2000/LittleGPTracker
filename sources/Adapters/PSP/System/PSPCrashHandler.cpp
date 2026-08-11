#include "PSPCrashHandler.h"

#include "System/Console/CrashContext.h"
#include <pspdebug.h>
#include <pspiofilemgr.h>
#include <pspkernel.h>
#include <pspsysmem.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace {

char reportPath_[512] = "lgpt-crash.log";

void WriteAll(SceUID file, const char *text) {
    int remaining = (int)strlen(text);
    while (remaining > 0) {
        int written = sceIoWrite(file, text, remaining);
        if (written <= 0)
            return;
        text += written;
        remaining -= written;
    }
}

void WriteFormat(SceUID file, const char *format, ...) {
    char buffer[256];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    buffer[sizeof(buffer) - 1] = 0;
    WriteAll(file, buffer);
}

void WriteRegisterDump(SceUID file, PspDebugRegBlock *registers) {
    WriteFormat(file, "epc=0x%08X cause=0x%08X badvaddr=0x%08X\n",
                registers->epc, registers->cause, registers->badvaddr);
    WriteFormat(file, "status=0x%08X hi=0x%08X lo=0x%08X\n",
                registers->status, registers->hi, registers->lo);
    for (int index = 0; index < 32; index += 4) {
        WriteFormat(file,
                    "r%02d=0x%08X r%02d=0x%08X r%02d=0x%08X "
                    "r%02d=0x%08X\n",
                    index, registers->r[index], index + 1,
                    registers->r[index + 1], index + 2,
                    registers->r[index + 2], index + 3,
                    registers->r[index + 3]);
    }
}

void ExceptionHandler(PspDebugRegBlock *registers) {
    SceUID file = sceIoOpen(reportPath_,
                            PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (file >= 0) {
        WriteAll(file, "LittleGPTracker PSP crash report\n");
        WriteFormat(file, "build=%s %s\n", __DATE__, __TIME__);
        WriteFormat(file, "thread=0x%08X\n", sceKernelGetThreadId());
        WriteFormat(file, "free_total=%u free_largest=%u\n",
                    (unsigned int)sceKernelTotalFreeMemSize(),
                    (unsigned int)sceKernelMaxFreeMemSize());
        WriteFormat(file, "last_log=%s\n\n", CrashContext::Get());
        WriteRegisterDump(file, registers);

        PspDebugStackTrace trace[16];
        int traceCount = pspDebugGetStackTrace2(registers, trace, 16);
        WriteFormat(file, "\nstack_frames=%d\n", traceCount);
        for (int index = 0; index < traceCount; index++) {
            WriteFormat(file, "%02d call=0x%08X function=0x%08X\n", index,
                        trace[index].call_addr, trace[index].func_addr);
        }
        sceIoClose(file);
    }

    // Leave a useful report on screen as well. The file is written first in
    // case video initialization is itself unsafe after the exception.
    pspDebugScreenInit();
    pspDebugScreenSetBackColor(0x00200020);
    pspDebugScreenSetTextColor(0xFFFFFFFF);
    pspDebugScreenClear();
    pspDebugScreenPrintf("LittleGPTracker crashed\n\n");
    pspDebugScreenPrintf("Crash report:\n%s\n\n", reportPath_);
    pspDebugScreenPrintf("Please copy lgpt-crash.log and lgpt.log.\n\n");
    pspDebugDumpException(registers);
}

} // namespace

bool PSPCrashHandler::Install(const char *applicationDirectory) {
    if (applicationDirectory && applicationDirectory[0]) {
        size_t length = strlen(applicationDirectory);
        const char *separator =
            applicationDirectory[length - 1] == '/' ? "" : "/";
        int result = snprintf(reportPath_, sizeof(reportPath_), "%s%s%s",
                              applicationDirectory, separator,
                              "lgpt-crash.log");
        if (result < 0 || result >= (int)sizeof(reportPath_)) {
            strcpy(reportPath_, "lgpt-crash.log");
            return false;
        }
    }
    return pspDebugInstallErrorHandler(ExceptionHandler) >= 0;
}

const char *PSPCrashHandler::GetReportPath() { return reportPath_; }
