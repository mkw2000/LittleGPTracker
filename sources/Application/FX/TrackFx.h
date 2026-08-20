#ifndef _TRACK_FX_H_
#define _TRACK_FX_H_

#include "Application/Utils/fixed.h"

class TrackFx {
  public:
    TrackFx();
    ~TrackFx();

    bool Init(int sampleRate);
    void Close();
    void Reset();

    bool Process(fixed *buffer, int frameCount, bool hasInput,
                 int stepSampleCount);
    bool ProcessCommand(unsigned int command, unsigned short parameter);

  private:
    TrackFx(const TrackFx &);
    TrackFx &operator=(const TrackFx &);

    struct DelayLine {
        short *buffer_;
        int frameCapacity_;
        int writeFrame_;

        void Clear();
        bool Init(int frameCapacity);
        void Close();
    };

    struct ReverbDelay {
        short *buffer_;
        int length_;
        int position_;

        void Clear();
        bool Init(int length);
        void Close();
    };

    static fixed ClampSample(long long value);
    static fixed ClampOutput(long long value);
    static short ToShort(fixed value);
    static fixed ByteToGain(unsigned char amount);

    void ProcessEcho(fixed inputLeft, fixed inputRight, int stepSampleCount,
                     fixed &left, fixed &right);
    void ProcessModulation(fixed inputLeft, fixed inputRight, fixed &left,
                           fixed &right);
    void ProcessReverb(fixed inputLeft, fixed inputRight, fixed &left,
                       fixed &right);
    fixed ProcessComb(ReverbDelay &delay, fixed input);
    fixed ProcessAllpass(ReverbDelay &delay, fixed input);
    fixed ReadModulated(const DelayLine &delay, int channel,
                        unsigned int delayFramesQ16) const;
    void SetModulationMode(int mode, unsigned char amount);

    int sampleRate_;
    bool initialized_;

    DelayLine echo_;
    unsigned char echoSend_;
    unsigned char echoFeedback_;
    fixed echoSendGain_;
    fixed echoFeedbackGain_;
    unsigned char echoSteps_;
    int echoNonZeroSamples_;

    DelayLine modulation_;
    int modulationMode_;
    unsigned char modulationAmount_;
    fixed modulationWetGain_;
    unsigned int modulationPhase_;
    unsigned int modulationPhaseIncrement_;
    unsigned int modulationBaseQ16_;
    unsigned int modulationDepthQ16_;
    fixed modulationFeedback_;

    ReverbDelay comb_[2][2];
    ReverbDelay allpass_[2];
    unsigned char reverbSend_;
    fixed reverbSendGain_;
    int reverbTailFrames_;
};

#endif
