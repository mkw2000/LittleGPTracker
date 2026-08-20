#include "AudioFileStreamer.h"
#include "Application/Utils/fixed.h"
#include "System/Console/Trace.h"
#include "Application/Model/Config.h"

// About 93 ms at 44.1 kHz. This keeps preview memory bounded while avoiding a
// filesystem request for every mixer slice.
static const int STREAM_BUFFER_FRAME_COUNT=4096 ;
static const int STREAM_BUFFER_READ_SIZE=16384 ;

AudioFileStreamer::AudioFileStreamer() {
	wav_=0 ;
	shift_=1 ;
	mode_=AFSM_STOPPED ;
	newPath_=false ;
	bufferStart_=0 ;
	bufferSize_=0 ;
} ;

AudioFileStreamer::~AudioFileStreamer() {
	SAFE_DELETE(wav_) ;
} ;
 
bool AudioFileStreamer::Start(const Path &path) {
  Trace::Debug("Starting to stream %s",path.GetPath().c_str());
	path_=path ;
	const char *shift=Config::GetInstance()->GetValue("PRELISTENATTENUATION") ;
	shift_=(shift)?atoi(shift):1 ;
  Trace::Debug("Streaming shift is %d",shift_);
	newPath_=true ;
	mode_=AFSM_PLAYING ;
	return true ;
} ;

void AudioFileStreamer::Stop() {
	mode_=AFSM_STOPPED ;
  Trace::Debug("Streaming stopped");
} ;

bool AudioFileStreamer::Render(fixed *buffer,int samplecount) {

	// See if we're playing

	if (mode_==AFSM_STOPPED) {
		SAFE_DELETE(wav_) ;
		return false ;
	}

	// Do we need to get a new file ?

	if (newPath_) {
		SAFE_DELETE(wav_) ;
		newPath_=false ;
		bufferStart_=0 ;
		bufferSize_=0 ;
	}

	// new look if we need to load the file

	if (!wav_) 
  {
		wav_=WavFile::Open(path_.GetPath().c_str()) ;
		if (!wav_) 
    {
      Trace::Error("Failed to open streaming of %s",path_.GetPath().c_str());
			mode_=AFSM_STOPPED ;
			return false ;
		}
		position_=0 ;
		bufferStart_=0 ;
		bufferSize_=0 ;
	}

	// we are playing a valid file

	long size=wav_->GetSize(-1) ;
	int count=samplecount ;
	if (position_+samplecount>=size)
  {
    Trace::Debug("Reached the end of %d samples",size);
		count=size-position_ ;
		mode_=AFSM_STOPPED ;
		memset(buffer,0,2*samplecount*sizeof(fixed)) ;
	}
	int channel=wav_->GetChannelCount(-1) ;

	// I might need to do sample interpolation here

	fixed *dst=buffer ;
	int remaining=count ;
	while (remaining>0) {
		int bufferOffset=position_-bufferStart_ ;
		if (bufferOffset<0 || bufferOffset>=bufferSize_) {
			int framesToRead=size-position_ ;
			if (framesToRead>STREAM_BUFFER_FRAME_COUNT) {
				framesToRead=STREAM_BUFFER_FRAME_COUNT ;
			}
			if (!wav_->GetBuffer(position_,framesToRead,
			                     STREAM_BUFFER_READ_SIZE)) {
				Trace::Error("Failed to read streaming WAV data from %s",
				             path_.GetPath().c_str()) ;
				memset(dst,0,2*remaining*sizeof(fixed)) ;
				mode_=AFSM_STOPPED ;
				return count!=remaining ;
			}
			bufferStart_=position_ ;
			bufferSize_=framesToRead ;
			bufferOffset=0 ;
		}

		int frames=bufferSize_-bufferOffset ;
		if (frames>remaining) frames=remaining ;
		short *src=(short *)wav_->GetSampleBuffer(-1) ;
		src+=bufferOffset*channel ;
		for (int i=0;i<frames;i++) {
			fixed v=*dst++=i2fp((*src++)>>(1+shift_)) ;
			if (channel==2) {
				*dst++=i2fp((*src++)>>(1+shift_)) ;
			} else {
				*dst++=v ;
			}
		}
		position_+=frames ;
		remaining-=frames ;
	}
	return true ;
}
