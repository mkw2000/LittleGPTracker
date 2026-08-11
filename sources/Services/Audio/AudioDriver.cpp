
#include "AudioDriver.h"
#include "System/System/System.h"
#include "System/Console/Trace.h"
#include "System/Console/n_assert.h"

AudioDriver::AudioDriver(AudioSettings &settings) {
	settings_=settings ;
}

AudioDriver::~AudioDriver() {
}

bool AudioDriver::Init() {

  // Clear all buffers
	
   for (int i=0;i<SOUND_BUFFER_COUNT;i++) {
     pool_[i].buffer_=0 ;
     pool_[i].size_=0 ;
#ifdef PLATFORM_PSP
     pool_[i].retainedBuffer_=0 ;
     pool_[i].capacity_=0 ;
#endif
   } ;
   isPlaying_=false;	 

   return InitDriver() ;
}

void AudioDriver::Close() {
    CloseDriver();
    for (int i = 0; i < SOUND_BUFFER_COUNT; i++) {
#ifdef PLATFORM_PSP
        pool_[i].buffer_ = 0;
        SAFE_FREE(pool_[i].retainedBuffer_);
        pool_[i].capacity_ = 0;
#else
        SAFE_FREE(pool_[i].buffer_);
#endif
        pool_[i].size_ = 0;
    }
};

bool AudioDriver::Start() {

    isPlaying_ = true;

    for (int i = 0; i < SOUND_BUFFER_COUNT; i++) {
#ifdef PLATFORM_PSP
        pool_[i].buffer_ = 0;
        pool_[i].size_ = 0;
#else
        SAFE_FREE(pool_[i].buffer_);
#endif
    };

    poolQueuePosition_ = 0;
    poolPlayPosition_ = 0;
    hasData_ = false;

    bool started = StartDriver();
    if (!started)
        isPlaying_ = false;
    return started;
};

void AudioDriver::Stop() {
    isPlaying_ = false;
    hasData_ = false;
    StopDriver();
}

void AudioDriver::AddBuffer(short *buffer, int samplecount) {

    if (!buffer || samplecount <= 0 ||
        samplecount > SOUND_BUFFER_MAX / (2 * (int)sizeof(short))) {
        Trace::Error("Invalid audio buffer size: %d samples", samplecount);
        return;
    }

    int len = samplecount * 2 * sizeof(short);

    if (!isPlaying_)
        return;

    if (pool_[poolQueuePosition_].buffer_ != 0) {
        NInvalid;
        Trace::Error("Audio overrun, please report");
        return;
    }

#ifdef PLATFORM_PSP
    AudioBufferData &slot = pool_[poolQueuePosition_];
    if (slot.capacity_ < len) {
        char *replacement = (char *)SYS_MALLOC(len);
        if (!replacement) {
            Trace::Error("Could not grow PSP audio queue buffer");
            return;
        }
        SAFE_FREE(slot.retainedBuffer_);
        slot.retainedBuffer_ = replacement;
        slot.capacity_ = len;
    }
    SYS_MEMCPY(slot.retainedBuffer_, (char *)buffer, len);
    slot.size_ = len;
#if defined(__GNUC__)
    __sync_synchronize();
#endif
    slot.buffer_ = slot.retainedBuffer_;
#else
    char *newBuffer = (char *)SYS_MALLOC(len);
    if (!newBuffer) {
        Trace::Error("Could not allocate audio buffer");
        return;
    }

    SYS_MEMCPY(newBuffer, (char *)buffer, len);
    pool_[poolQueuePosition_].size_ = len;
#if defined(__GNUC__)
    __sync_synchronize();
#endif
    pool_[poolQueuePosition_].buffer_ = newBuffer;
#endif
    poolQueuePosition_ = (poolQueuePosition_ + 1) % SOUND_BUFFER_COUNT;
    hasData_ = true;
}

void AudioDriver::OnNewBufferNeeded() {
    SetChanged();
    Event event(Event::ADET_BUFFERNEEDED);
    NotifyObservers(&event);
};

void AudioDriver::onAudioBufferTick() {
    SetChanged();
    Event event(Event::ADET_DRIVERTICK);
    NotifyObservers(&event);
}

bool AudioDriver::hasData() {
	return hasData_ ;
}  ;

AudioSettings AudioDriver::GetAudioSettings() {
	return settings_ ;
} ;
