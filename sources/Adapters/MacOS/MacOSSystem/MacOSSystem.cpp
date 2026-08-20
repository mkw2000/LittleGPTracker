
#include "MacOSSystem.h"
#include "Adapters/RTMidi/RTMidiService.h"
#include "Adapters/RTAudio/RTAudioStub.h"
#ifndef _USE_NCURSES_
#include "Adapters/SDL2/GUI/GUIFactory.h"
#include "Adapters/SDL2/GUI/SDLGUIWindowImp.h"
#else
#include "Adapters/Unix/GUI/GUIFactory.h"
#endif
#include "Adapters/Unix/FileSystem/UnixFileSystem.h"
#include "Adapters/Unix/Process/UnixProcess.h"
#include "Application/Model/Config.h"
#include "Adapters/SDL2/Timer/SDLTimer.h"
#include "System/Console/Trace.h"
#include "System/Console/Logger.h"
#include <time.h>
#include <sys/time.h>
#include <memory.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <CoreFoundation/CoreFoundation.h>

EventManager *MacOSSystem::eventManager_ = NULL ;

int MacOSSystem::MainLoop()
{
	eventManager_->InstallMappings() ;
	return eventManager_->MainLoop() ;
}

void MacOSSystem::Boot(int argc,char **argv)
{
	// Install System
	System::Install(new MacOSSystem()) ;

	// Install GUI Factory
	I_GUIWindowFactory::Install(new GUIFactory()) ;

	// Install FileSystem
	FileSystem::Install(new UnixFileSystem()) ;

  installAliases();
  
  // Tracing
  
#ifdef _DEBUG
  Trace::GetInstance()->SetLogger(*(new StdOutLogger()));
#else
  // Contents/Resources is code-signed. Keep the mutable diagnostic log next
  // to the app bundle so launching the app does not invalidate its signature.
  Path logPath("root:lgpt.log");
  FileLogger *fileLogger=new FileLogger(logPath);
  if(fileLogger->Init().Succeeded())
  {
    Trace::GetInstance()->SetLogger(*fileLogger);    
  }
#endif
  
	Config::GetInstance()->ProcessArguments(argc,argv) ;
	
	// Install Timers

	TimerService::GetInstance()->Install(new SDLTimerService()) ;

	// Install Sound

	AudioSettings hints ;
	hints.audioAPI_="" ;
	hints.audioDevice_="" ;
	hints.bufferSize_=64 ;
	hints.preBufferCount_=0;
	hints.sampleRate_=44100;
	
	Audio::Install(new RTAudioStub(hints)) ;

	// Install Midi
	MidiService::Install(new RTMidiService()) ;

	// Install Threads

	SysProcessFactory::Install(new UnixProcessFactory()) ;

	eventManager_=I_GUIWindowFactory::GetInstance()->GetEventManager() ;
	eventManager_->Init() ;

	eventManager_->MapAppButton("a",APP_BUTTON_A) ;
	eventManager_->MapAppButton("s",APP_BUTTON_B) ;
	eventManager_->MapAppButton("left",APP_BUTTON_LEFT) ;
	eventManager_->MapAppButton("right",APP_BUTTON_RIGHT) ;
	eventManager_->MapAppButton("up",APP_BUTTON_UP) ;
	eventManager_->MapAppButton("down",APP_BUTTON_DOWN) ;
	eventManager_->MapAppButton("x",APP_BUTTON_L) ;
	eventManager_->MapAppButton("left shift",APP_BUTTON_R) ;
	eventManager_->MapAppButton("space",APP_BUTTON_START) ;


}

void MacOSSystem::Shutdown()
{
}

void MacOSSystem::installAliases()
{
	// Keep executable resources inside the app bundle while leaving projects and
	// samples next to the app, where users can edit and back them up normally.
	char bundlePath[PATH_MAX];
	CFBundleRef mainBundle = CFBundleGetMainBundle();
	if (!mainBundle)
	{
		return;
	}

	CFURLRef mainBundleURL = CFBundleCopyBundleURL( mainBundle);
	if (!mainBundleURL)
	{
		return;
	}

	Boolean pathOK = CFURLGetFileSystemRepresentation(
		mainBundleURL, true, (UInt8 *)bundlePath, sizeof(bundlePath));
	CFRelease( mainBundleURL);
	if (!pathOK)
	{
		return;
	}

	// LaunchServices normally gives us an absolute URL, but direct launches can
	// preserve a relative bundle path. Canonicalize it before installing aliases.
	char absoluteBundlePath[PATH_MAX];
	if (realpath(bundlePath, absoluteBundlePath))
	{
		strncpy(bundlePath, absoluteBundlePath, sizeof(bundlePath) - 1);
		bundlePath[sizeof(bundlePath) - 1] = 0;
	}

	std::string asciiPath = bundlePath;

	// Get the directory containing the .app bundle for project data.
	std::string::size_type pos = asciiPath.find_last_of('/');
	std::string directoryPath = (pos == std::string::npos) ? std::string(".") : std::string(asciiPath, 0, pos);
	std::string resourcesPath = asciiPath + "/Contents/Resources";
  
	Path::SetAlias("bin",resourcesPath.c_str()) ;
	Path::SetAlias("root",directoryPath.c_str()) ;
}

//------------------------------------------------------------------------------


static long secbase=0;

unsigned long MacOSSystem::GetClock() {
	
	struct timeval tp;
	
	gettimeofday(&tp, NULL);
	if (!secbase)
    {
        secbase = tp.tv_sec;
        return long(tp.tv_usec/1000.0);
	}
	return long((tp.tv_sec - secbase)*1000 + tp.tv_usec/1000.0);
}


//------------------------------------------------------------------------------


void MacOSSystem::Sleep(int millisec)
{
/*	if (millisec>0)
		::Sleep(millisec) ;
*/}


//------------------------------------------------------------------------------

void *MacOSSystem::Malloc(unsigned size)
{
	return malloc(size) ;
}


//------------------------------------------------------------------------------

void MacOSSystem::Free(void *ptr) {
	free(ptr) ;
} 


//------------------------------------------------------------------------------

void MacOSSystem::Memset(void *addr,char val,int size) {
    
    unsigned long ad=(unsigned long)addr ;
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
    }
}


//------------------------------------------------------------------------------

void *MacOSSystem::Memcpy(void *s1, const void *s2, int n)
{
    return memcpy(s1,s2,n) ;
}  

//------------------------------------------------------------------------------


void MacOSSystem::PostQuitMessage()
{
	eventManager_->PostQuitMessage() ;
} 


//------------------------------------------------------------------------------

int unsigned MacOSSystem::GetMemoryUsage()
{
	return 0 ;
}
