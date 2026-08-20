#include "NativeReverb.h"

#include "Application/Instruments/SoundSource.h"
#include "System/Console/Trace.h"
#include "System/FileSystem/FileSystem.h"
#include "System/System/typedefs.h"
#include "Services/Time/TimeService.h"
#include "System/io/Status.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

namespace {

const int kCombCount = 4;
const int kAllpassCount = 2;
const int kOutputBlockFrames = 256;

struct Preset {
    float size_;
    float feedback_;
    float damping_;
};

const Preset kPresets[] = {
    {0.70f, 0.72f, 0.36f}, // room
    {1.00f, 0.80f, 0.28f}, // hall
    {0.48f, 0.74f, 0.12f}, // spring
    {1.35f, 0.86f, 0.24f}, // church
};

const int kCombLengths[kCombCount] = {1116, 1188, 1277, 1356};
const int kAllpassLengths[kAllpassCount] = {556, 441};

struct CombFilter {
    float *buffer_;
    int length_;
    int position_;
    float filterStore_;

    void Reset() {
        buffer_ = 0;
        length_ = 0;
        position_ = 0;
        filterStore_ = 0.0f;
    }

    bool Init(int length) {
        length_ = length;
        buffer_ = (float *)SYS_MALLOC(length_ * sizeof(float));
        if (!buffer_)
            return false;
        SYS_MEMSET(buffer_, 0, length_ * sizeof(float));
        return true;
    }

    float Process(float input, float feedback, float damping) {
        float output = buffer_[position_];
        filterStore_ = output * (1.0f - damping) + filterStore_ * damping;
        buffer_[position_] = input + filterStore_ * feedback;
        if (++position_ == length_)
            position_ = 0;
        return output;
    }

    void Close() {
        SAFE_FREE(buffer_);
        length_ = 0;
        position_ = 0;
        filterStore_ = 0.0f;
    }
};

struct AllpassFilter {
    float *buffer_;
    int length_;
    int position_;

    void Reset() {
        buffer_ = 0;
        length_ = 0;
        position_ = 0;
    }

    bool Init(int length) {
        length_ = length;
        buffer_ = (float *)SYS_MALLOC(length_ * sizeof(float));
        if (!buffer_)
            return false;
        SYS_MEMSET(buffer_, 0, length_ * sizeof(float));
        return true;
    }

    float Process(float input) {
        float delayed = buffer_[position_];
        float output = delayed - input;
        buffer_[position_] = input + delayed * 0.5f;
        if (++position_ == length_)
            position_ = 0;
        return output;
    }

    void Close() {
        SAFE_FREE(buffer_);
        length_ = 0;
        position_ = 0;
    }
};

class ReverbState {
  public:
    ReverbState() : feedback_(0.0f), damping_(0.0f) {
        for (int channel = 0; channel < 2; channel++) {
            for (int i = 0; i < kCombCount; i++)
                comb_[channel][i].Reset();
            for (int i = 0; i < kAllpassCount; i++)
                allpass_[channel][i].Reset();
        }
    }

    ~ReverbState() { Close(); }

    bool Init(int sampleRate, int presetIndex) {
        const Preset &preset = kPresets[presetIndex];
        feedback_ = preset.feedback_;
        damping_ = preset.damping_;
        const float sampleRateScale = (float)sampleRate / 44100.0f;
        const int stereoSpread =
            MAX(1, (int)(23.0f * sampleRateScale * preset.size_));

        for (int channel = 0; channel < 2; channel++) {
            for (int i = 0; i < kCombCount; i++) {
                int length = MAX(1, (int)(kCombLengths[i] * sampleRateScale *
                                          preset.size_));
                if (channel == 1)
                    length += stereoSpread;
                if (!comb_[channel][i].Init(length)) {
                    Close();
                    return false;
                }
            }
            for (int i = 0; i < kAllpassCount; i++) {
                int length = MAX(1, (int)(kAllpassLengths[i] *
                                          sampleRateScale * preset.size_));
                if (channel == 1)
                    length += stereoSpread;
                if (!allpass_[channel][i].Init(length)) {
                    Close();
                    return false;
                }
            }
        }
        return true;
    }

