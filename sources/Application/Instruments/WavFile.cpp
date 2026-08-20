
#include "WavFile.h"
#include "System/Console/Trace.h"
#include "Foundation/Types/Types.h"
#include "Services/Time/TimeService.h"
#include "Application/Model/Config.h"
#include <limits.h>
#include <stdlib.h>

int WavFile::bufferChunkSize_=-1 ;
bool WavFile::initChunkSize_=true ;

short Swap16 (short from)
{
#ifdef __ppc__
	short result;
	((char*)&result)[0] = ((char*)&from)[1];
	((char*)&result)[1] = ((char*)&from)[0];
	return  result;
#else
	return from;
#endif	
}

int Swap32 (int from)
{
#ifdef __ppc__
	int result;
	((char*)&result)[0] = ((char*)&from)[3];
	((char*)&result)[1] = ((char*)&from)[2];
	((char*)&result)[2] = ((char*)&from)[1];
	((char*)&result)[3] = ((char*)&from)[0];			 
	return  result;
#else
	return from;
#endif 	
}


WavFile::WavFile(I_File *file) {
	if (initChunkSize_) {
		const char *size=Config::GetInstance()->GetValue("SAMPLELOADCHUNKSIZE") ;
		if (size) {
			bufferChunkSize_=atoi(size) ;
		}
		initChunkSize_=false;
	}
	samples_=0 ;
	size_=0 ;
	readBuffer_=0 ;
	readBufferSize_=0 ;
	readPosition_=-1 ;
	sampleBufferSize_=0 ;
	file_=file ;
} ;

WavFile::~WavFile() {
	if (file_) {
		file_->Close() ;
		delete file_ ;
	}
	SAFE_FREE(samples_) ;
	SAFE_FREE(readBuffer_) ;
} ;

WavFile *WavFile::Open(const char *path) {
	FileSystem *fs=FileSystem::GetInstance() ;
	I_File *file=fs->Open(path,"r") ;
	if (!file) return 0 ;

	WavFile *wav=new WavFile(file) ;
    file->Seek(0, SEEK_END);
    long fileSize = file->Tell();
    if (fileSize < 12 || wav->readBlock(0, 12) != 12) {
        Trace::Error("Truncated WAV header: %s", path);
        delete wav;
        return 0;
    }

    unsigned int chunk = 0;
    unsigned int waveTag = 0;
    memcpy(&chunk, wav->readBuffer_, 4);
    memcpy(&waveTag, (char *)wav->readBuffer_ + 8, 4);
    chunk = Swap32(chunk);
    waveTag = Swap32(waveTag);
    if (chunk != 0x46464952 || waveTag != 0x45564157) {
        Trace::Error("Bad RIFF/WAVE format: %s", path);
        delete wav;
        return 0;
    }

    bool foundFormat = false;
    bool foundData = false;
    unsigned short channelCount = 0;
    unsigned short bytesPerSample = 0;
    unsigned int sampleRate = 0;
    unsigned int dataSize = 0;
    long dataPosition = 0;
    long position = 12;

    while (position <= fileSize - 8) {
        if (wav->readBlock(position, 8) != 8) {
            Trace::Error("Truncated WAV chunk header: %s", path);
            delete wav;
            return 0;
        }

        unsigned int chunkSize = 0;
        memcpy(&chunk, wav->readBuffer_, 4);
        memcpy(&chunkSize, (char *)wav->readBuffer_ + 4, 4);
        chunk = Swap32(chunk);
        chunkSize = Swap32(chunkSize);
        position += 8;

        if (position < 0 || (unsigned long)chunkSize >
                                (unsigned long)(fileSize - position)) {
            Trace::Error("Invalid WAV chunk size in %s", path);
            delete wav;
            return 0;
        }

        if (chunk == 0x20746D66) {
            if (chunkSize < 16 || wav->readBlock(position, 16) != 16) {
                Trace::Error("Invalid WAV fmt chunk: %s", path);
                delete wav;
                return 0;
            }

            unsigned short compression = 0;
            unsigned short bitsPerSample = 0;
            memcpy(&compression, wav->readBuffer_, 2);
            memcpy(&channelCount, (char *)wav->readBuffer_ + 2, 2);
            memcpy(&sampleRate, (char *)wav->readBuffer_ + 4, 4);
            memcpy(&bitsPerSample, (char *)wav->readBuffer_ + 14, 2);
            compression = Swap16(compression);
            channelCount = Swap16(channelCount);
            sampleRate = Swap32(sampleRate);
            bitsPerSample = Swap16(bitsPerSample);

            if (compression != 1 || (channelCount != 1 && channelCount != 2) ||
                sampleRate == 0 ||
                (bitsPerSample != 8 && bitsPerSample != 16)) {
                Trace::Error("Unsupported WAV format in %s", path);
                delete wav;
                return 0;
            }
            bytesPerSample = bitsPerSample / 8;
            foundFormat = true;
        } else if (chunk == 0x61746164 && !foundData) {
            dataPosition = position;
            dataSize = chunkSize;
            foundData = true;
        }

        long advance = (long)chunkSize + (chunkSize & 1U);
        if (advance < 0 || advance > fileSize - position) {
            Trace::Error("Invalid padded WAV chunk in %s", path);
            delete wav;
            return 0;
        }
        position += advance;

        if (foundFormat && foundData)
            break;
    }

    if (!foundFormat || !foundData || dataSize == 0 ||
        dataPosition > INT_MAX) {
        Trace::Error("WAV is missing valid fmt/data chunks: %s", path);
        delete wav;
        return 0;
    }

    unsigned int bytesPerFrame = channelCount * bytesPerSample;
    if (bytesPerFrame == 0 || dataSize % bytesPerFrame != 0 ||
        dataSize / bytesPerFrame > INT_MAX) {
        Trace::Error("Invalid WAV sample data size: %s", path);
        delete wav;
        return 0;
    }

    wav->sampleRate_ = sampleRate;
    wav->channelCount_ = channelCount;
    wav->bytePerSample_ = bytesPerSample;
    wav->size_ = dataSize / bytesPerFrame;
    wav->dataPosition_ = (int)dataPosition;
    return wav ;
} ; 

