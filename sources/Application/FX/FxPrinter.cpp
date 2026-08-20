#include "FxPrinter.h"
#include "NativeReverb.h"
#include "Application/Player/Player.h"
#include <stdio.h>

FxPrinter::FxPrinter(ViewData *viewData)
    : samples_dir("project:samples"), viewData_(viewData) {
    int curInstr = viewData_->currentInstrument_;
    InstrumentBank *bank = viewData_->project_->GetInstrumentBank();
    instrument_ =
        static_cast<SampleInstrument *>(bank->GetInstrument(curInstr));
}

void FxPrinter::setParams() {
    irPad_ = instrument_->FindVariable(SIP_IR_PAD)->GetInt();
    irWet_ = instrument_->FindVariable(SIP_IR_WET)->GetInt();
    // Builds that exposed the old FFmpeg feature defaulted to a zero-length
    // tail. That truncates native reflections on short samples, so migrate the
    // value when the effect is first used.
    if (irPad_ <= 0) {
        irPad_ = 1000;
        instrument_->FindVariable(SIP_IR_PAD)->SetInt(irPad_);
    }
}

bool FxPrinter::setPaths() {
    fi_ = std::string(instrument_->GetFileName());
    Path input(fi_);
    if (!input.Matches("*.wav")) {
        notificationResult_ = "Reverb requires a WAV sample";
        return false;
    }

    std::string::size_type extension = fi_.find_last_of('.');
    std::string stem = extension == std::string::npos
                           ? fi_
                           : fi_.substr(0, extension);
    std::string preset =
        instrument_->FindVariable(SIP_PRINTFX)->GetString();
    for (int version = 1; version <= 99; version++) {
        foWav_ = stem + "_" + preset;
        if (version > 1) {
            char suffix[8];
            snprintf(suffix, sizeof(suffix), "_%d", version);
            foWav_ += suffix;
        }
        foWav_ += ".wav";
        if (foWav_.length() >= MAX_FILENAME_SIZE)
            continue;

        Path output = samples_dir.Descend(foWav_);
        if (!output.Exists()) {
            foPath_ = output.GetPath();
            return true;
        }
    }
    notificationResult_ = "No free reverb filename";
    return false;
}

const char *FxPrinter::GetNotification() { return notificationResult_.c_str(); }

bool FxPrinter::Run() {
    if (Player::GetInstance()->IsRunning()) {
        notificationResult_ = "Stop playback before PrintFX";
        return false;
    }
    SamplePool *pool = SamplePool::GetInstance();
    int sampleIndex = instrument_->GetSampleIndex();
    if (sampleIndex < 0 || sampleIndex >= pool->GetNameListSize()) {
        notificationResult_ = "No sample selected";
        return false;
    }
    if (pool->GetNameListSize() >= MAX_PIG_SAMPLES) {
        notificationResult_ = "Sample pool is full";
        return false;
    }
    setParams();
    if (irWet_ <= 0) {
        notificationResult_ = "Set wet above 0%";
        return false;
    }
    if (!setPaths())
        return false;

    NativeReverb::Settings settings;
    settings.preset_ = instrument_->FindVariable(SIP_PRINTFX)->GetInt();
    settings.wetPercent_ = irWet_;
    settings.tailMilliseconds_ = irPad_;
    std::string error;
    if (!NativeReverb::Render(pool->GetSource(sampleIndex), -1,
                              foPath_.c_str(), settings, error)) {
        notificationResult_ = error;
        return false;
    }

    int newIndex = pool->Reassign(foWav_);
    if (newIndex < 0) {
        FileSystem::GetInstance()->Delete(foPath_.c_str());
        notificationResult_ = "Rendered WAV reload failed";
        return false;
    }
    SoundSource *renderedSource = pool->GetSource(newIndex);
    if (!renderedSource || renderedSource->GetSize(-1) <= 0) {
        notificationResult_ = "Rendered sample validation failed";
        return false;
    }
    if (!instrument_->AssignSampleImmediate(newIndex) ||
        instrument_->GetSampleIndex() != newIndex) {
        notificationResult_ = "Rendered sample assignment failed";
        return false;
    }
    notificationResult_ = "Selected " + foWav_;
    Trace::Log("PRINTFX", "Rendered %s", foPath_.c_str());
    return true;
}