    void Process(float inputLeft, float inputRight, float &left, float &right) {
        left = 0.0f;
        right = 0.0f;
        for (int i = 0; i < kCombCount; i++) {
            left += comb_[0][i].Process(inputLeft, feedback_, damping_);
            right += comb_[1][i].Process(inputRight, feedback_, damping_);
        }
        left *= 1.0f / kCombCount;
        right *= 1.0f / kCombCount;
        for (int i = 0; i < kAllpassCount; i++) {
            left = allpass_[0][i].Process(left);
            right = allpass_[1][i].Process(right);
        }
        // The parallel comb average deliberately keeps the tank stable. Restore
        // an audible output level here so 100% wet is a useful upper bound.
        left *= 2.0f;
        right *= 2.0f;
    }

  private:
    void Close() {
        for (int channel = 0; channel < 2; channel++) {
            for (int i = 0; i < kCombCount; i++)
                comb_[channel][i].Close();
            for (int i = 0; i < kAllpassCount; i++)
                allpass_[channel][i].Close();
        }
    }

    CombFilter comb_[2][kCombCount];
    AllpassFilter allpass_[2][kAllpassCount];
    float feedback_;
    float damping_;
};

bool WriteField(I_File *file, const void *field, int size) {
    return file->Write(field, size, 1) == 1;
}

bool WriteHeader(I_File *file, unsigned int sampleRate,
                 unsigned int dataBytes) {
    unsigned int value32 = Swap32(0x46464952); // RIFF
    if (!WriteField(file, &value32, 4))
        return false;
    value32 = Swap32(36 + dataBytes);
    if (!WriteField(file, &value32, 4))
        return false;
    value32 = Swap32(0x45564157); // WAVE
    if (!WriteField(file, &value32, 4))
        return false;
    value32 = Swap32(0x20746D66); // fmt
    if (!WriteField(file, &value32, 4))
        return false;
    value32 = Swap32(16);
    if (!WriteField(file, &value32, 4))
        return false;

    unsigned short value16 = Swap16(1); // PCM
    if (!WriteField(file, &value16, 2))
        return false;
    value16 = Swap16(2); // stereo
    if (!WriteField(file, &value16, 2))
        return false;
    value32 = Swap32(sampleRate);
    if (!WriteField(file, &value32, 4))
        return false;
    value32 = Swap32(sampleRate * 4);
    if (!WriteField(file, &value32, 4))
        return false;
    value16 = Swap16(4);
    if (!WriteField(file, &value16, 2))
        return false;
    value16 = Swap16(16);
    if (!WriteField(file, &value16, 2))
        return false;

    value32 = Swap32(0x61746164); // data
    if (!WriteField(file, &value32, 4))
        return false;
    value32 = Swap32(dataBytes);
    return WriteField(file, &value32, 4);
}

short ToPcm16(float sample) {
    if (sample >= 1.0f)
        return 32767;
    if (sample <= -1.0f)
        return -32768;
    return (short)(sample * 32767.0f);
}

} // namespace

