#include "SDLEventManager.h"
#include "Application/Application.h"
#include "Application/Controllers/ControlRoom.h"
#include "Application/Model/Config.h"
#include "System/Console/Trace.h"
#include "UIFramework/BasicDatas/GUIEvent.h"
#include "SDLGUIWindowImp.h"
#include <stdio.h>
#include <string.h>

bool SDLEventManager::finished_=false ;
bool SDLEventManager::dumpEvent_=false ;

SDLEventManager::SDLEventManager()
{
	keyboardCS_=0 ;
	for (int i=0;i<MAX_JOY_COUNT;i++)
	{
		joystick_[i]=0 ;
		controller_[i]=0 ;
		joystickInstance_[i]=-1 ;
		buttonCS_[i]=0 ;
		joystickCS_[i]=0 ;
		hatCS_[i]=0 ;
	}
}

SDLEventManager::~SDLEventManager()
{
	for (int i=0;i<MAX_JOY_COUNT;i++)
	{
		closeDevice(i) ;
		delete buttonCS_[i] ;
		delete joystickCS_[i] ;
		delete hatCS_[i] ;
	}
	delete keyboardCS_ ;
}

bool SDLEventManager::Init()
{
	EventManager::Init() ;
	finished_=false ;

	if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_JOYSTICK|SDL_INIT_GAMECONTROLLER|SDL_INIT_TIMER)<0)
	{
		Trace::Error("EVENT","SDL_Init failed: %s",SDL_GetError()) ;
		return false ;
	}

	SDL_ShowCursor(SDL_DISABLE) ;
	atexit(SDL_Quit) ;

	keyboardCS_=new KeyboardControllerSource("keyboard") ;
	dumpEvent_=false ;
	const char *dumpIt=Config::GetInstance()->GetValue("DUMPEVENT") ;
	if ((dumpIt)&&(!strcmp(dumpIt,"YES")))
	{
		dumpEvent_=true ;
	}

	for (int i=0;i<MAX_JOY_COUNT;i++)
	{
		joystick_[i]=0 ;
		controller_[i]=0 ;
		joystickInstance_[i]=-1 ;
	}

	int joyCount=SDL_NumJoysticks() ;
	for (int i=0;i<joyCount;i++)
	{
		if (!openDevice(i) && findFreeSlot()<0)
		{
			break ;
		}
	}

	return true ;
}

int SDLEventManager::findSlot(SDL_JoystickID instanceID) const
{
	for (int i=0;i<MAX_JOY_COUNT;i++)
	{
		if (joystick_[i] && joystickInstance_[i]==instanceID)
		{
			return i ;
		}
	}
	return -1 ;
}

int SDLEventManager::findFreeSlot() const
{
	for (int i=0;i<MAX_JOY_COUNT;i++)
	{
		if (!joystick_[i])
		{
			return i ;
		}
	}
	return -1 ;
}

void SDLEventManager::createSources(int slot)
{
	char sourceName[128] ;
	if (!buttonCS_[slot])
	{
		sprintf(sourceName,"buttonJoy%d",slot) ;
		buttonCS_[slot]=new ButtonControllerSource(sourceName) ;
	}
	if (!joystickCS_[slot])
	{
		sprintf(sourceName,"axisJoy%d",slot) ;
		joystickCS_[slot]=new JoystickControllerSource(sourceName) ;
	}
	if (!hatCS_[slot])
	{
		sprintf(sourceName,"hatJoy%d",slot) ;
		hatCS_[slot]=new HatControllerSource(sourceName) ;
	}
}

