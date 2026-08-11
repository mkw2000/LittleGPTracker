#ifndef _PSP_CRASH_HANDLER_H_
#define _PSP_CRASH_HANDLER_H_

namespace PSPCrashHandler {
bool Install(const char *applicationDirectory);
const char *GetReportPath();
}

#endif
