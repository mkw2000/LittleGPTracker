#include "Application/Application.h"
#include "Adapters/PSP/System/PSPSystem.h"
#include "Adapters/SDL/GUI/SDLGUIWindowImp.h"

#include <pspdebug.h>
#include <pspkernel.h>
#include <psppower.h>

/* Define the module info section */
PSP_MODULE_INFO("LittleGPTracker", 0, 1, 1);
/* Define the main thread's attribute value (optional) */
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
/* Exit callback */
int exitCallback(int arg1, int arg2, void *common) {
    sceKernelExitGame();
    return 0;
}

int powerCallback(int unknown, int powerInfo, void *common) {
    PSPSystem::HandlePowerEvent(powerInfo);
    return 0;
}

/* Callback thread */
int callbackThread(SceSize args, void *argp) {
    int exitCbid = sceKernelCreateCallback("Exit Callback", exitCallback, NULL);
    int powerCbid =
        sceKernelCreateCallback("Power Callback", powerCallback, NULL);
    if (exitCbid >= 0)
        sceKernelRegisterExitCallback(exitCbid);
    if (powerCbid >= 0)
        scePowerRegisterCallback(-1, powerCbid);
    while (1)
        sceKernelSleepThreadCB();

    return 0;
}

/* Sets up the callback thread and returns its thread id */
int setupCallbacks(void) {
    int thid = 0;
    if (!PSPSystem::InitializePowerManagement())
        return -1;

    thid = sceKernelCreateThread("callback_thread", callbackThread, 0x11,
                                 0x1000, 0, 0);
    if (thid >= 0) {
        int result = sceKernelStartThread(thid, 0, 0);
        if (result < 0) {
            sceKernelDeleteThread(thid);
            return result;
        }
    }
    return thid;
}

int main(int argc,char *argv[]) 
{
	if (!PSPSystem::Boot(argc,argv)) {
		sceKernelExitGame();
		return 1;
	}

	SDLCreateWindowParams params ;
	params.title="littlegptracker" ;
	params.cacheFonts_=false ;
    params.framebuffer_=false ;
	if (!Application::GetInstance()->Init(params)) {
		sceKernelExitGame();
		return 1;
	}
	// The power callback waits for the event loop to acknowledge suspend.
	// Register it only after the application and audio device are ready.
	if (setupCallbacks()<0) {
		sceKernelExitGame();
		return 1;
	}
	PSPSystem::MainLoop() ;
    PSPSystem::Shutdown() ; 
	sceKernelExitGame();
    return 0;
}