bool SDLEventManager::openDevice(int deviceIndex)
{
	int deviceCount=SDL_NumJoysticks() ;
	if (deviceIndex<0 || deviceIndex>=deviceCount)
	{
		return false ;
	}

	SDL_JoystickID deviceInstance=SDL_JoystickGetDeviceInstanceID(deviceIndex) ;
	if (deviceInstance>=0 && findSlot(deviceInstance)>=0)
	{
		return true ;
	}

	int slot=findFreeSlot() ;
	if (slot<0)
	{
		Trace::Log("EVENT","Ignoring joystick %d: maximum of %d controllers reached",deviceIndex,MAX_JOY_COUNT) ;
		return false ;
	}

	SDL_GameController *controller=0 ;
	SDL_Joystick *joystick=0 ;
	if (SDL_IsGameController(deviceIndex))
	{
		controller=SDL_GameControllerOpen(deviceIndex) ;
		if (controller)
		{
			joystick=SDL_GameControllerGetJoystick(controller) ;
		}
	}
	if (!joystick)
	{
		if (controller)
		{
			SDL_GameControllerClose(controller) ;
			controller=0 ;
		}
		joystick=SDL_JoystickOpen(deviceIndex) ;
	}
	if (!joystick)
	{
		Trace::Error("EVENT","Could not open joystick %d: %s",deviceIndex,SDL_GetError()) ;
		return false ;
	}

	joystick_[slot]=joystick ;
	controller_[slot]=controller ;
	joystickInstance_[slot]=SDL_JoystickInstanceID(joystick) ;
	createSources(slot) ;
	ControlRoom::GetInstance()->RetryMappings() ;

	Trace::Log("EVENT","Opened %s %d as slot %d (instance %d)",
		controller?"game controller":"joystick",deviceIndex,slot,joystickInstance_[slot]) ;
	Trace::Log("EVENT","Number of axis:%d",SDL_JoystickNumAxes(joystick)) ;
	Trace::Log("EVENT","Number of buttons:%d",SDL_JoystickNumButtons(joystick)) ;
	Trace::Log("EVENT","Number of hats:%d",SDL_JoystickNumHats(joystick)) ;
	return true ;
}

void SDLEventManager::closeDevice(int slot)
{
	if (slot<0 || slot>=MAX_JOY_COUNT)
	{
		return ;
	}
	if (controller_[slot])
	{
		SDL_GameControllerClose(controller_[slot]) ;
	}
	else if (joystick_[slot])
	{
		SDL_JoystickClose(joystick_[slot]) ;
	}
	controller_[slot]=0 ;
	joystick_[slot]=0 ;
	joystickInstance_[slot]=-1 ;
}

void SDLEventManager::setControllerAxis(int slot,SDL_GameControllerAxis axis,Sint16 value)
{
	if (slot<0 || slot>=MAX_JOY_COUNT || !joystickCS_[slot])
	{
		return ;
	}
	if (axis<=SDL_CONTROLLER_AXIS_RIGHTY)
	{
		float normalized=float(value)/32767.0f ;
		if (normalized<-1.0f) normalized=-1.0f ;
		if (normalized>1.0f) normalized=1.0f ;
		joystickCS_[slot]->SetAxis((int)axis,normalized) ;
	}
}

