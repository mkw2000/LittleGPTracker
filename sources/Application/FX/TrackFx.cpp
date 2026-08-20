#include "TrackFx.h"

#include "Application/Instruments/CommandList.h"
#include "System/System/System.h"

#include <limits.h>

namespace {

const int kEchoMaximumSeconds = 2;
const int kModulationMaximumMilliseconds = 64;
const int kReverbTailSeconds = 3;
const fixed kEchoMaximumFeedback = 31130; // 0.95 in Q15
const fixed kReverbFeedback = 23593;      // 0.72 in Q15
const fixed kAllpassFeedback = 16384;     // 0.50 in Q15
const fixed kReverbOutputGain = 19661;    // 0.60 in Q15

int ScaleDelayLength(int baseLength, int sampleRate, int spread) {
    int length = (baseLength * sampleRate + 22050) / 44100;
    length += spread;
    return (length < 1) ? 1 : length;
}

} // namespace

void TrackFx::DelayLine::Clear() {
    buffer_ = 0;
    frameCapacity_ = 0;
    writeFrame_ = 0;
}

bool TrackFx::DelayLine::Init(int frameCapacity) {
    Close();
    if (frameCapacity < 1)
        return false;
    buffer_ = (short *)SYS_MALLOC(frameCapacity * 2 * sizeof(short));
    if (!buffer_)
        return false;
    frameCapacity_ = frameCapacity;
    writeFrame_ = 0;
    SYS_MEMSET(buffer_, 0, frameCapacity_ * 2 * sizeof(short));
    return true;
}

void TrackFx::DelayLine::Close() {
    SAFE_FREE(buffer_);
    frameCapacity_ = 0;
    writeFrame_ = 0;
}

void TrackFx::ReverbDelay::Clear() {
    buffer_ = 0;
    length_ = 0;
    position_ = 0;
}

bool TrackFx::ReverbDelay::Init(int length) {
    Close();
    if (length < 1)
        return false;
    buffer_ = (short *)SYS_MALLOC(length * sizeof(short));
    if (!buffer_)
        return false;
    length_ = length;
    position_ = 0;
    SYS_MEMSET(buffer_, 0, length_ * sizeof(short));
    return true;
}

void TrackFx::ReverbDelay::Close() {
    SAFE_FREE(buffer_);
    length_ = 0;
    position_ = 0;
}

TrackFx::TrackFx()
    : sampleRate_(44100), initialized_(false), echoSend_(0),
      echoFeedback_(128), echoSendGain_(0), echoFeedbackGain_(0),
      echoSteps_(2), echoNonZeroSamples_(0), modulationMode_(0),
      modulationAmount_(0), modulationWetGain_(0), modulationPhase_(0),
      modulationPhaseIncrement_(0), modulationBaseQ16_(0),
      modulationDepthQ16_(0), modulationFeedback_(0), reverbSend_(0),
      reverbSendGain_(0), reverbTailFrames_(0) {
    echo_.Clear();
    modulation_.Clear();
    for (int channel = 0; channel < 2; ++channel) {
        for (int i = 0; i < 2; ++i)
            comb_[channel][i].Clear();
        allpass_[channel].Clear();
    }
}

TrackFx::~TrackFx() { Close(); }

bool TrackFx::Init(int sampleRate) {
    Close();
    sampleRate_ = (sampleRate > 0) ? sampleRate : 44100;

    if (!echo_.Init(sampleRate_ * kEchoMaximumSeconds)) {
        Close();
        return false;
    }
    int modulationFrames =
        (sampleRate_ * kModulationMaximumMilliseconds) / 1000 + 2;
    if (!modulation_.Init(modulationFrames)) {
        Close();
        return false;
    }

    const int combLengths[2] = {1116, 1356};
    for (int channel = 0; channel < 2; ++channel) {
        int spread = (channel == 0) ? 0 : ScaleDelayLength(23, sampleRate_, 0);
        for (int i = 0; i < 2; ++i) {
            if (!comb_[channel][i].Init(
                    ScaleDelayLength(combLengths[i], sampleRate_, spread))) {
                Close();
                return false;
            }
        }
        if (!allpass_[channel].Init(
                ScaleDelayLength(556, sampleRate_, spread))) {
            Close();
            return false;
        }
    }

    initialized_ = true;
    Reset();
    return true;
}

void TrackFx::Close() {
    echo_.Close();
    modulation_.Close();
    for (int channel = 0; channel < 2; ++channel) {
        for (int i = 0; i < 2; ++i)
            comb_[channel][i].Close();
        allpass_[channel].Close();
    }
    initialized_ = false;
}

