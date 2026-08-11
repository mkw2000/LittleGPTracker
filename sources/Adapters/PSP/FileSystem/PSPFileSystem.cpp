
#include "PSPFileSystem.h"
#include "System/Console/Trace.h"
#include <Application/Utils/wildcard.h>
#include <limits.h>
#include <stdarg.h>
#include <string.h>
#include <string>

#include <sys/dir.h>
#include <sys/stat.h>

PSPFile::PSPFile(SceUID file) {
    file_ = file;
    writeBufferPos_ = 0;
    readFailed_ = false;
    writeFailed_ = false;
}

PSPFile::~PSPFile() { Close(); }

int PSPFile::Read(void *ptr, int size, int nmemb) {
    if ((file_ < 0) || (size <= 0) || (nmemb <= 0))
        return 0;
    if (nmemb > (INT_MAX / size))
        return 0;
    int bytes = sceIoRead(file_, ptr, size * nmemb);
    if (bytes < 0) {
        readFailed_ = true;
        return 0;
    }
    return bytes / size;
}

bool PSPFile::Flush() {
    int offset = 0;
    while ((offset < writeBufferPos_) && !writeFailed_) {
        int written =
            sceIoWrite(file_, writeBuffer_ + offset, writeBufferPos_ - offset);
        if (written <= 0) {
            writeFailed_ = true;
            break;
        }
        offset += written;
    }
    writeBufferPos_ = 0;
    return !writeFailed_;
}

int PSPFile::Write(const void *ptr, int size, int nmemb) {
    if ((file_ < 0) || (writeFailed_) || (size <= 0) || (nmemb <= 0))
        return 0;
    if (nmemb > (INT_MAX / size))
        return 0;
    int len = size * nmemb;
    if (writeBufferPos_ + len > WRITE_BUFFER_SIZE) {
        if (!Flush())
            return 0;
    }
    if (len > WRITE_BUFFER_SIZE) {
        const unsigned char *source = (const unsigned char *)ptr;
        int offset = 0;
        while (offset < len) {
            int written = sceIoWrite(file_, source + offset, len - offset);
            if (written <= 0) {
                writeFailed_ = true;
                break;
            }
            offset += written;
        }
        return offset / size;
    } else {
        memcpy(writeBuffer_ + writeBufferPos_, ptr, len);
        writeBufferPos_ += len;
    }
    return nmemb;
}

void PSPFile::Printf(const char *fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);

    int length = vsnprintf(buffer, sizeof(buffer), fmt, args);
    if (length > 0) {
        int toWrite =
            (length < (int)sizeof(buffer)) ? length : (int)sizeof(buffer) - 1;
        Write(buffer, 1, toWrite);
    }
    va_end(args);
}

void PSPFile::Seek(long offset, int whence) {
    if ((file_ >= 0) && Flush())
        sceIoLseek(file_, offset, whence);
}

long PSPFile::Tell() {
    if (file_ < 0)
        return -1;
    long position = sceIoLseek(file_, 0, SEEK_CUR);
    return (position < 0) ? position : (position + writeBufferPos_);
}

void PSPFile::Close() {
    if (file_ >= 0) {
        Flush();
        sceIoClose(file_);
        file_ = -1;
    }
}
//

PSPDir::PSPDir(const char *path):I_Dir(path) {
}

void PSPDir::GetContent(char *mask) {

	Empty() ;
	if (!path_ || !path_[0] || !mask) {
		Trace::Error("Invalid PSP directory query");
		return;
	}

  SceIoDirent de;
  memset(&de,0,sizeof(SceIoDirent));

  SceUID fd=sceIoDopen(path_);
  if(fd<0) {
    Trace::Error("Failed to open %s",path_);
		return;
	}
	
    SceUID v=sceIoDread(fd,&de);
    char nameBuffer[256];

    while(v>0) {

        // See if matches current mask
        int len = strlen(de.d_name);
        for (int i = 0; i < len; i++) {
            nameBuffer[i] = tolower((unsigned char)de.d_name[i]);
        }
        nameBuffer[len]=0 ;
		
		if (wildcardfit(mask,nameBuffer)) {

			std::string fullpath=path_ ;
			if (path_[strlen(path_)-1]!='/') {
				fullpath+="/" ;
			}
			fullpath+=de.d_name ;
		
			Path *path=new Path(fullpath.c_str()) ;
			Insert(path) ;

		}
//		sceIoClose(v) ;
        v=sceIoDread(fd,&de);
    }
    if (v < 0)
        Trace::Error("Failed reading directory %s", path_);

    sceIoDclose(fd) ;

};


I_Dir *PSPFileSystem::Open(const char *path) {
    return new PSPDir(path) ;
}

I_File *PSPFileSystem::Open(const char *path,char *mode) {
	
	int flags=0 ;
	
	switch(*mode) {
        case 'r':
            flags=PSP_O_RDONLY ;
            break ;
        case 'w':
            flags = PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC;
            break;
        default:
            return 0 ;
    }

	SceUID file=sceIoOpen(path,flags, 0777) ;

    PSPFile *pspFile = 0;
    if (file>=0) {
        pspFile = new PSPFile(file);
    }
	return pspFile ;
}

FileType PSPFileSystem::GetFileType(const char *path) {

	struct stat attributes ;
	if (stat(path,&attributes)==0)
	{
		if (attributes.st_mode&S_IFDIR) return FT_DIR ;
		if (attributes.st_mode&S_IFREG) return FT_FILE ;
	}
	else
	{
		if (!strcmp("ms0:", path))
		{
			return FT_DIR;
		}

	}

	return FT_UNKNOWN ;

}

void PSPFileSystem::Delete(const char *path) {
	if (GetFileType(path)==FT_DIR)
		sceIoRmdir(path);
	else
		sceIoRemove(path);
}

Result PSPFileSystem::MakeDir(const char *path) {
	int retval = sceIoMkdir(path,0777);
  if (retval != 0)
  {
    std::ostringstream oss ;
    oss << "MakeDir failed with code " << retval;
    return Result(oss.str());
  }
  return Result::NoError;
}
