#ifndef _CRASH_CONTEXT_H_
#define _CRASH_CONTEXT_H_

// Fixed-size, allocation-free breadcrumb storage for platform crash handlers.
// The value is best effort: it is intended to identify the last logged action,
// not to replace the normal application log.
namespace CrashContext {
void Set(const char *context);
const char *Get();
}

#endif
