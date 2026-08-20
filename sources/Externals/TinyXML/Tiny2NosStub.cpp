#include "Tiny2NosStub.h"
#include "System/Console/Trace.h"
#include <stdarg.h>
#include <string.h>

void fprintf(I_File *f, char *fmt, ...) {
    char localBuffer[256];
    char *buffer = localBuffer;
    va_list args;
    va_start(args, fmt);
    int length = vsnprintf(0, 0, fmt, args);
    va_end(args);

    if (length < 0)
        return;
    if (length >= (int)sizeof(localBuffer)) {
        buffer = (char *)SYS_MALLOC(length + 1);
        if (!buffer) {
            Trace::Error("Could not allocate %d bytes for XML output",
                         length + 1);
            return;
        }
    }

    va_start(args, fmt);
    vsnprintf(buffer, length + 1, fmt, args);
    va_end(args);

    if (f->Write(buffer, 1, length) != length)
        Trace::Error("Short write while saving XML");

    if (buffer != localBuffer)
        SYS_FREE(buffer);
};