void TrackFx::Reset() {
    echoSend_ = 0;
    echoFeedback_ = 128;
    echoSendGain_ = 0;
    echoFeedbackGain_ =
        fp_mul(ByteToGain(echoFeedback_), kEchoMaximumFeedback);
    echoSteps_ = 2;
    if (echo_.buffer_)
        SYS_MEMSET(echo_.buffer_, 0,
                   echo_.frameCapacity_ * 2 * sizeof(short));
    echo_.writeFrame_ = 0;
    echoNonZeroSamples_ = 0;

    if (modulation_.buffer_)
        SYS_MEMSET(modulation_.buffer_, 0,
                   modulation_.frameCapacity_ * 2 * sizeof(short));
    modulation_.writeFrame_ = 0;
    modulationMode_ = 0;
    modulationAmount_ = 0;
    modulationWetGain_ = 0;
    modulationPhase_ = 0;

    for (int channel = 0; channel < 2; ++channel) {
        for (int i = 0; i < 2; ++i) {
            ReverbDelay &delay = comb_[channel][i];
            if (delay.buffer_)
                SYS_MEMSET(delay.buffer_, 0, delay.length_ * sizeof(short));
            delay.position_ = 0;
        }
        ReverbDelay &delay = allpass_[channel];
        if (delay.buffer_)
            SYS_MEMSET(delay.buffer_, 0, delay.length_ * sizeof(short));
        delay.position_ = 0;
    }
    reverbSend_ = 0;
    reverbSendGain_ = 0;
    reverbTailFrames_ = 0;
}

fixed TrackFx::ClampSample(long long value) {
    const fixed maximum = 1073709056;
    const fixed minimum = -1073741824;
    if (value > maximum)
        return maximum;
    if (value < minimum)
        return minimum;
    return (fixed)value;
}

fixed TrackFx::ClampOutput(long long value) {
    if (value > INT_MAX)
        return INT_MAX;
    if (value < INT_MIN)
        return INT_MIN;
    return (fixed)value;
}

short TrackFx::ToShort(fixed value) {
    return (short)fp2i(ClampSample(value));
}

fixed TrackFx::ByteToGain(unsigned char amount) {
    if (amount == 255)
        return FP_ONE;
    return (fixed)(amount * 128 + amount / 2);
}

bool TrackFx::ProcessCommand(unsigned int command, unsigned short parameter) {
    unsigned char value = (unsigned char)(parameter & 0xFF);
    switch (command) {
    case I_CMD_ECHO:
        echoSend_ = value;
        echoSendGain_ = ByteToGain(value);
        return true;
    case I_CMD_ETIM:
        echoSteps_ = (value == 0) ? 1 : value;
        return true;
    case I_CMD_EFBK:
        echoFeedback_ = value;
        echoFeedbackGain_ =
            fp_mul(ByteToGain(value), kEchoMaximumFeedback);
        return true;
    case I_CMD_CHOR:
        SetModulationMode(1, value);
        return true;
    case I_CMD_FLNG:
        SetModulationMode(2, value);
        return true;
    case I_CMD_RVRB:
        reverbSend_ = value;
        reverbSendGain_ = ByteToGain(value);
        return true;
    default:
        return false;
    }
}

void TrackFx::SetModulationMode(int mode, unsigned char amount) {
    if (amount == 0) {
        if (modulationMode_ == mode) {
            modulationMode_ = 0;
            modulationAmount_ = 0;
            modulationWetGain_ = 0;
            if (modulation_.buffer_)
                SYS_MEMSET(modulation_.buffer_, 0,
                           modulation_.frameCapacity_ * 2 * sizeof(short));
            modulation_.writeFrame_ = 0;
        }
        return;
    }

    modulationMode_ = mode;
    modulationAmount_ = amount;
    modulationWetGain_ = ByteToGain(amount);
    modulationPhase_ = 0;
    if (mode == 1) {
        modulationBaseQ16_ =
            (unsigned int)(((long long)sampleRate_ * 20 * 65536) / 1000);
        modulationDepthQ16_ =
            (unsigned int)(((long long)sampleRate_ * 8 * 65536) / 1000);
        modulationPhaseIncrement_ =
            (unsigned int)(1503238554ULL / (unsigned int)sampleRate_); // 0.35 Hz
        modulationFeedback_ = 0;
    } else {
        modulationBaseQ16_ =
            (unsigned int)(((long long)sampleRate_ * 25 * 65536) / 10000);
        modulationDepthQ16_ =
            (unsigned int)(((long long)sampleRate_ * 2 * 65536) / 1000);
        modulationPhaseIncrement_ =
            (unsigned int)(858993459ULL / (unsigned int)sampleRate_); // 0.20 Hz
        modulationFeedback_ = 11469; // 0.35 in Q15
    }
}