int SDLEventManager::MainLoop()
{
	GUIWindow *appWindow=Application::GetInstance()->GetWindow() ;
	SDLGUIWindowImp *sdlWindow=(SDLGUIWindowImp *)appWindow->GetImpWindow() ;
	while (!finished_)
	{
		SDL_Event event ;
		if (SDL_WaitEvent(&event))
			{
				switch (event.type)
				{
				case SDL_KEYDOWN:
					if (!event.key.repeat)
					{
						bool macFullscreenShortcut=(event.key.keysym.sym==SDLK_f)
							&&((event.key.keysym.mod&(KMOD_CTRL|KMOD_GUI))==(KMOD_CTRL|KMOD_GUI));
						if ((event.key.keysym.sym==SDLK_F11)||macFullscreenShortcut)
						{
							sdlWindow->ToggleFullscreen() ;
							break ;
						}
					}
					if (dumpEvent_)
					{
						Trace::Log("EVENT","key(%s:%d):%d",SDL_GetScancodeName(event.key.keysym.scancode),event.key.keysym.scancode,1) ;
					}
					keyboardCS_->SetKey((int)event.key.keysym.scancode,true) ;
					break ;

				case SDL_KEYUP:
					if (dumpEvent_)
					{
						Trace::Log("EVENT","key(%s:%d):%d",SDL_GetScancodeName(event.key.keysym.scancode),event.key.keysym.scancode,0) ;
					}
					keyboardCS_->SetKey((int)event.key.keysym.scancode,false) ;
					break ;

				case SDL_CONTROLLERDEVICEADDED:
				case SDL_JOYDEVICEADDED:
					openDevice(event.cdevice.which) ;
					break ;

				case SDL_CONTROLLERDEVICEREMOVED:
				case SDL_JOYDEVICEREMOVED:
					closeDevice(findSlot((SDL_JoystickID)event.cdevice.which)) ;
					break ;

				case SDL_CONTROLLERBUTTONDOWN:
				case SDL_CONTROLLERBUTTONUP:
				{
					int slot=findSlot(event.cbutton.which) ;
					if (slot>=0 && event.cbutton.button<MAX_BUTTON)
					{
						if (dumpEvent_)
						{
							Trace::Log("EVENT","gamepad(%d):%d:%d",slot,event.cbutton.button,
								event.type==SDL_CONTROLLERBUTTONDOWN?1:0) ;
						}
						buttonCS_[slot]->SetButton(event.cbutton.button,event.type==SDL_CONTROLLERBUTTONDOWN) ;
					}
				}
				break ;

				case SDL_CONTROLLERAXISMOTION:
				{
					int slot=findSlot(event.caxis.which) ;
					if (slot>=0)
					{
						if (dumpEvent_)
						{
							Trace::Log("EVENT","gamepad(%d)::%d=%d",slot,event.caxis.axis,event.caxis.value) ;
						}
						setControllerAxis(slot,(SDL_GameControllerAxis)event.caxis.axis,event.caxis.value) ;
					}
				}
				break ;

				case SDL_JOYBUTTONDOWN:
				case SDL_JOYBUTTONUP:
				{
					int slot=findSlot(event.jbutton.which) ;
					if (slot>=0 && event.jbutton.button<MAX_BUTTON)
					{
						if (dumpEvent_)
						{
							Trace::Log("EVENT","but(%d):%d:%d",slot,event.jbutton.button,
								event.type==SDL_JOYBUTTONDOWN?1:0) ;
						}
						buttonCS_[slot]->SetButton(event.jbutton.button,event.type==SDL_JOYBUTTONDOWN) ;
					}
				}
				break ;

				case SDL_JOYAXISMOTION:
				{
					int slot=findSlot(event.jaxis.which) ;
					if (slot>=0 && event.jaxis.axis<MAX_JOY_CHANNEL_AXIS)
					{
						if (dumpEvent_)
						{
							Trace::Log("EVENT","joy(%d)::%d=%d",slot,event.jaxis.axis,event.jaxis.value) ;
						}
						float normalized=float(event.jaxis.value)/32767.0f ;
						if (normalized<-1.0f) normalized=-1.0f ;
						if (normalized>1.0f) normalized=1.0f ;
						joystickCS_[slot]->SetAxis(event.jaxis.axis,normalized) ;
					}
				}
				break ;

				case SDL_JOYHATMOTION:
				{
					int slot=findSlot(event.jhat.which) ;
					if (slot>=0 && event.jhat.hat<MAX_HAT_CHANNELS)
					{
						if (dumpEvent_)
						{
							for (int i=0;i<4;i++)
							{
								int mask=1<<i ;
								if (event.jhat.value&mask)
								{
									Trace::Log("EVENT","hat(%d)::%d::%d",slot,event.jhat.hat,i) ;
								}
							}
						}
						hatCS_[slot]->SetHat(event.jhat.hat,event.jhat.value) ;
					}
				}
				break ;

				case SDL_JOYBALLMOTION:
					if (dumpEvent_)
					{
						int slot=findSlot(event.jball.which) ;
						Trace::Log("EVENT","ball(%d)::%d=(%d,%d)",slot,event.jball.ball,event.jball.xrel,event.jball.yrel) ;
					}
					break ;
			}

			switch (event.type)
			{
				case SDL_QUIT:
					sdlWindow->ProcessQuit() ;
					break ;
				case SDL_WINDOWEVENT:
					switch (event.window.event)
					{
						case SDL_WINDOWEVENT_EXPOSED:
							sdlWindow->ProcessExpose() ;
							break ;
						case SDL_WINDOWEVENT_RESIZED:
						case SDL_WINDOWEVENT_SIZE_CHANGED:
							sdlWindow->ProcessExpose(true) ;
							break ;
					}
					break ;
				case SDL_USEREVENT:
					sdlWindow->ProcessUserEvent(event) ;
					break ;
			}
		}
	}
	return 0 ;
}

void SDLEventManager::PostQuitMessage()
{
	Trace::Log("EVENT","SDEM:PostQuitMessage()") ;
	finished_=true ;
}

int SDLEventManager::GetKeyCode(const char *key)
{
	return SDL_GetScancodeFromName(key) ;
}
