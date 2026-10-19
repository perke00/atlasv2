#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    const juce::StringArray noteNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const juce::StringArray intervalNames { "Off", "Octave Down", "6th Down", "5th Down", "4th Down", "3rd Down",
                                            "3rd Up", "4th Up", "5th Up", "6th Up", "7th Up", "Octave Up" };
    // scale steps for each interval entry; octaves are handled as +-12 semitones
    const int intervalSteps[] = { 0, -12, -5, -4, -3, -2, 2, 3, 4, 5, 6, 12 };
    const juce::StringArray voiceTypes { "Soprano", "Alto", "Tenor", "Bass" };
    const float voiceFormant[] = { 1.18f, 1.07f, 0.93f, 0.82f };
    const float voicePan[] = { -0.55f, 0.55f, -0.25f, 0.25f };

    juce::String vid (const char* base, int i) { return juce::String (base) + juce::String (i + 1); }
}

juce::AudioProcessorValueTreeState::ParameterLayout AtlasProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;

    l.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::key, 1 }, "Key", noteNames, 0));
    StringArray scales;
    for (auto& s : atlas::scaleDefs()) scales.add (s.name);
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::scale, 1 }, "Scale", scales, 1));

    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::retune, 1 }, "Retune Speed",
                                                  NormalisableRange<float> (0.0f, 400.0f, 0.1f, 0.4f), 15.0f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::humanize, 1 }, "Humanize",
                                                  NormalisableRange<float> (0.0f, 100.0f, 0.1f), 20.0f));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::mode, 1 }, "Mode",
                                                   StringArray { "Classic", "Modern", "Auto" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::amount, 1 }, "Pitch Amount",
                                                  NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::formant, 1 }, "Formant",
                                                  NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::mix, 1 }, "Dry / Wet",
                                                  NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::deesser, 1 }, "De-Esser",
                                                  NormalisableRange<float> (0.0f, 100.0f, 0.1f), 40.0f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::comp, 1 }, "Compressor",
                                                  NormalisableRange<float> (0.0f, 100.0f, 0.1f), 30.0f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::eq, 1 }, "EQ",
                                                  NormalisableRange<float> (0.0f, 100.0f, 0.1f), 25.0f));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { ids::lowLat, 1 }, "Low Latency", false));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::outGain, 1 }, "Output",
                                                  NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f));

    l.add (std::make_unique<AudioParameterBool> (ParameterID { ids::harmOn, 1 }, "Harmony", false));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::harmMix, 1 }, "Harmony Mix",
                                                  NormalisableRange<float> (0.0f, 100.0f, 0.1f), 60.0f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::harmSpread, 1 }, "Harmony Spread",
                                                  NormalisableRange<float> (0.0f, 100.0f, 0.1f), 30.0f));

    const int defInt[] = { 6, 8, 0, 0 };   // 3rd up, 5th up, off, off
    const int defType[] = { 1, 0, 2, 3 };  // alto, soprano, tenor, bass
    for (int v = 0; v < numVoices; ++v)
    {
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { vid ("vint", v), 1 }, "Voice " + String (v + 1) + " Interval", intervalNames, defInt[v]));
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { vid ("vtype", v), 1 }, "Voice " + String (v + 1) + " Type", voiceTypes, defType[v]));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { vid ("vlevel", v), 1 }, "Voice " + String (v + 1) + " Level",
                                                      NormalisableRange<float> (0.0f, 100.0f, 0.1f), 70.0f));
    }

    const auto mask = atlas::makeMask (1, 0);
    for (int i = 0; i < 12; ++i)
        l.add (std::make_unique<AudioParameterBool> (ParameterID { "note" + String (i), 1 }, "Note " + noteNames[i], mask[(size_t) i]));

    return l;
}

AtlasProcessor::AtlasProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ATLAS", createLayout())
{
    for (auto& g : graph) g.store (0.0f);

    pKey = apvts.getRawParameterValue (ids::key);        pScale = apvts.getRawParameterValue (ids::scale);
    pRetune = apvts.getRawParameterValue (ids::retune);  pHuman = apvts.getRawParameterValue (ids::humanize);
    pMode = apvts.getRawParameterValue (ids::mode);      pAmount = apvts.getRawParameterValue (ids::amount);
    pFormant = apvts.getRawParameterValue (ids::formant);pMix = apvts.getRawParameterValue (ids::mix);
    pDeess = apvts.getRawParameterValue (ids::deesser);  pComp = apvts.getRawParameterValue (ids::comp);
    pEq = apvts.getRawParameterValue (ids::eq);          pLowLat = apvts.getRawParameterValue (ids::lowLat);
    pOutGain = apvts.getRawParameterValue (ids::outGain);pHarmOn = apvts.getRawParameterValue (ids::harmOn);
    pHarmMix = apvts.getRawParameterValue (ids::harmMix);pHarmSpread = apvts.getRawParameterValue (ids::harmSpread);
    for (int i = 0; i < 12; ++i) pNote[(size_t) i] = apvts.getRawParameterValue ("note" + juce::String (i));
    for (int v = 0; v < numVoices; ++v)
    {
        pVInt[(size_t) v] = apvts.getRawParameterValue (vid ("vint", v));
        pVType[(size_t) v] = apvts.getRawParameterValue (vid ("vtype", v));
        pVLevel[(size_t) v] = apvts.getRawParameterValue (vid ("vlevel", v));
    }
}

