#pragma once
#include "PluginProcessor.h"

class AmpSimAudioProcessorEditor : public juce::AudioProcessorEditor,
                                   private juce::Timer
{
public:
    explicit AmpSimAudioProcessorEditor (AmpSimAudioProcessor&);
    ~AmpSimAudioProcessorEditor() override { stopTimer(); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAtt> att;      // zuletzt deklariert = zuerst zerstört
    };

    void addKnob (Knob&, const juce::String& text, const char* paramID);
    void timerCallback() override;

    AmpSimAudioProcessor& proc;

    Knob inK, gateK, compK, driveK, bassK, midK, trebleK, presK, masterK,
         chorusK, dTimeK, dFbK, dMixK, rMixK, refK;
    std::vector<Knob*> gridKnobs;

    juce::ComboBox modeBox, tuningBox;
    juce::ToggleButton cabBtn { "Cabinet" }, tunerBtn { "Tuner an" }, muteBtn { "Ausgang stumm" };
    juce::TextButton loadIrBtn { "Cab-IR laden..." };
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<ComboAtt> modeAtt, tuningAtt;
    std::unique_ptr<ButtonAtt> cabAtt, tunerAtt, muteAtt;

    juce::Rectangle<int> tunerArea;
    std::vector<float> rawBuf, decBuf;
    bool hasSignal = false;
    float centsSmooth = 0.f, detectedHz = 0.f, targetHz = 0.f;
    float levelDbL = -100.f, levelDbR = -100.f;
    std::vector<float> centsHist;
    int lastNote = -1, missCount = 0;
    juce::String noteText;
};
