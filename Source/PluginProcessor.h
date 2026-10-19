#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include "DSP/PitchDetector.h"
#include "DSP/PsolaShifter.h"
#include "DSP/PitchCorrector.h"
#include "DSP/Scales.h"
#include "DSP/VocalFx.h"

namespace ids
{
    inline const char* key = "key";            inline const char* scale = "scale";
    inline const char* retune = "retune";      inline const char* humanize = "humanize";
    inline const char* mode = "mode";          inline const char* amount = "amount";
    inline const char* formant = "formant";    inline const char* mix = "mix";
    inline const char* deesser = "deesser";    inline const char* comp = "comp";
    inline const char* eq = "eq";              inline const char* lowLat = "lowlat";
    inline const char* outGain = "outgain";
    inline const char* harmOn = "harmon";      inline const char* harmMix = "harmmix";
    inline const char* harmSpread = "harmspread";
}

class AtlasProcessor : public juce::AudioProcessor
{
public:
    static constexpr int numVoices = 4;
    static constexpr int graphSize = 240;

    AtlasProcessor();
    ~AtlasProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Atlas Tune Pro"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // copies the selected key + scale into the 12 note toggles (called by the editor when key/scale change)
    void applyScaleToNotes();

    // meters / display data (read by the editor)
    std::atomic<float> inLevelDb { -100.0f }, outLevelDb { -100.0f };
    std::atomic<float> detectedMidi { -1.0f }, targetMidi { -1.0f };
    std::array<std::atomic<float>, graphSize> graph;
    std::atomic<int> graphPos { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void setParam (const juce::String& id, float plainValue);
    void configureForMode (bool lowLatency);
    void analyse();

    // DSP
    atlas::PitchDetector detector;
    atlas::PitchCorrector corrector;
    atlas::PsolaShifter lead;
    std::array<atlas::PsolaShifter, numVoices> harm;
    atlas::DeEsser deEsser;
    atlas::VocalCompressor compressor;
    atlas::VocalEq vocalEq;

    double sr = 44100.0;
    int hop = 128, hopCounter = 0;
    bool currentLowLat = false;
    int currentProgram = 0;

    // pitch state
    float T0 = 0.0f, TsynLead = 0.0f, formantLead = 1.0f;
    std::array<float, numVoices> TsynV {}, formantV {};
    std::array<bool, numVoices> voiceActive {};
    std::array<float, numVoices> voiceGainL {}, voiceGainR {};
    int heldNotes[128] {};

    // cached parameter pointers
    std::atomic<float> *pKey, *pScale, *pRetune, *pHuman, *pMode, *pAmount, *pFormant, *pMix, *pDeess, *pComp, *pEq,
        *pLowLat, *pOutGain, *pHarmOn, *pHarmMix, *pHarmSpread;
    std::array<std::atomic<float>*, 12> pNote {};
    std::array<std::atomic<float>*, numVoices> pVInt {}, pVType {}, pVLevel {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AtlasProcessor)
};