void TrackFx::ProcessEcho(fixed inputLeft, fixed inputRight,
                          int stepSampleCount, fixed &left, fixed &right) {
    int delayFrames = stepSampleCount * (int)echoSteps_;
    if (delayFrames < 1)
        delayFrames = 1;
    if (delayFrames > echo_.frameCapacity_)
        delayFrames = echo_.frameCapacity_;

    int readFrame = echo_.writeFrame_ - delayFrames;
    if (readFrame < 0)
        readFrame += echo_.frameCapacity_;
    int writeIndex = echo_.writeFrame_ * 2;
    int readIndex = readFrame * 2;

    fixed delayedLeft = i2fp(echo_.buffer_[readIndex]);
    fixed delayedRight = i2fp(echo_.buffer_[readIndex + 1]);
    left = delayedLeft;
    right = delayedRight;

    fixed nextLeft = ClampSample(
        (long long)fp_mul(inputLeft, echoSendGain_) +
        fp_mul(delayedLeft, echoFeedbackGain_));
    fixed nextRight = ClampSample(
        (long long)fp_mul(inputRight, echoSendGain_) +
        fp_mul(delayedRight, echoFeedbackGain_));

    short oldLeft = echo_.buffer_[writeIndex];
    short oldRight = echo_.buffer_[writeIndex + 1];
    if (oldLeft != 0)
        --echoNonZeroSamples_;
    if (oldRight != 0)
        --echoNonZeroSamples_;
    echo_.buffer_[writeIndex] = ToShort(nextLeft);
    echo_.buffer_[writeIndex + 1] = ToShort(nextRight);
    if (echo_.buffer_[writeIndex] != 0)
        ++echoNonZeroSamples_;
    if (echo_.buffer_[writeIndex + 1] != 0)
        ++echoNonZeroSamples_;

    if (++echo_.writeFrame_ == echo_.frameCapacity_)
        echo_.writeFrame_ = 0;
}

fixed TrackFx::ReadModulated(const DelayLine &delay, int channel,
                             unsigned int delayFramesQ16) const {
    unsigned int wholeFrames = delayFramesQ16 >> 16;
    unsigned int fraction = delayFramesQ16 & 0xFFFF;
    if (wholeFrames >= (unsigned int)delay.frameCapacity_)
        wholeFrames = delay.frameCapacity_ - 1;

    int newerFrame = delay.writeFrame_ - (int)wholeFrames;
    while (newerFrame < 0)
        newerFrame += delay.frameCapacity_;
    int olderFrame = newerFrame - 1;
    if (olderFrame < 0)
        olderFrame += delay.frameCapacity_;

    fixed newer = i2fp(delay.buffer_[newerFrame * 2 + channel]);
    fixed older = i2fp(delay.buffer_[olderFrame * 2 + channel]);
    return (fixed)(((long long)newer * (65536 - fraction) +
                    (long long)older * fraction) >>
                   16);
}

void TrackFx::ProcessModulation(fixed inputLeft, fixed inputRight, fixed &left,
                                fixed &right) {
    unsigned int phase = modulationPhase_;
    unsigned int ramp = phase & 0x7FFFFFFF;
    unsigned int triangle =
        (phase & 0x80000000) ? (0x7FFFFFFF - ramp) : ramp;
    unsigned int lfoQ16 = triangle >> 15;
    if (lfoQ16 > 65535)
        lfoQ16 = 65535;
    unsigned int inverseQ16 = 65535 - lfoQ16;

    unsigned int leftDelay = modulationBaseQ16_ +
                             (unsigned int)(((unsigned long long)
                                                 modulationDepthQ16_ *
                                             lfoQ16) >>
                                            16);
    unsigned int rightDelay = modulationBaseQ16_ +
                              (unsigned int)(((unsigned long long)
                                                  modulationDepthQ16_ *
                                              inverseQ16) >>
                                             16);
    fixed delayedLeft = ReadModulated(modulation_, 0, leftDelay);
    fixed delayedRight = ReadModulated(modulation_, 1, rightDelay);
    left = fp_mul(delayedLeft, modulationWetGain_);
    right = fp_mul(delayedRight, modulationWetGain_);

    int writeIndex = modulation_.writeFrame_ * 2;
    modulation_.buffer_[writeIndex] =
        ToShort(ClampSample((long long)inputLeft +
                            fp_mul(delayedLeft, modulationFeedback_)));
    modulation_.buffer_[writeIndex + 1] =
        ToShort(ClampSample((long long)inputRight +
                            fp_mul(delayedRight, modulationFeedback_)));
    if (++modulation_.writeFrame_ == modulation_.frameCapacity_)
        modulation_.writeFrame_ = 0;
    modulationPhase_ += modulationPhaseIncrement_;
}

