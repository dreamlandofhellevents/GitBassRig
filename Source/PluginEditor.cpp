#include "PluginEditor.h"
#include "PitchDetector.h"
#include <algorithm>

using namespace juce;

static StringArray choicesOf (AudioProcessorValueTreeState& a, const char* id)
{
    return dynamic_cast<AudioParameterChoice*> (a.getParameter (id))->choices;
}

void AmpSimAudioProcessorEditor::addKnob (Knob& k, const String& text, const char* id)
{
    k.slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (Slider::TextBoxBelow, false, 70, 16);
    addAndMakeVisible (k.slider);
    k.label.setText (text, dontSendNotification);
    k.label.setJustificationType (Justification::centred);
    k.label.attachToComponent (&k.slider, false);
    k.att = std::make_unique<SliderAtt> (proc.apvts, id, k.slider);
}

AmpSimAudioProcessorEditor::AmpSimAudioProcessorEditor (AmpSimAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    struct Def { Knob* k; String text; const char* id; };
    const Def defs[] = {
        { &inK, "Input", PID::inGain },   { &gateK, "Gate", PID::gate },       { &compK, "Kompressor", PID::comp },
        { &driveK, "Drive", PID::drive }, { &masterK, "Master", PID::master },
        { &bassK, "Bass", PID::bass },    { &midK, "Mitten", PID::mid },       { &trebleK, String::fromUTF8 ("Höhen"), PID::treble },
        { &presK, "Presence", PID::presence }, { &chorusK, "Chorus", PID::chorus },
        { &dTimeK, "Delay ms", PID::dTime }, { &dFbK, "Feedback", PID::dFb },  { &dMixK, "Delay Mix", PID::dMix },
        { &rMixK, "Hall Mix", PID::rMix }
    };
    for (auto& d : defs) { addKnob (*d.k, d.text, d.id); gridKnobs.push_back (d.k); }

    addKnob (refK, "Kammerton A (Hz)", PID::refA);
    refK.slider.setSliderStyle (Slider::LinearHorizontal);
    refK.slider.setTextBoxStyle (Slider::TextBoxRight, false, 60, 20);

    modeBox.addItemList (choicesOf (proc.apvts, PID::mode), 1);
    tuningBox.addItemList (choicesOf (proc.apvts, PID::tuning), 1);
    for (Component* c : std::initializer_list<Component*> { &modeBox, &tuningBox, &cabBtn, &tunerBtn, &muteBtn, &loadIrBtn })
        addAndMakeVisible (c);

    modeAtt   = std::make_unique<ComboAtt>  (proc.apvts, PID::mode, modeBox);
    tuningAtt = std::make_unique<ComboAtt>  (proc.apvts, PID::tuning, tuningBox);
    cabAtt    = std::make_unique<ButtonAtt> (proc.apvts, PID::cabOn, cabBtn);
    tunerAtt  = std::make_unique<ButtonAtt> (proc.apvts, PID::tunerOn, tunerBtn);
    muteAtt   = std::make_unique<ButtonAtt> (proc.apvts, PID::tunerMute, muteBtn);

    loadIrBtn.onClick = [this]
    {
        chooser = std::make_unique<FileChooser> (String::fromUTF8 ("Cabinet-Impulsantwort wählen"), File(), "*.wav;*.aif;*.aiff");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                              [this] (const FileChooser& fc)
                              {
                                  if (fc.getResult().existsAsFile())
                                  {
                                      proc.loadCabIR (fc.getResult());
                                      loadIrBtn.setButtonText (proc.getCabName());
                                  }
                              });
    };
    if (proc.getCabName().isNotEmpty()) loadIrBtn.setButtonText (proc.getCabName());

    startTimerHz (15);

    // Erst ganz am Ende: setSize ruft resized() auf, und dafür müssen alle Regler schon registriert sein
    setSize (780, 580);
}

void AmpSimAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto top = r.removeFromTop (180);

    auto right = top.removeFromRight (240);
    tunerArea = top.withTrimmedRight (10);
    tunerBtn.setBounds (right.removeFromTop (26));
    muteBtn.setBounds (right.removeFromTop (26));
    right.removeFromTop (4);
    tuningBox.setBounds (right.removeFromTop (28));
    right.removeFromTop (22);
    refK.slider.setBounds (right.removeFromTop (26));

    r.removeFromTop (10);
    auto row = r.removeFromTop (30);
    modeBox.setBounds (row.removeFromLeft (150));
    row.removeFromLeft (10);
    cabBtn.setBounds (row.removeFromLeft (100));
    loadIrBtn.setBounds (row.removeFromLeft (200));
    r.removeFromTop (8);

    const int cols = 5, rows = 3;
    const int cw = r.getWidth() / cols, ch = r.getHeight() / rows;
    for (size_t i = 0; i < gridKnobs.size(); ++i)
    {
        auto cell = Rectangle<int> (r.getX() + (int) (i % cols) * cw,
                                    r.getY() + (int) (i / cols) * ch, cw, ch).reduced (8);
        cell.removeFromTop (20);                     // Platz für das Label
        gridKnobs[i]->slider.setBounds (cell);
    }
}

void AmpSimAudioProcessorEditor::timerCallback()
{
    const bool on = proc.apvts.getRawParameterValue (PID::tunerOn)->load() > 0.5f;
    if (! on) { if (hasSignal) { hasSignal = false; } repaint (tunerArea); return; }

    auto toDb = [] (float gain) { return gain > 1.0e-5f ? 20.f * std::log10 (gain) : -100.f; };
    levelDbL = toDb (proc.peakL.exchange (0.f));
    levelDbR = toDb (proc.peakR.exchange (0.f));

    const bool bass = proc.apvts.getRawParameterValue (PID::mode)->load() > 0.5f;
    const double sr = proc.getSampleRate();
    if (sr <= 0.0) return;

    // Auf ~12 kHz herunterteilen (Mittelwert über D Samples) -> YIN bleibt auch bei 96/192 kHz billig
    const int D = jmax (1, (int) (sr / 12000.0));
    const double effSr = sr / D;
    const float minHz = bass ? 25.f : 60.f, maxHz = bass ? 400.f : 600.f;
    const int tauMax = (int) (effSr / minHz);
    const int w = jmax (1536, tauMax);      // längeres Fenster = ruhigere tiefe Saiten
    const int n = tauMax + w;

    rawBuf.resize ((size_t) n * (size_t) D);
    decBuf.resize ((size_t) n);
    proc.readRing (rawBuf.data(), n * D);

    double energy = 0.0;
    for (int i = 0; i < n; ++i)
    {
        float s = 0.f;
        for (int k = 0; k < D; ++k) s += rawBuf[(size_t) (i * D + k)];
        decBuf[(size_t) i] = s / (float) D;
        energy += (double) decBuf[(size_t) i] * decBuf[(size_t) i];
    }

    float f = -1.f;
    if (std::sqrt (energy / n) > 0.0015)             // Pegel-Schwelle ~ -56 dBFS
        f = PitchDetector::detect (decBuf.data(), n, effSr, minHz, maxHz);

    if (f <= 0.f)
    {
        // kurze Aussetzer (z. B. beim Ausklingen tiefer Saiten) überbrücken statt sofort "kein Signal" zu zeigen
        if (hasSignal && ++missCount > 8) { hasSignal = false; centsHist.clear(); lastNote = -1; }
        repaint (tunerArea);
        return;
    }
    missCount = 0;

    const auto& tunings = getTunings();
    const int idx = jlimit (0, (int) tunings.size() - 1, (int) proc.apvts.getRawParameterValue (PID::tuning)->load());
    const double refA = proc.apvts.getRawParameterValue (PID::refA)->load();

    // Oktavfehler abfangen: Kandidaten f, f/2 und f*2. Die Originalfrequenz wird bevorzugt,
    // ein Oktav-Kandidat gewinnt nur, wenn er einer Saite deutlich näher liegt (tiefe Saiten haben starke Obertöne).
    double bestCents = 1e9, bestHz = 0, bestScore = 1e9; int bestNote = 0;
    for (double cand : { (double) f, (double) f * 0.5, (double) f * 2.0 })
    {
        const double penalty = (cand == (double) f) ? 0.0 : 30.0;
        for (int midi : tunings[(size_t) idx].midi)
        {
            const double t = midiToHz (midi, refA);
            const double cents = 1200.0 * std::log2 (cand / t);
            const double score = std::abs (cents) + penalty;
            if (score < bestScore) { bestScore = score; bestCents = cents; bestNote = midi; bestHz = t; }
        }
    }

    // Median der letzten 5 Messungen gegen Ausreißer, danach leichte Glättung
    if (bestNote != lastNote) { centsHist.clear(); lastNote = bestNote; }
    centsHist.push_back ((float) bestCents);
    if (centsHist.size() > 5) centsHist.erase (centsHist.begin());
    auto sorted = centsHist;
    std::sort (sorted.begin(), sorted.end());
    const float med = sorted[sorted.size() / 2];

    centsSmooth = hasSignal ? 0.5f * centsSmooth + 0.5f * med : med;
    hasSignal = true;
    detectedHz = (float) (bestHz * std::pow (2.0, bestCents / 1200.0));
    targetHz = (float) bestHz;
    noteText = MidiMessage::getMidiNoteName (bestNote, true, true, 4);
    repaint (tunerArea);
}

