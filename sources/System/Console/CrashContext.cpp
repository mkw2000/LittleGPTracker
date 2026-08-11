#include "CrashContext.h"

#include <string.h>

namespace {
char context_[192] = "startup";
}

void CrashContext::Set(const char *context) {
    if (!context)
        context = "";
    strncpy(context_, context, sizeof(context_) - 1);
    context_[sizeof(context_) - 1] = 0;
}

const char *CrashContext::Get() { return context_; }
