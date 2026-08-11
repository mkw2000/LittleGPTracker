#include "Logger.h"
#include <iostream>

void StdOutLogger::AddLine(const char *line)
{
	std::cout << line << std::endl ;
}

// ----------------------------------------------

FileLogger::FileLogger(const Path &path)
:path_(path)
{
}

FileLogger::~FileLogger()
{
}

Result FileLogger::Init()
{
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
  FILE *file = fopen(path_.GetPath().c_str(), "a");
  if (!file)
    return;

  fprintf(file, "%s\n", line);
  fclose(file);
}