bool NativeReverb::Render(SoundSource *source, int note,
                          const char *outputPath, const Settings &settings,
                          std::string &error) {
    if (!source || !outputPath || !outputPath[0]) {
        error = "No sample selected";
        return false;
    }

    int frameCount = source->GetSize(note);
    int sampleRate = source->GetSampleRate(note);
    int channelCount = source->GetChannelCount(note);
    short *samples = (short *)source->GetSampleBuffer(note);
    if (!samples || frameCount <= 0 || (channelCount != 1 && channelCount != 2)) {
        error = "Unsupported sample data";
        return false;
    }
    if (sampleRate < 8000 || sampleRate > 192000) {
        error = "Unsupported sample rate";
        return false;
    }

    int presetIndex = settings.preset_;
    if (presetIndex < 0)
        presetIndex = 0;
    if (presetIndex >= (int)(sizeof(kPresets) / sizeof(kPresets[0])))
        presetIndex = (int)(sizeof(kPresets) / sizeof(kPresets[0])) - 1;
    int wetPercent = MIN(100, MAX(0, settings.wetPercent_));
    int tailMilliseconds = MIN(5000, MAX(0, settings.tailMilliseconds_));

    long long tailFrames =
        ((long long)sampleRate * tailMilliseconds) / 1000;
    long long outputFrames64 = (long long)frameCount + tailFrames;
    if (outputFrames64 <= 0 || outputFrames64 > INT_MAX ||
        outputFrames64 > (0xFFFFFFFFULL - 36) / 4) {
        error = "Rendered sample is too large";
        return false;
    }
    int outputFrames = (int)outputFrames64;
    unsigned int dataBytes = (unsigned int)outputFrames * 4;

    ReverbState reverb;
    if (!reverb.Init(sampleRate, presetIndex)) {
        error = "Not enough memory for reverb";
        return false;
    }

    FileSystem *fileSystem = FileSystem::GetInstance();
    I_File *file = fileSystem->Open(outputPath, (char *)"wb");
    if (!file) {
        error = "Could not create rendered sample";
        return false;
    }

    bool succeeded = WriteHeader(file, (unsigned int)sampleRate, dataBytes);
    short output[kOutputBlockFrames * 2];
    int bufferedSamples = 0;
    const float wet = (float)wetPercent / 100.0f;
    const float dry = 1.0f - wet;
    unsigned int changedSampleCount = 0;
    int peakDifference = 0;

    for (int frame = 0; frame < outputFrames && succeeded; frame++) {
        float dryLeft = 0.0f;
        float dryRight = 0.0f;
        if (frame < frameCount) {
            if (channelCount == 1) {
                dryLeft = dryRight = (float)samples[frame] / 32768.0f;
            } else {
                dryLeft = (float)samples[frame * 2] / 32768.0f;
                dryRight = (float)samples[frame * 2 + 1] / 32768.0f;
            }
        }

        float wetLeft = 0.0f;
        float wetRight = 0.0f;
        reverb.Process(dryLeft, dryRight, wetLeft, wetRight);
        const float stereoWetLeft = wetLeft * 0.9f + wetRight * 0.1f;
        const float stereoWetRight = wetRight * 0.9f + wetLeft * 0.1f;
        short dryPcmLeft = ToPcm16(dryLeft);
        short dryPcmRight = ToPcm16(dryRight);
        short renderedLeft =
            ToPcm16(dryLeft * dry + stereoWetLeft * wet);
        short renderedRight =
            ToPcm16(dryRight * dry + stereoWetRight * wet);
        int leftDifference = abs((int)renderedLeft - (int)dryPcmLeft);
        int rightDifference = abs((int)renderedRight - (int)dryPcmRight);
        if (leftDifference > 0)
            changedSampleCount++;
        if (rightDifference > 0)
            changedSampleCount++;
        peakDifference = MAX(peakDifference, MAX(leftDifference, rightDifference));
        output[bufferedSamples++] = Swap16(renderedLeft);
        output[bufferedSamples++] = Swap16(renderedRight);

        if (bufferedSamples == kOutputBlockFrames * 2) {
            succeeded = file->Write(output, sizeof(short), bufferedSamples) ==
                        bufferedSamples;
            bufferedSamples = 0;
        }
        if ((frame & 0x3FFF) == 0) {
            Status::Set((char *)"Rendering reverb %d%%",
                        (frame * 100) / outputFrames);
            TimeService::GetInstance()->Sleep(1);
        }
    }

    if (succeeded && bufferedSamples > 0) {
        succeeded = file->Write(output, sizeof(short), bufferedSamples) ==
                    bufferedSamples;
    }
    if (succeeded)
        succeeded = file->Flush() && !file->HasError();
    file->Close();
    delete file;

    if (!succeeded) {
        fileSystem->Delete(outputPath);
        error = "Failed writing rendered sample";
        Trace::Error("Native reverb failed writing %s", outputPath);
        return false;
    }
    if (changedSampleCount == 0 || peakDifference < 16) {
        fileSystem->Delete(outputPath);
        error = "Reverb output was unchanged";
        Trace::Error("Native reverb produced no audible change for %s",
                     outputPath);
        return false;
    }

    Trace::Log("PRINTFX", "Changed %u samples, peak delta %d",
               changedSampleCount, peakDifference);
    Status::Set((char *)"Reverb complete");
    return true;
}
