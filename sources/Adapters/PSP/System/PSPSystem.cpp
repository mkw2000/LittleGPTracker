
#include "PSPSystem.h"
#include "PSPCrashHandler.h"
#include "Adapters/Dummy/Midi/DummyMidi.h"
#include "Adapters/PSP/FileSystem/PSPFileSystem.h"
#include "Adapters/SDL/Audio/SDLAudio.h"
#include "Adapters/SDL/Audio/SDLAudioDriver.h"
#include "Adapters/SDL/GUI/GUIFactory.h"
#include "Adapters/SDL/GUI/SDLEventManager.h"
#include "Adapters/SDL/GUI/SDLGUIWindowImp.h"
#include "Adapters/SDL/Process/SDLProcess.h"
#include "Adapters/SDL/Timer/SDLTimer.h"
#include "Application/Model/Config.h"
#include "System/Console/Logger.h"
#include <malloc.h>
#include <pspdebug.h>
#include <pspiofilemgr.h>
#include <psppower.h>
#include <pspthreadman.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

EventManager *PSPSystem::eventManager_ = NULL ;
volatile int PSPSystem::powerState_ = PSPSystem::POWER_RUNNING;
int PSPSystem::suspendAckSema_ = -1;
int PSPSystem::resumeSema_ = -1;

int PSPSystem::MainLoop() 
{
	eventManager_->InstallMappings() ;
	return eventManager_->MainLoop() ;
} ;

bool PSPSystem::Boot(int argc,char **argv) {
#ifdef PSPDEBUG
	pspDebugScreenInit();
#endif

	// Install System
	System::Install(new PSPSystem()) ;

	// Install FileSystem
	FileSystem::Install(new PSPFileSystem()) ;

	Path bootPath(argv[0]) ;
	Path parent=bootPath.GetParent() ;
	bool crashHandlerInstalled =
	    PSPCrashHandler::Install(parent.GetPath().c_str());

	Path::SetAlias("bin",parent.GetPath().c_str()) ;
	Path::SetAlias("root",parent.GetPath().c_str()) ;

	Config::GetInstance()->ProcessArguments(argc,argv) ;

  Path logPath("bin:lgpt.log");
  // Preserve one prior session so rebooting after a crash does not immediately
  // destroy the application breadcrumbs needed to diagnose it.
  Path previousLogPath("bin:lgpt-prev.log");
  sceIoRemove(previousLogPath.GetPath().c_str());
  sceIoRename(logPath.GetPath().c_str(), previousLogPath.GetPath().c_str());
  FileLogger *fileLogger=new FileLogger(logPath);
  if(fileLogger->Init().Succeeded())
  {
    Trace::GetInstance()->SetLogger(*fileLogger);    
  }
	if (crashHandlerInstalled)
		Trace::Log("PSP", "Crash reports enabled at %s",
		           PSPCrashHandler::GetReportPath());
	else
		Trace::Error("Could not install PSP crash reporter");
	 
	// Install GUI Factory
	I_GUIWindowFactory::Install(new GUIFactory()) ;

	// Install Timers

	TimerService::GetInstance()->Install(new SDLTimerService()) ;

	// Install Sound

	AudioSettings hints ;
	hints.bufferSize_=128 ;
	hints.preBufferCount_=6 ;
	Audio::Install(new SDLAudio(hints)) ;

	// Install Midi
	MidiService::Install(new DummyMidi()) ;

	// Install Threads

	SysProcessFactory::Install(new SDLProcessFactory()) ;

    eventManager_ = I_GUIWindowFactory::GetInstance()->GetEventManager();
    if (!eventManager_->Init()) {
        Trace::Error("Failed to initialize PSP SDL event manager");
        return false;
    }

    // PSP SDL Basic config

    bool invert = false;
    Config *config=Config::GetInstance() ;
	const char *s=config->GetValue("INVERT") ;

	if ((s)&&(!strcmp(s,"YES"))) {
		invert=true ;
	}

	if (!invert) {
		eventManager_->MapAppButton("but:0:0",APP_BUTTON_B) ;
		eventManager_->MapAppButton("but:0:3",APP_BUTTON_A) ;
	}else {
		eventManager_->MapAppButton("but:0:0",APP_BUTTON_A) ;
		eventManager_->MapAppButton("but:0:3",APP_BUTTON_B) ;
	}
	eventManager_->MapAppButton("but:0:7",APP_BUTTON_LEFT) ;
	eventManager_->MapAppButton("but:0:9",APP_BUTTON_RIGHT) ;
	eventManager_->MapAppButton("but:0:8",APP_BUTTON_UP) ;
	eventManager_->MapAppButton("but:0:6",APP_BUTTON_DOWN) ;
	eventManager_->MapAppButton("but:0:4",APP_BUTTON_L) ;
	eventManager_->MapAppButton("but:0:5",APP_BUTTON_R) ;
	eventManager_->MapAppButton("but:0:11",APP_BUTTON_START) ;

	return true;
} ;