fixed TrackFx::ProcessComb(ReverbDelay &delay, fixed input) {
    fixed output = i2fp(delay.buffer_[delay.position_]);
    fixed next =
        ClampSample((long long)input + fp_mul(output, kReverbFeedback));
    delay.buffer_[delay.position_] = ToShort(next);
    if (++delay.position_ == delay.length_)
        delay.position_ = 0;
    return output;
}

fixed TrackFx::ProcessAllpass(ReverbDelay &delay, fixed input) {
    fixed delayed = i2fp(delay.buffer_[delay.position_]);
    fixed output = ClampSample((long long)delayed - input);
    fixed next =
        ClampSample((long long)input + fp_mul(delayed, kAllpassFeedback));
    delay.buffer_[delay.position_] = ToShort(next);
    if (++delay.position_ == delay.length_)
        delay.position_ = 0;
    return output;
}

void TrackFx::ProcessReverb(fixed inputLeft, fixed inputRight, fixed &left,
                            fixed &right) {
    fixed sentLeft = fp_mul(inputLeft, reverbSendGain_);
    fixed sentRight = fp_mul(inputRight, reverbSendGain_);
    if (sentLeft != 0 || sentRight != 0)
        reverbTailFrames_ = sampleRate_ * kReverbTailSeconds;
    else if (reverbTailFrames_ > 0)
        --reverbTailFrames_;

    fixed combLeft = ClampSample(
        ((long long)ProcessComb(comb_[0][0], sentLeft) +
         ProcessComb(comb_[0][1], sentLeft)) /
        2);
    fixed combRight = ClampSample(
        ((long long)ProcessComb(comb_[1][0], sentRight) +
         ProcessComb(comb_[1][1], sentRight)) /
        2);
    left = fp_mul(ProcessAllpass(allpass_[0], combLeft), kReverbOutputGain);
    right =
        fp_mul(ProcessAllpass(allpass_[1], combRight), kReverbOutputGain);
}

bool TrackFx::Process(fixed *buffer, int frameCount, bool hasInput,
                      int stepSampleCount) {
    if (!initialized_ || !buffer || frameCount <= 0)
        return hasInput;

    bool echoActive = (echoSend_ != 0 && hasInput) || echoNonZeroSamples_ > 0;
    bool modulationActive = modulationMode_ != 0 && modulationAmount_ != 0;
    bool reverbActive = (reverbSend_ != 0 && hasInput) || reverbTailFrames_ > 0;
    if (!echoActive && !modulationActive && !reverbActive)
        return hasInput;

    for (int frame = 0; frame < frameCount; ++frame) {
        int index = frame * 2;
        fixed dryLeft = hasInput ? buffer[index] : 0;
        fixed dryRight = hasInput ? buffer[index + 1] : 0;
        long long outputLeft = dryLeft;
        long long outputRight = dryRight;

        if (echoActive) {
            fixed left = 0;
            fixed right = 0;
            ProcessEcho(dryLeft, dryRight, stepSampleCount, left, right);
            outputLeft += left;
            outputRight += right;
        }
        if (modulationActive) {
            fixed left = 0;
            fixed right = 0;
            ProcessModulation(dryLeft, dryRight, left, right);
            outputLeft += left;
            outputRight += right;
        }
        if (reverbActive) {
            fixed left = 0;
            fixed right = 0;
            ProcessReverb(dryLeft, dryRight, left, right);
            outputLeft += left;
            outputRight += right;
        }

        // Preserve Piggy's existing mixer headroom. Only the short delay-line
        // storage is PCM-clamped; the mixed track remains full fixed-point.
        buffer[index] = ClampOutput(outputLeft);
        buffer[index + 1] = ClampOutput(outputRight);
    }

    return hasInput || echoNonZeroSamples_ > 0 || modulationActive ||
           reverbTailFrames_ > 0;
}
