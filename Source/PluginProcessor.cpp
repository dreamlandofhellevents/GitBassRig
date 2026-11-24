#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

AmpSimAudioProcessor::AmpSimAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  AudioChannelSet::stereo(), true)
                        .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
}

AudioProcessorValueTreeState::ParameterLayout AmpSimAudioProcessor::createLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto range = [] (float lo, float hi, float step = 0.01f) { return NormalisableRange<float> (lo, hi, step); };
    auto flt = [&] (const char* id, const String& name, NormalisableRange<float> r, float def)
    {
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def));
    };

    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { PID::mode, 1 }, "Modus",
                                                         StringArray { "Gitarre", "Bass" }, 0));
    flt (PID::inGain,   "Input (dB)",    range (-24.f, 24.f, 0.1f), 0.f);
    flt (PID::gate,     "Gate (dB)",     range (-80.f, -20.f, 0.1f), -80.f);   // -80 = aus
    flt (PID::comp,     "Kompressor",    range (0.f, 1.f), 0.f);
    flt (PID::drive,    "Drive",         range (0.f, 1.f), 0.4f);
    flt (PID::bass,     "Bass",          range (0.f, 1.f), 0.5f);
    flt (PID::mid,      "Mitten",        range (0.f, 1.f), 0.5f);
    flt (PID::treble,   String::fromUTF8 ("Höhen"),         range (0.f, 1.f), 0.5f);
    flt (PID::presence, "Presence",      range (0.f, 1.f), 0.3f);
    flt (PID::master,   "Master (dB)",   range (-36.f, 6.f, 0.1f), -6.f);
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { PID::cabOn, 1 }, "Cabinet", true));
    flt (PID::chorus,   "Chorus",        range (0.f, 1.f), 0.f);
    flt (PID::dTime,    "Delay Zeit (ms)", range (20.f, 1000.f, 1.f), 350.f);
    flt (PID::dFb,      "Delay Feedback", range (0.f, 0.9f), 0.35f);
    flt (PID::dMix,     "Delay Mix",     range (0.f, 1.f), 0.f);
    flt (PID::rMix,     "Hall Mix",      range (0.f, 1.f), 0.f);

    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { PID::tunerOn, 1 },   "Tuner", false));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { PID::tunerMute, 1 }, "Tuner stumm", true));
    StringArray names;
    for (auto& t : getTunings()) names.add (t.name);
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { PID::tuning, 1 }, "Stimmung", names, 0));
    flt (PID::refA, "Kammerton A (Hz)", range (415.f, 466.f, 0.1f), 440.f);

    return { p.begin(), p.end() };
}

bool AmpSimAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    const auto in  = l.getMainInputChannelSet();
    if (out != AudioChannelSet::mono() && out != AudioChannelSet::stereo())
        return false;
    return in == out || (in == AudioChannelSet::mono() && out == AudioChannelSet::stereo());
}

void AmpSimAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSr = sampleRate;
    dsp::ProcessSpec spec { sampleRate, (uint32) samplesPerBlock, (uint32) getTotalNumOutputChannels() };

    inGain.prepare (spec);     inGain.setRampDurationSeconds (0.02);
    masterGain.prepare (spec); masterGain.setRampDurationSeconds (0.02);
    gate.prepare (spec);       gate.setRatio (6.f); gate.setAttack (1.f); gate.setRelease (80.f);
    comp.prepare (spec);       comp.setAttack (8.f); comp.setRelease (120.f);
    for (auto* f : { &preHP, &postLP, &lowShelf, &midPeak, &highShelf, &presenceShelf, &cabHP, &cabLP })
        f->prepare (spec);
    oversampling.initProcessing ((size_t) samplesPerBlock);
    cabConv.prepare (spec);
    chorus.prepare (spec);     chorus.setRate (0.8f); chorus.setDepth (0.3f);
                               chorus.setCentreDelay (7.f); chorus.setFeedback (0.f);
    delay.prepare (spec);
    reverb.prepare (spec);

    setLatencySamples ((int) std::ceil (oversampling.getLatencyInSamples()));
    filtersDirty = true;
}