void *WavFile::GetSampleBuffer(int note) {
	return samples_ ;
} ;

int WavFile::GetSize(int note) {
	return size_ ;
} ;

int WavFile::GetChannelCount(int note) {
    return channelCount_ ;
} ;

int WavFile::GetSampleRate(int note) {
    return sampleRate_ ;
} ;

long WavFile::readBlock(long start,long size) {
	if (start < 0 || size <= 0 || size > INT_MAX)
		return 0;
	if (size>readBufferSize_) {
		SAFE_FREE(readBuffer_) ;
		readBuffer_=SYS_MALLOC(size) ;
		readBufferSize_=readBuffer_ ? size : 0 ;
	}
	if (!readBuffer_) {
		readPosition_=-1 ;
		Trace::Error("Failed to allocate read buffer of size %d",size);
	} else {
		if (readPosition_!=start) {
			file_->Seek(start,SEEK_SET) ;
			long actualPosition=file_->Tell() ;
			if (actualPosition!=start) {
				readPosition_=-1 ;
				return 0 ;
			}
			readPosition_=start ;
		}
		int bytesRead = file_->Read(readBuffer_, 1, (int)size);
		if (bytesRead>0) {
			readPosition_+=bytesRead ;
			if (bytesRead!=(int)size) readPosition_=-1 ;
			return bytesRead ;
		}
		readPosition_=-1 ;
		return 0 ;
	}
	return 0 ;
} ;


bool WavFile::GetBuffer(long start,long size,int readChunkSize) {
	if (start < 0 || size <= 0 || start > size_ || size > size_ - start)
        return false;

	// compute the sample buffer size we need,
	// allocate if needed

	long long totalSamples = (long long)channelCount_ * size;
	long long outputBytes = totalSamples * (long long)sizeof(short);
	if (totalSamples <= 0 || outputBytes > INT_MAX)
        return false;
	int sampleBufferSize=(int)outputBytes ;
	if (sampleBufferSize>sampleBufferSize_) {
		SAFE_FREE(samples_) ;
		samples_=(short *)SYS_MALLOC(sampleBufferSize) ;
		sampleBufferSize_=sampleBufferSize ;
	}

	  if (!samples_)
	  {
	    Trace::Error("Failed to allocate %d bytes for WAV samples",sampleBufferSize);
	    return false;
	  }

	// compute the file buffer size we need to read

	long long inputBytes = totalSamples * bytePerSample_;
	long long inputStart = (long long)dataPosition_ +
                           (long long)start * channelCount_ * bytePerSample_;
	if (inputBytes > INT_MAX || inputStart < 0 || inputStart > INT_MAX)
        return false;
	int bufferSize=(int)inputBytes ;
	int bufferStart=(int)inputStart ;

	// Read the buffer but in small chunk to let the system breathe
	// if the files are big

	int count=bufferSize ;
	int offset=0 ;
	char *ptr=(char *)samples_ ;
	int readSize = (readChunkSize>0)
	                   ? ((count>readChunkSize)?readChunkSize:count)
	               : (bufferChunkSize_>0)
	                   ? bufferChunkSize_
	                   : ((count>4096)?4096:count);

	while (count>0) {
		readSize=(count>readSize)?readSize:count ;
		if (readBlock(bufferStart,readSize) != readSize) {
            Trace::Error("Short read while loading WAV sample");
            return false;
        }
		memcpy(ptr+offset,readBuffer_,readSize) ;
		bufferStart+=readSize ;
		count-=readSize ;
		offset+=readSize ;
		if (readChunkSize<=0 && bufferChunkSize_>0)
			TimeService::GetInstance()->Sleep(1) ;
	}


        // expand 8 bit data if needed

	unsigned char *src=(unsigned char *)samples_ ;
	short *dst=samples_ ;
	if (bytePerSample_==1) {
		for (int i=(int)totalSamples-1;i>=0;i--)
			dst[i]=(src[i]-128)*256 ;
	} else {
		for (int i=0;i<(int)totalSamples;i++)
			dst[i]=Swap16(dst[i]) ;
	}
	return true ;
} ;

void WavFile::Close() {
	file_->Close() ;
	SAFE_DELETE(file_) ;
	readPosition_=-1 ;
	SAFE_FREE(readBuffer_) ;
	readBufferSize_=0 ;
} ;

int WavFile::GetRootNote(int note) {
	return 60 ;
} 