void PSPSystem::Shutdown() {
} ;

bool PSPSystem::InitializePowerManagement() {
    powerState_ = POWER_RUNNING;
    suspendAckSema_ = sceKernelCreateSema("LGPT suspend ack", 0, 0, 1, NULL);
    if (suspendAckSema_ < 0)
        return false;

    resumeSema_ = sceKernelCreateSema("LGPT resume", 0, 0, 1, NULL);
    if (resumeSema_ < 0) {
        sceKernelDeleteSema(suspendAckSema_);
        suspendAckSema_ = -1;
        return false;
    }
    return true;
}

void PSPSystem::HandlePowerEvent(int powerInfo) {
    const int suspendFlags =
        PSP_POWER_CB_POWER_SWITCH | PSP_POWER_CB_SUSPENDING;
    const int resumeFlags =
        PSP_POWER_CB_RESUMING | PSP_POWER_CB_RESUME_COMPLETE;
    if (powerInfo & suspendFlags) {
        if (__sync_bool_compare_and_swap(&powerState_, POWER_RUNNING,
                                         POWER_SUSPEND_REQUESTED)) {
            // SDL teardown belongs to the main thread. Keep this callback
            // blocked until that teardown has completed so the PSP cannot
            // enter suspend with its audio channel still active.
            if (suspendAckSema_ >= 0)
                sceKernelWaitSema(suspendAckSema_, 1, NULL);
        }
    }

    else if (powerInfo & resumeFlags) {
        if (__sync_bool_compare_and_swap(&powerState_, POWER_SUSPENDED,
                                         POWER_RESUME_PENDING) &&
            resumeSema_ >= 0)
            sceKernelSignalSema(resumeSema_, 1);
    }
}

bool PSPSystem::ProcessPowerEvents() {
    if (powerState_ != POWER_SUSPEND_REQUESTED)
        return false;

    bool audioSuspended = SDLAudioDriver::SuspendForPowerEvent();
    powerState_ = POWER_SUSPENDED;
#if defined(__GNUC__)
    __sync_synchronize();
#endif
    if (suspendAckSema_ >= 0)
        sceKernelSignalSema(suspendAckSema_, 1);

    // The power callback can now return and let the PSP sleep. Keep the main
    // thread entirely outside SDL until the resume callback wakes it.
    if (resumeSema_ >= 0)
        sceKernelWaitSema(resumeSema_, 1, NULL);

    bool audioResumed = SDLAudioDriver::ResumeFromPowerEvent();
    powerState_ = POWER_RUNNING;
    if (!audioSuspended)
        Trace::Error("Could not suspend PSP audio cleanly");
    if (!audioResumed)
        Trace::Error("Could not restore PSP audio after resume");
    return true;
}

unsigned long PSPSystem::GetClock() {
    struct timeval now;
    Uint32 ticks;
    gettimeofday(&now, NULL);
    ticks = (now.tv_sec) * 1000 + (now.tv_usec) / 1000;
    return (ticks);
}

void PSPSystem::Sleep(int millisec) {
    if (millisec > 0)
        sceKernelDelayThread((SceUInt)millisec * 1000);
}

void *PSPSystem::Malloc(unsigned size) {
	return malloc(size) ;
}

void PSPSystem::Free(void *ptr) {
	free(ptr) ;
} 

void PSPSystem::Memset(void *addr,char val,int size) {
    
    unsigned int ad=(unsigned int)addr ;
    if (((ad&0x3)==0)&&((size&0x3)==0)) { // Are we 4-byte aligned ?
        unsigned int intVal=0 ;
        for (int i=0;i<4;i++) {
             intVal=(intVal<<8)+val ;  
        }
        unsigned int *dst=(unsigned int *)addr ;
        size_t intSize=size>>2 ;
        
        for (unsigned int i=0;i<intSize;i++) {
            *dst++=intVal ;
        }        
    } else {
        memset(addr,val,size) ;
    } ;
} ;

void *PSPSystem::Memcpy(void *s1, const void *s2, int n) {
    return memcpy(s1,s2,n) ;
} ;  
/*
void PSPSystem::AddUserLog(const char *msg) {
#ifdef PSPDEBUG
	pspDebugScreenPrintf("%s\n",msg) ;
#endif
};
*/
void PSPSystem::PostQuitMessage() {
	SDLEventManager::GetInstance()->PostQuitMessage() ;
};

unsigned int PSPSystem::GetMemoryUsage() {
    struct mallinfo m = mallinfo();
    return m.uordblks;
}