void AmpSimAudioProcessor::updateFilters (bool bass, float b, float m, float t, float pr)
{
    if (! filtersDirty && bass == lBass && b == lb && m == lm && t == lt && pr == lp)
        return;

    filtersDirty = false; lBass = bass; lb = b; lm = m; lt = t; lp = pr;
    const double sr = currentSr;
    auto g = [] (float db) { return Decibels::decibelsToGain (db); };

    // Hinweis: makeXxx() alloziert – für ein Release besser Koeffizienten vorberechnen / glätten.
    *preHP.state         = *Coeffs::makeHighPass  (sr, bass ? 30.f : 90.f);
    *postLP.state        = *Coeffs::makeLowPass   (sr, bass ? 6000.f : 7500.f);
    *lowShelf.state      = *Coeffs::makeLowShelf  (sr, bass ? 70.f : 120.f,  0.7f, g ((b - 0.5f) * 24.f));
    *midPeak.state       = *Coeffs::makePeakFilter(sr, bass ? 450.f : 650.f, 0.8f, g ((m - 0.5f) * 24.f));
    *highShelf.state     = *Coeffs::makeHighShelf (sr, bass ? 2500.f : 3000.f, 0.7f, g ((t - 0.5f) * 24.f));
    *presenceShelf.state = *Coeffs::makeHighShelf (sr, 5500.f, 0.7f, g (pr * 9.f));
    // einfache Cab-Näherung, falls keine IR geladen ist
    *cabHP.state         = *Coeffs::makeHighPass  (sr, bass ? 40.f : 80.f);
    *cabLP.state         = *Coeffs::makeLowPass   (sr, bass ? 3500.f : 5000.f);
}

void AmpSimAudioProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;
    const int ns   = buffer.getNumSamples();
    const int nIn  = getTotalNumInputChannels();
    const int nOut = getTotalNumOutputChannels();

    for (int ch = nIn; ch < nOut; ++ch) buffer.clear (ch, 0, ns);
    if (nIn == 1 && nOut == 2) buffer.copyFrom (1, 0, buffer, 0, 0, ns);

    // Tuner bekommt das rohe Eingangssignal (beide Kanäle addiert, damit der Eingangskanal egal ist)
    const float* inL = buffer.getReadPointer (0);
    const float* inR = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : nullptr;
    float pl = 0.f, pr = 0.f;
    for (int i = 0; i < ns; ++i)
    {
        pl = jmax (pl, std::abs (inL[i]));
        if (inR != nullptr) pr = jmax (pr, std::abs (inR[i]));
    }
    peakL.store (jmax (peakL.load(), pl));
    peakR.store (jmax (peakR.load(), pr));
    pushToRing (inL, inR, ns);
    if (get (PID::tunerOn) > 0.5f && get (PID::tunerMute) > 0.5f)
    {
        buffer.clear();
        return;
    }

    const bool bassMode = get (PID::mode) > 0.5f;
    updateFilters (bassMode, get (PID::bass), get (PID::mid), get (PID::treble), get (PID::presence));

    dsp::AudioBlock<float> block (buffer);
    dsp::ProcessContextReplacing<float> ctx (block);

    // --- Eingang, Gate, Kompressor ---
    inGain.setGainDecibels (get (PID::inGain));
    inGain.process (ctx);

    if (get (PID::gate) > -79.9f) { gate.setThreshold (get (PID::gate)); gate.process (ctx); }

    const float c = get (PID::comp);
    if (c > 0.01f)
    {
        comp.setThreshold (-6.f - 24.f * c);
        comp.setRatio (2.f + 4.f * c);
        comp.process (ctx);
    }

    // --- Preamp / Verzerrung (4x Oversampling gegen Aliasing) ---
    const float drive = get (PID::drive);
    if (drive > 0.001f)
    {
        preHP.process (ctx);
        auto up = oversampling.processSamplesUp (block);
        const float g     = Decibels::decibelsToGain (drive * 36.f + 3.f);
        const float blend = bassMode ? 0.55f : 1.f;     // Bass: Clean-Anteil für die Tiefen behalten
        const float bias  = 0.12f;                      // leichte Asymmetrie = geradzahlige Obertöne
        const float bt    = std::tanh (bias);
        for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
        {
            auto* d = up.getChannelPointer (ch);
            for (size_t i = 0; i < up.getNumSamples(); ++i)
            {
                const float x = d[i];
                const float y = 0.7f * (std::tanh (g * x + bias) - bt);
                d[i] = x * (1.f - blend) + y * blend;
            }
        }
        oversampling.processSamplesDown (block);
        postLP.process (ctx);
    }

    // --- Klangregelung ---
    lowShelf.process (ctx);
    midPeak.process (ctx);
    highShelf.process (ctx);
    presenceShelf.process (ctx);

    // --- Cabinet ---
    if (get (PID::cabOn) > 0.5f)
    {
        if (cabLoaded.load()) cabConv.process (ctx);
        else { cabHP.process (ctx); cabLP.process (ctx); }
    }

    // --- Modulation / Zeiteffekte ---
    const float chMix = get (PID::chorus);
    if (chMix > 0.001f) { chorus.setMix (chMix * 0.6f); chorus.process (ctx); }

    const float dMix = get (PID::dMix);
    if (dMix > 0.001f)
    {
        const float dSamples = get (PID::dTime) * 0.001f * (float) currentSr;
        const float fb = get (PID::dFb);
        for (size_t ch = 0; ch < block.getNumChannels(); ++ch)
        {
            auto* d = block.getChannelPointer (ch);
            for (size_t i = 0; i < block.getNumSamples(); ++i)
            {
                const float in  = d[i];
                const float del = delay.popSample ((int) ch, dSamples);
                delay.pushSample ((int) ch, in + del * fb);
                d[i] = in + del * dMix;
            }
        }
    }

    const float rMix = get (PID::rMix);
    if (rMix > 0.001f)
    {
        dsp::Reverb::Parameters rp;
        rp.roomSize = 0.55f; rp.damping = 0.5f; rp.width = 1.f;
        rp.wetLevel = rMix * 0.4f; rp.dryLevel = 1.f; rp.freezeMode = 0.f;
        reverb.setParameters (rp);
        reverb.process (ctx);
    }

    // --- Master ---
    masterGain.setGainDecibels (get (PID::master));
    masterGain.process (ctx);
}

void AmpSimAudioProcessor::loadCabIR (const File& f)
{
    if (! f.existsAsFile()) return;
    cabConv.loadImpulseResponse (f, dsp::Convolution::Stereo::no, dsp::Convolution::Trim::yes,
                                 0, dsp::Convolution::Normalise::yes);
    cabLoaded = true;
    cabName = f.getFileName();
    apvts.state.setProperty ("cabPath", f.getFullPathName(), nullptr);
}

void AmpSimAudioProcessor::pushToRing (const float* a, const float* b, int n)
{
    int p = ringPos.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
        ring[(size_t) ((p + i) & (ringSize - 1))] = a[i] + (b != nullptr ? b[i] : 0.f);
    ringPos.store ((p + n) & (ringSize - 1), std::memory_order_release);
}

void AmpSimAudioProcessor::readRing (float* dest, int n) const
{
    n = jmin (n, ringSize);
    const int p = ringPos.load (std::memory_order_acquire);
    const int start = (p - n) & (ringSize - 1);
    for (int i = 0; i < n; ++i)
        dest[i] = ring[(size_t) ((start + i) & (ringSize - 1))];
}

void AmpSimAudioProcessor::getStateInformation (MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void AmpSimAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (ValueTree::fromXml (*xml));
            const auto path = apvts.state.getProperty ("cabPath").toString();
            if (path.isNotEmpty()) loadCabIR (File (path));
        }
}

AudioProcessorEditor* AmpSimAudioProcessor::createEditor()
{
    return new AmpSimAudioProcessorEditor (*this);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AmpSimAudioProcessor();
}
