#ifndef _NATIVE_REVERB_H_
#define _NATIVE_REVERB_H_

#include <string>

class SoundSource;

class NativeReverb {
  public:
    struct Settings {
        int preset_;
        int wetPercent_;
        int tailMilliseconds_;
    };

    static bool Render(SoundSource *source, int note, const char *outputPath,
                       const Settings &settings, std::string &error);
};

#endif
