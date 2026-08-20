#ifndef FxPrinter_H
#define FxPrinter_H

#include <string>
#include "Application/Views/ViewData.h"
#include "Application/Instruments/InstrumentBank.h"
#include "Application/Instruments/SampleInstrument.h"
#include "System/FileSystem/FileSystem.h"
#include "Application/Instruments/SamplePool.h"

class FxPrinter {
public:
    FxPrinter(ViewData* viewData);
    bool Run();
    const char *GetNotification();

  private:
    void setParams();
    bool setPaths();
    Path samples_dir;
    SampleInstrument* instrument_;
    ViewData* viewData_;
    int irPad_;
    int irWet_;
    std::string fi_;
    std::string foPath_;
    std::string foWav_;
    std::string notificationResult_;
};

#endif // FxPrinter_H
