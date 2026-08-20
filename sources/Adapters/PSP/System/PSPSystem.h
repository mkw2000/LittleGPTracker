#ifndef _PSP_SYSTEM_H_
#define _PSP_SYSTEM_H_

#include "System/System/System.h"
#include "UIFramework/SimpleBaseClasses/EventManager.h"

class PSPSystem : public System {
  public:
    static bool Boot(int argc, char **argv);
    static void Shutdown();
    static int MainLoop();
    static bool InitializePowerManagement();
    static void HandlePowerEvent(int powerInfo);
    static bool ProcessPowerEvents();

  public: // System implementation
    virtual unsigned long GetClock();
    virtual void Sleep(int millisec);
    virtual void *Malloc(unsigned size);
    virtual void Free(void *);
    virtual void Memset(void *addr, char val, int size);
    virtual void *Memcpy(void *s1, const void *s2, int n);
    virtual int GetBatteryLevel() { return -1; };
    virtual void PostQuitMessage();
    virtual unsigned int GetMemoryUsage();

    static bool finished_;

  private:
    static std::string eboot_;
    static EventManager *eventManager_;
    enum PowerState {
        POWER_RUNNING,
        POWER_SUSPEND_REQUESTED,
        POWER_SUSPENDED,
        POWER_RESUME_PENDING
    };
    static volatile int powerState_;
    static int suspendAckSema_;
    static int resumeSema_;
};
#endif
