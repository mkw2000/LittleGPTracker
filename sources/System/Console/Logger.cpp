#include "Logger.h"
#include <iostream>
#include <string.h>

namespace {

const long kMaximumLogSize = 256 * 1024;
const char kLogLimitMessage[] = "[log stopped: 256 KiB limit reached]\n";

}

void StdOutLogger::AddLine(const char *line)
{
	std::cout << line << std::endl ;
}

// ----------------------------------------------

FileLogger::FileLogger(const Path &path)
:path_(path), limitReached_(false)
{
}

FileLogger::~FileLogger()
{
}

Result FileLogger::Init()
{
  limitReached_ = false;
  FILE *file = fopen(path_.GetPath().c_str(), "w");
  if (!file)
  {
    return Result("Failed to open log file");
  }
  fclose(file);
  return Result::NoError;
}

void FileLogger::AddLine(const char *line)
{
  if (!line || limitReached_)
    return;

  FILE *file = fopen(path_.GetPath().c_str(), "a+");
  if (!file)
    return;

  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return;
  }

  long size = ftell(file);
  long nextLineSize = (long)strlen(line) + 1;
  if (size < 0 || size + nextLineSize > kMaximumLogSize) {
    if (size >= 0 &&
        size + (long)(sizeof(kLogLimitMessage) - 1) <= kMaximumLogSize)
      fwrite(kLogLimitMessage, 1, sizeof(kLogLimitMessage) - 1, file);
    fclose(file);
    limitReached_ = true;
    return;
  }

  fprintf(file, "%s\n", line);
  fclose(file);
}