void AmpSimAudioProcessorEditor::paint (Graphics& g)
{
    g.fillAll (Colour (0xff15181d));
    auto area = tunerArea.toFloat();
    g.setColour (Colour (0xff1f242b));
    g.fillRoundedRectangle (area, 8.f);

    const bool on = proc.apvts.getRawParameterValue (PID::tunerOn)->load() > 0.5f;
    g.setColour (Colours::white.withAlpha (0.55f));
    g.setFont (14.f);

    if (! on)       { g.drawFittedText ("Tuner aus", tunerArea, Justification::centred, 1); return; }
    if (! hasSignal)
    {
        auto a = tunerArea.reduced (14);
        g.drawFittedText ("Spiele eine Saite an ...", a.removeFromTop (a.getHeight() / 2), Justification::centred, 1);

        String lvl = String::formatted ("Eingang  L: %.0f dB", levelDbL);
        if (proc.getTotalNumInputChannels() > 1) lvl << String::formatted ("   R: %.0f dB", levelDbR);
        g.setFont (13.f);
        g.drawFittedText (lvl, a.removeFromTop (24), Justification::centred, 1);

        if (jmax (levelDbL, levelDbR) < -80.f)
        {
            g.setColour (Colour (0xffffcc00));
            g.drawFittedText (String::fromUTF8 ("Kein Signal am Eingang. Standalone: Options > Audio/MIDI Settings, \"Mute audio input\" ausschalten."),
                              a, Justification::centredTop, 3);
        }
        return;
    }

    const bool inTune = std::abs (centsSmooth) < 5.f;
    const Colour col = inTune ? Colour (0xff4cd964) : (std::abs (centsSmooth) < 20.f ? Colour (0xffffcc00) : Colour (0xffff5a4d));

    auto a = tunerArea.reduced (14);
    g.setColour (col);
    g.setFont (Font (FontOptions (54.f, Font::bold)));
    g.drawText (noteText, a.removeFromTop (62), Justification::centred);

    g.setColour (Colours::white.withAlpha (0.7f));
    g.setFont (13.f);
    g.drawText (String::formatted ("Soll %.2f Hz   Ist %.2f Hz   %+.0f Cent", targetHz, detectedHz, centsSmooth),
                a.removeFromTop (20), Justification::centred);

    auto bar = a.removeFromBottom (40).toFloat().reduced (6.f, 8.f);
    g.setColour (Colours::white.withAlpha (0.15f));
    g.fillRoundedRectangle (bar, 4.f);
    g.setColour (Colours::white.withAlpha (0.5f));
    g.fillRect (bar.getCentreX() - 1.f, bar.getY() - 4.f, 2.f, bar.getHeight() + 8.f);
    const float x = bar.getCentreX() + jlimit (-50.f, 50.f, centsSmooth) / 50.f * (bar.getWidth() * 0.5f);
    g.setColour (col);
    g.fillRoundedRectangle (x - 4.f, bar.getY() - 2.f, 8.f, bar.getHeight() + 4.f, 3.f);
}
