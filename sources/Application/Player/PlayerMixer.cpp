#include "PlayerMixer.h"
#include "Application/Mixer/MixerService.h"
#include "Application/Model/Mixer.h"
#include "Application/Utils/char.h"
#include "Application/Utils/fixed.h"
#include "Services/Midi/MidiService.h"
#include "Services/Audio/Audio.h"
#include "SyncMaster.h"
#include "System/Console/Trace.h"
#include "System/System/System.h"
#include <math.h>
#include <stdlib.h>

PlayerMixer::PlayerMixer() {

    for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
        lastInstrument_[i] = 0;
		channel_[i] = new PlayerChannel(i);
		isChannelPlaying_[i] = false;
    }
}

bool PlayerMixer::Init(Project *project) {

	MixerService *ms=MixerService::GetInstance() ;
	if (!ms->Init()) {
			return false ;
	}

	AudioMixer *mixer=ms->GetMixBus(STREAM_MIX_BUS) ;
	mixer->Insert(fileStreamer_) ;

	project_=project ;
	int sampleRate=Audio::GetInstance()->GetSampleRate();
	for (int i=0;i<SONG_CHANNEL_COUNT;i++) {
		if (!channel_[i]->InitEffects(sampleRate)) {
			Trace::Error("Could not allocate realtime FX for channel %d",i);
		}
	}

	// Init states

	for (int i=0;i<SONG_CHANNEL_COUNT;i++) {
        lastInstrument_[i]=0 ;
	} ;

	clipped_=false ;
	return true ;
} ;

void PlayerMixer::Close()  {

	for (int i=0;i<SONG_CHANNEL_COUNT;i++) {
		channel_[i]->Reset() ;
		channel_[i]->CloseEffects() ;
	}


	MixerService *ms=MixerService::GetInstance() ;
	ms->Close() ;

}

bool PlayerMixer::Start() {
	MixerService *ms=MixerService::GetInstance() ;
	ms->AddObserver(*this) ;

	for (int i=0;i<SONG_CHANNEL_COUNT;i++) {
        notes_[i]=0xFF ;
    } ;

	if (!ms->Start()) {
		ms->RemoveObserver(*this) ;
		return false ;
	}
	return true ;
} ;

void PlayerMixer::Stop() {
	MixerService *ms=MixerService::GetInstance() ;
	ms->Stop() ;
	ms->RemoveObserver(*this) ;
} ;

void PlayerMixer::StartChannel(int channel) {
	isChannelPlaying_[channel]=true ;
} ;

void PlayerMixer::StopChannel(int channel) {

    StopInstrument(channel) ;
	isChannelPlaying_[channel]=false ;
} ;


bool PlayerMixer::IsChannelPlaying(int channel) {
	return isChannelPlaying_[channel] ;
} ;

I_Instrument *PlayerMixer::GetLastInstrument(int channel) {
	return lastInstrument_[channel] ;
} ;


bool PlayerMixer::Clipped() {
     return clipped_ ;
}

void PlayerMixer::Update(Observable &o,I_ObservableData *d) {

  // Notifies the player so that pattern data is processed
  SetChanged();
  NotifyObservers();

  // Transfer the mixer data
  Mixer *mixer = Mixer::GetInstance();

  for (int i=0;i<SONG_CHANNEL_COUNT;i++) {
      channel_[i]->SetMixBus(mixer->GetBus(i));
      channel_[i]->SetVolume(fl2fp(mixer->GetChannelVolume(i) / 255.0f));
      channel_[i]->SetHPFMode((unsigned char)mixer->GetChannelHPF(i));
      channel_[i]->SetLPFFreq(mixer->GetChannelLPF(i));
  }
  MixerService *ms=MixerService::GetInstance();
  ms->SetPregain(project_->GetPregain());
  ms->SetSoftclip(project_->GetSoftclip(), project_->GetSoftclipGain());
  ms->SetMasterVolume(project_->GetMasterVolume());
  clipped_=ms->Clipped();
} ;


void PlayerMixer::StartInstrument(int channel,I_Instrument *instrument,unsigned char note,bool newInstrument)  {
	channel_[channel]->StartInstrument(instrument,note,newInstrument) ;
	lastInstrument_[channel]=instrument ;
	notes_[channel]=note ;

} ;

void PlayerMixer::StopInstrument(int channel) {
    channel_[channel]->StopInstrument() ;
    notes_[channel]=0xFF ;
}

bool PlayerMixer::ProcessFXCommand(int channel, FourCC command,
                                   unsigned short parameter) {
    if (channel < 0 || channel >= SONG_CHANNEL_COUNT)
        return false;
    return channel_[channel]->ProcessFXCommand(command, parameter);
}

I_Instrument *PlayerMixer::GetInstrument(int channel) {
    return channel_[channel]->GetInstrument();
}

int PlayerMixer::GetPlayedBufferPercentage() {
	MixerService *ms=MixerService::GetInstance() ;
	return ms->GetPlayedBufferPercentage() ;
};

void PlayerMixer::SetChannelMute(int channel,bool mode) {
     channel_[channel]->SetMute(mode) ;
}

bool PlayerMixer::IsChannelMuted(int channel) {
     return channel_[channel]->IsMuted() ;
}

void PlayerMixer::StartStreaming(const Path &path) {
	MixerService *ms=MixerService::GetInstance() ;
	ms->Lock() ;
	fileStreamer_.Start(path) ;
	ms->Unlock() ;
} ;

void PlayerMixer::StopStreaming() {
	MixerService *ms=MixerService::GetInstance() ;
	ms->Lock() ;
	fileStreamer_.Stop() ;
	ms->Unlock() ;
} ;

void PlayerMixer::OnPlayerStart() {
	for (int i=0;i<SONG_CHANNEL_COUNT;i++) {
		channel_[i]->ResetEffects();
	}
	MixerService *ms=MixerService::GetInstance() ;
	ms->OnPlayerStart();
}

void PlayerMixer::OnPlayerStop() {
	MixerService *ms=MixerService::GetInstance() ;
	ms->OnPlayerStop();
	for (int i=0;i<SONG_CHANNEL_COUNT;i++) {
		channel_[i]->ResetEffects();
	}
}

static char noteBuffer[5] ;

int PlayerMixer::GetChannelNote(int channel) {
	return notes_[channel] ;
}

char *PlayerMixer::GetPlayedNote(int channel) {

    if (notes_[channel]!=0xFF) {
		note2visualizer(notes_[channel],noteBuffer) ; 
		return noteBuffer ;
    }
    return "  " ;
} ;

char *PlayerMixer::GetPlayedOctive(int channel) {
    if (notes_[channel]!=0xFF) {
		if (!IsChannelMuted(channel)) {
	        oct2visualizer(notes_[channel],noteBuffer) ; 
	        return noteBuffer ;
		} else {
			return "--" ;
		}
    }
    return "  " ;
} ;

AudioOut *PlayerMixer::GetAudioOut() {
	MixerService *ms=MixerService::GetInstance() ;
	return ms->GetAudioOut();
} ;

void PlayerMixer::Lock() {
	MixerService *ms=MixerService::GetInstance() ;
	ms->Lock() ;
} ;

void PlayerMixer::Unlock() {
	MixerService *ms=MixerService::GetInstance() ;
	ms->Unlock() ;
};
