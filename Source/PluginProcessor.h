#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include "Tunings.h"

namespace PID
{
    inline constexpr const char* mode      = "mode";
    inline constexpr const char* inGain    = "inGain";
    inline constexpr const char* gate      = "gate";
    inline constexpr const char* comp      = "comp";
    inline constexpr const char* drive     = "drive";
    inline constexpr const char* bass      = "bass";
    inline constexpr const char* mid       = "mid";
    inline constexpr const char* treble    = "treble";
    inline constexpr const char* presence  = "presence";
    inline constexpr const char* master    = "master";
    inline constexpr const char* cabOn     = "cabOn";
    inline constexpr const char* chorus    = "chorus";
    inline constexpr const char* dTime     = "dTime";
    inline constexpr const char* dFb       = "dFb";
    inline constexpr const char* dMix      = "dMix";
    inline constexpr const char* rMix      = "rMix";
    inline constexpr const char* tunerOn   = "tunerOn";
    inline constexpr const char* tunerMute = "tunerMute";
    inline constexpr const char* tuning    = "tuning";
    inline constexpr const char* refA      = "refA";
}

class AmpSimAudioProcessor : public juce::AudioProcessor
{
public:
    AmpSimAudioProcessor();
    ~AmpSimAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "GitBassRig"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Cabinet-Impulsantwort (WAV) laden – aufrufen aus dem GUI-Thread
    void loadCabIR (const juce::File& file);
    juce::String getCabName() const { return cabName; }

    // Ringpuffer für den Tuner (Audio-Thread schreibt, GUI-Thread liest)
    static constexpr int ringSize = 32768;          // Zweierpotenz!
    void readRing (float* dest, int n) const;

    juce::AudioProcessorValueTreeState apvts;

    // Eingangs-Spitzenpegel seit dem letzten Auslesen (GUI setzt per exchange(0) zurück)
    std::atomic<float> peakL { 0.f }, peakR { 0.f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void pushToRing (const float* a, const float* b, int n);
    void updateFilters (bool bass, float b, float m, float t, float p);
    float get (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    using Filter     = juce::dsp::IIR::Filter<float>;
    using Coeffs     = juce::dsp::IIR::Coefficients<float>;
    using Duplicator = juce::dsp::ProcessorDuplicator<Filter, Coeffs>;

    juce::dsp::Gain<float> inGain, masterGain;
    juce::dsp::NoiseGate<float> gate;
    juce::dsp::Compressor<float> comp;
    Duplicator preHP, postLP, lowShelf, midPeak, highShelf, presenceShelf, cabHP, cabLP;
    juce::dsp::Oversampling<float> oversampling { 2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR };
    juce::dsp::Convolution cabConv;
    juce::dsp::Chorus<float> chorus;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delay { 1 << 19 };
    juce::dsp::Reverb reverb;

    std::atomic<bool> cabLoaded { false };
    juce::String cabName;
    bool filtersDirty = true;
    float lb = 0, lm = 0, lt = 0, lp = 0;
    bool lBass = false;
    double currentSr = 44100.0;

    std::array<float, ringSize> ring {};
    std::atomic<int> ringPos { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpSimAudioProcessor)
};