AtlasProcessor::~AtlasProcessor()
{
}

// --- key/scale -> note toggles ---------------------------------------------------------------
void AtlasProcessor::setParam (const juce::String& id, float plain)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
}

void AtlasProcessor::applyScaleToNotes()
{
    const auto mask = atlas::makeMask ((int) pScale->load(), (int) pKey->load());
    for (int i = 0; i < 12; ++i) setParam ("note" + juce::String (i), mask[(size_t) i] ? 1.0f : 0.0f);
}

// --- programs ---------------------------------------------------------------------------------
int AtlasProcessor::getNumPrograms() { return 5; }
const juce::String AtlasProcessor::getProgramName (int i)
{
    static const char* names[] = { "Default", "Hard Tune", "Natural Vocal", "Choir Harmony", "Live Low Latency" };
    return names[juce::jlimit (0, 4, i)];
}

void AtlasProcessor::setCurrentProgram (int index)
{
    currentProgram = juce::jlimit (0, 4, index);
    setParam (ids::retune, 15); setParam (ids::humanize, 20); setParam (ids::amount, 100); setParam (ids::mode, 0);
    setParam (ids::formant, 50); setParam (ids::mix, 100); setParam (ids::lowLat, 0); setParam (ids::harmOn, 0);
    switch (currentProgram)
    {
        case 1: setParam (ids::retune, 0); setParam (ids::humanize, 0); break;
        case 2: setParam (ids::retune, 70); setParam (ids::humanize, 55); setParam (ids::amount, 85); setParam (ids::mode, 1); break;
        case 3:
            setParam (ids::retune, 25); setParam (ids::harmOn, 1);
            setParam (vid ("vint", 0), 6); setParam (vid ("vtype", 0), 1);
            setParam (vid ("vint", 1), 8); setParam (vid ("vtype", 1), 0);
            setParam (vid ("vint", 2), 5); setParam (vid ("vtype", 2), 2);
            setParam (vid ("vint", 3), 1); setParam (vid ("vtype", 3), 3);
            break;
        case 4: setParam (ids::lowLat, 1); setParam (ids::retune, 20); setParam (ids::mode, 2); break;
        default: break;
    }
}

// --- state -------------------------------------------------------------------------------------
void AtlasProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void AtlasProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
        }
}

// --- audio -------------------------------------------------------------------------------------
bool AtlasProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto o = l.getMainOutputChannelSet();
    return (o == juce::AudioChannelSet::stereo() || o == juce::AudioChannelSet::mono()) && o == l.getMainInputChannelSet();
}

void AtlasProcessor::configureForMode (bool lowLatency)
{
    currentLowLat = lowLatency;
    const float minHz = lowLatency ? 150.0f : 80.0f;
    detector.configure (minHz, 1000.0f);
    const int maxT = (int) std::ceil (sr / minHz);
    lead.configure (maxT);
    for (auto& h : harm) h.configure (maxT);
    hop = lowLatency ? 48 : 64;
    hopCounter = 0; corrector.reset(); T0 = 0.0f;
    setLatencySamples (lead.latency());
}

void AtlasProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    detector.prepare (sr);
    lead.prepare();
    for (auto& h : harm) h.prepare();
    deEsser.prepare (sr);
    compressor.prepare (sr);
    vocalEq.prepare (sr);
    corrector.reset();
    configureForMode (pLowLat->load() > 0.5f);
}

void AtlasProcessor::analyse()
{
    float hz = 0.0f, conf = 0.0f;
    const bool found = detector.detect (hz, conf);
    const float dt = (float) hop / (float) sr;

    // allowed notes: held MIDI notes win over the scale
    atlas::CorrParams cp;
    bool anyHeld = false;
    for (int n = 0; n < 128; ++n) if (heldNotes[n] > 0) { cp.mask[(size_t) (n % 12)] = true; anyHeld = true; }
    if (! anyHeld)
        for (int i = 0; i < 12; ++i) cp.mask[(size_t) i] = pNote[(size_t) i]->load() > 0.5f;
    cp.retuneMs = pRetune->load();
    cp.humanize = pHuman->load() * 0.01f;
    cp.amount   = pAmount->load() * 0.01f;
    cp.mode     = (int) pMode->load();

    const auto r = corrector.update (found, hz, cp, dt);

    if (r.voiced)
    {
        T0 = (float) sr / r.freq;
        auto periodFor = [this] (float midi) { return (float) sr / (440.0f * std::pow (2.0f, (midi - 69.0f) / 12.0f)); };
        TsynLead = periodFor (r.outMidi);

        const float fb = std::pow (2.0f, (pFormant->load() * 0.01f - 0.5f) * 0.8f);
        formantLead = fb;

        const bool hOn = pHarmOn->load() > 0.5f;
        const float spread = pHarmSpread->load() * 0.01f;
        for (int v = 0; v < numVoices; ++v)
        {
            const int ii = (int) pVInt[(size_t) v]->load();
            const bool act = hOn && ii > 0 && pVLevel[(size_t) v]->load() > 0.5f;
            voiceActive[(size_t) v] = act;
            if (! act) continue;

            float off;
            if (ii == 1) off = -12.0f; else if (ii == 11) off = 12.0f;
            else off = (float) (atlas::stepInScale (r.target, intervalSteps[ii], cp.mask) - r.target);
            off += ((v % 2) ? 1.0f : -1.0f) * spread * 0.14f * (1.0f + 0.35f * (float) v);   // tiny detune = choir width
            TsynV[(size_t) v] = periodFor (r.outMidi + off);
            formantV[(size_t) v] = juce::jlimit (0.7f, 1.4f, fb * voiceFormant[juce::jlimit (0, 3, (int) pVType[(size_t) v]->load())]);
        }

        detectedMidi.store (r.detMidi);
        targetMidi.store ((float) r.target);
    }
    else if (! r.holding)
    {
        T0 = 0.0f;
        detectedMidi.store (-1.0f); targetMidi.store (-1.0f);
    }

    const int gp = graphPos.load();
    graph[(size_t) gp].store (juce::jlimit (-12.0f, 12.0f, corrector.correction()));
    graphPos.store ((gp + 1) % graphSize);
}

void AtlasProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = juce::jmin (buffer.getNumChannels(), 2);
    const int n = buffer.getNumSamples();
    for (int c = numCh; c < getTotalNumOutputChannels(); ++c) buffer.clear (c, 0, n);

    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn()) heldNotes[msg.getNoteNumber() & 127] = 1;
        else if (msg.isNoteOff()) heldNotes[msg.getNoteNumber() & 127] = 0;
        else if (msg.isAllNotesOff() || msg.isAllSoundOff()) std::fill (std::begin (heldNotes), std::end (heldNotes), 0);
    }

    const bool wantLow = pLowLat->load() > 0.5f;
    if (wantLow != currentLowLat) configureForMode (wantLow);

    const float wet = pMix->load() * 0.01f;
    const float deess = pDeess->load() * 0.01f, comp = pComp->load() * 0.01f, eq = pEq->load() * 0.01f;
    const float outGain = juce::Decibels::decibelsToGain (pOutGain->load());
    const bool hOn = pHarmOn->load() > 0.5f;
    const float hMix = pHarmMix->load() * 0.01f;
    const float leadGain = hOn ? 1.0f : 1.0f;
    vocalEq.update (eq);

    for (int v = 0; v < numVoices; ++v)
    {
        const float pan = voicePan[v];
        const float ang = (pan * 0.5f + 0.5f) * juce::MathConstants<float>::halfPi;
        const float lvl = pVLevel[(size_t) v]->load() * 0.01f * hMix * 0.8f;
        voiceGainL[(size_t) v] = std::cos (ang) * lvl * 1.414f;
        voiceGainR[(size_t) v] = std::sin (ang) * lvl * 1.414f;
    }

    auto* l = buffer.getWritePointer (0);
    auto* r = numCh > 1 ? buffer.getWritePointer (1) : nullptr;
    float inPeak = 0.0f, outPeak = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        const float in = r ? 0.5f * (l[i] + r[i]) : l[i];
        inPeak = juce::jmax (inPeak, std::fabs (in));

        detector.push (in);
        if (++hopCounter >= hop) { hopCounter = 0; analyse(); }

        const float x = deEsser.process (in, deess);
        const float lv = lead.process (x, T0, TsynLead, formantLead, true, true);
        const float dry = lead.dryDelayed();

        float hl = 0.0f, hr = 0.0f;
        for (int v = 0; v < numVoices; ++v)
        {
            const bool act = hOn && voiceActive[(size_t) v];
            const float y = harm[(size_t) v].process (x, T0, TsynV[(size_t) v], formantV[(size_t) v] > 0.0f ? formantV[(size_t) v] : 1.0f, false, act);
            hl += y * voiceGainL[(size_t) v];
            hr += y * voiceGainR[(size_t) v];
        }

        float outL = dry * (1.0f - wet) + (lv * leadGain + hl) * wet;
        float outR = dry * (1.0f - wet) + (lv * leadGain + hr) * wet;

        outL = vocalEq.process (0, outL, eq);
        outR = vocalEq.process (1, outR, eq);
        compressor.process (outL, outR, comp);
        outL *= outGain; outR *= outGain;

        outPeak = juce::jmax (outPeak, std::fabs (outL), std::fabs (outR));
        l[i] = outL;
        if (r) r[i] = outR;
    }

    const float inDb = juce::Decibels::gainToDecibels (inPeak, -100.0f), outDb = juce::Decibels::gainToDecibels (outPeak, -100.0f);
    inLevelDb.store (juce::jmax (inDb, inLevelDb.load() - 1.5f));
    outLevelDb.store (juce::jmax (outDb, outLevelDb.load() - 1.5f));
}

juce::AudioProcessorEditor* AtlasProcessor::createEditor() { return new AtlasEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AtlasProcessor(); }
