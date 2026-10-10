#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

namespace
{
    const float kDefFreq[kNumBands] = { 700.f, 1220.f, 2600.f, 3300.f, 4500.f };   // roughly an "ah"
    const float kDefGain[kNumBands] = { 14.f, 11.f, 8.f, 6.f, 4.f };
    const double kAutoBeats[]       = { 0.0, 1.0, 2.0, 4.0, 8.0 };
    const double kRateBeats[]       = { 0.25, 0.5, 1.0, 2.0, 4.0 };   // 1/16, 1/8, 1/4, 1/2, 1 bar
    constexpr int kChunk = 16;   // coefficient update interval in samples

    // Starting content for the 8 sequencer steps (F1..F5 of a few vowels)
    const float kVowels[kMaxSteps][kNumBands] = {
        { 700.f, 1220.f, 2600.f, 3300.f, 4500.f },   // a
        { 270.f, 2290.f, 3010.f, 3500.f, 4500.f },   // i
        { 300.f,  870.f, 2240.f, 3300.f, 4500.f },   // u
        { 530.f, 1840.f, 2480.f, 3300.f, 4500.f },   // e
        { 570.f,  840.f, 2410.f, 3300.f, 4500.f },   // o
        { 660.f, 1700.f, 2400.f, 3300.f, 4500.f },
        { 400.f, 1000.f, 2400.f, 3300.f, 4500.f },
        { 350.f, 2000.f, 2800.f, 3300.f, 4500.f } };

    juce::NormalisableRange<float> logRange (float lo, float hi)
    {
        return { lo, hi,
                 [] (float s, float e, float p) { return s * std::pow (e / s, p); },
                 [] (float s, float e, float v) { return std::log (v / s) / std::log (e / s); },
                 [] (float, float, float v)     { return v; } };
    }

    float softClip (float x)
    {
        const float a = std::abs (x);
        if (a <= 0.9f) return x;
        return std::copysign (0.9f + 0.1f * std::tanh ((a - 0.9f) / 0.1f), x);
    }

    void setParam (juce::RangedAudioParameter* p, float v)
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (v));
        p->endChangeGesture();
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout FormantRouletteAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    for (int b = 0; b < kNumBands; ++b)
    {
        const auto n = String (b + 1);
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { bandId ("f", b), 1 },
                    "Band " + n + " Freq", logRange (20.f, 20000.f), kDefFreq[b]));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { bandId ("g", b), 1 },
                    "Band " + n + " Gain", NormalisableRange<float> (-24.f, 24.f, 0.01f), kDefGain[b]));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { bandId ("q", b), 1 },
                    "Band " + n + " Q", logRange (0.3f, 20.f), 8.f));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { bandId ("l", b), 1 },
                    "Band " + n + " Lock", false));
    }

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 }, "Mix",
        NormalisableRange<float> (0.f, 1.f, 0.001f), 1.f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.f)) + " %"; })));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "out", 1 }, "Output",
        NormalisableRange<float> (-24.f, 12.f, 0.1f), 0.f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "glide", 1 }, "Glide",
        NormalisableRange<float> (1.f, 2000.f, 0.f, 0.35f), 150.f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " ms"; })));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "chaos", 1 }, "Chaos",
        NormalisableRange<float> (0.f, 1.f, 0.001f), 1.f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.f)) + " %"; })));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Mode",
                StringArray { "Vowel", "Wild" }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "auto", 1 }, "Auto",
                StringArray { "Off", "1 beat", "2 beats", "1 bar", "2 bars" }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "topo", 1 }, "Filter",
                StringArray { "Serial Peaks", "Parallel Bands" }, 0));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "seq", 1 }, "Sequencer", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "seqrate", 1 }, "Step Rate",
                StringArray { "1/16", "1/8", "1/4", "1/2", "1 bar" }, 1));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "seqsteps", 1 }, "Steps",
                StringArray { "2 steps", "3 steps", "4 steps", "5 steps", "6 steps", "7 steps", "8 steps" }, 2));

    return layout;
}

//==============================================================================
FormantRouletteAudioProcessor::FormantRouletteAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    for (int b = 0; b < kNumBands; ++b)
    {
        fPtr[b]    = apvts.getRawParameterValue (bandId ("f", b));
        gPtr[b]    = apvts.getRawParameterValue (bandId ("g", b));
        qPtr[b]    = apvts.getRawParameterValue (bandId ("q", b));
        lockPtr[b] = apvts.getRawParameterValue (bandId ("l", b));
        liveFreq[(size_t) b].store (kDefFreq[b]);
        liveGain[(size_t) b].store (kDefGain[b]);
        liveQ[(size_t) b].store (8.f);
        curLogF[b] = std::log2 (kDefFreq[b]);
        curGain[b] = kDefGain[b];
        curLogQ[b] = std::log2 (8.f);
    }
    mixPtr   = apvts.getRawParameterValue ("mix");
    outPtr   = apvts.getRawParameterValue ("out");
    glidePtr = apvts.getRawParameterValue ("glide");
    chaosPtr = apvts.getRawParameterValue ("chaos");
    modePtr  = apvts.getRawParameterValue ("mode");
    autoPtr  = apvts.getRawParameterValue ("auto");
    topoPtr  = apvts.getRawParameterValue ("topo");
    seqPtr   = apvts.getRawParameterValue ("seq");
    ratePtr  = apvts.getRawParameterValue ("seqrate");
    stepsPtr = apvts.getRawParameterValue ("seqsteps");

    for (int s = 0; s < kMaxSteps; ++s)
        for (int b = 0; b < kNumBands; ++b)
        {
            setStepValue (s, b, 0, kVowels[s][b]);
            setStepValue (s, b, 1, kDefGain[b]);
            setStepValue (s, b, 2, 8.f);
        }
}

bool FormantRouletteAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto o = l.getMainOutputChannelSet();
    return (o == juce::AudioChannelSet::mono() || o == juce::AudioChannelSet::stereo())
           && o == l.getMainInputChannelSet();
}

void FormantRouletteAudioProcessor::snapToParams()
{
    for (int b = 0; b < kNumBands; ++b)
    {
        curLogF[b] = std::log2 (fPtr[b]->load());
        curGain[b] = gPtr[b]->load();
        curLogQ[b] = std::log2 (qPtr[b]->load());
    }
}

void FormantRouletteAudioProcessor::prepareToPlay (double sr, int)
{
    sampleRate = sr;
    for (auto& ch : filt) for (auto& f : ch) f.reset();
    snapToParams();
    mixSm.reset (sr, 0.02);  mixSm.setCurrentAndTargetValue (mixPtr->load());
    outSm.reset (sr, 0.02);  outSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outPtr->load()));
    autoCounter = 0.0;
    seqBeats = 0.0;
    lastParallel = topoPtr->load() > 0.5f;
    fifoIndex = 0;
    blockReady = false;
}

//==============================================================================
void FormantRouletteAudioProcessor::runAutoRandomise (int numSamples)
{
    const int idx = (int) autoPtr->load();
    if (idx <= 0) { autoCounter = 0.0; return; }

    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bp = pos->getBpm())
                bpm = *bp;

    const double interval = kAutoBeats[juce::jlimit (0, 4, idx)] * 60.0 / juce::jmax (20.0, bpm) * sampleRate;
    autoCounter += numSamples;
    if (autoCounter >= interval)
    {
        autoCounter = std::fmod (autoCounter, interval);
        triggerAsyncUpdate();   // randomising runs on the message thread
    }
}

void FormantRouletteAudioProcessor::pushSpectrumSample (float s)
{
    fifo[(size_t) fifoIndex++] = s;
    if (fifoIndex == fftSize)
    {
        if (! blockReady.load())
        {
            ready = fifo;
            blockReady.store (true);
        }
        fifoIndex = 0;
    }
}

bool FormantRouletteAudioProcessor::fetchSpectrumBlock (float* dest)
{
    if (! blockReady.load()) return false;
    std::copy (ready.begin(), ready.end(), dest);
    blockReady.store (false);
    return true;
}

//==============================================================================
void FormantRouletteAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin (buffer.getNumChannels(), 2);

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, n);

    runAutoRandomise (n);

    const bool   seq       = sequencerOn();
    const int    nSteps    = numSteps();
    const double stepBeats = kRateBeats[juce::jlimit (0, 4, (int) ratePtr->load())];
    const bool   parallel  = topoPtr->load() > 0.5f;

    if (parallel != lastParallel)
    {
        for (auto& ch : filt) for (auto& f : ch) f.reset();
        lastParallel = parallel;
    }

    // Tempo / position for the step sequencer
    double bpm = 120.0, hostBeats = 0.0;
    bool hostSync = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto bp = pos->getBpm()) bpm = *bp;
            if (pos->getIsPlaying())
                if (auto ppq = pos->getPpqPosition()) { hostBeats = *ppq; hostSync = true; }
        }
    const double beatsPerSample = juce::jmax (20.0, bpm) / 60.0 / sampleRate;
    const double beats0 = hostSync ? hostBeats : seqBeats;

    const float glideSec = juce::jmax (0.001f, glidePtr->load() * 0.001f);
    const float tau = glideSec / 3.0f;     // ~95% of the way there after "glide" ms

    float tgtLogF[kNumBands], tgtGain[kNumBands], tgtLogQ[kNumBands];
    for (int b = 0; b < kNumBands; ++b)
    {
        tgtLogF[b] = std::log2 (fPtr[b]->load());
        tgtGain[b] = gPtr[b]->load();
        tgtLogQ[b] = std::log2 (qPtr[b]->load());
    }
    mixSm.setTargetValue (mixPtr->load());
    outSm.setTargetValue (juce::Decibels::decibelsToGain (outPtr->load()));

    float* data[2] = { buffer.getWritePointer (0), numCh > 1 ? buffer.getWritePointer (1) : nullptr };
    float gLin[kNumBands];
    int stepNow = 0;

    for (int start = 0; start < n; start += kChunk)
    {
        const int len = juce::jmin (kChunk, n - start);

        if (seq)
        {
            const double beats = beats0 + beatsPerSample * (double) start;
            const long long si = (long long) std::floor (beats / stepBeats);
            stepNow = (int) (((si % nSteps) + nSteps) % nSteps);
            for (int b = 0; b < kNumBands; ++b)
            {
                tgtLogF[b] = std::log2 (juce::jmax (20.f, getStepValue (stepNow, b, 0)));
                tgtGain[b] = getStepValue (stepNow, b, 1);
                tgtLogQ[b] = std::log2 (juce::jmax (0.3f, getStepValue (stepNow, b, 2)));
            }
        }

        const float coef = 1.0f - std::exp (-(float) len / ((float) sampleRate * tau));

        for (int b = 0; b < kNumBands; ++b)
        {
            curLogF[b] += (tgtLogF[b] - curLogF[b]) * coef;
            curGain[b] += (tgtGain[b] - curGain[b]) * coef;
            curLogQ[b] += (tgtLogQ[b] - curLogQ[b]) * coef;

            const double f = std::exp2 (curLogF[b]);
            const double q = std::exp2 (curLogQ[b]);
            const auto c = parallel ? BiquadCoeffs::bandpass (sampleRate, f, q)
                                    : BiquadCoeffs::peak (sampleRate, f, q, curGain[b]);
            gLin[b] = parallel ? std::pow (10.0f, curGain[b] / 20.0f) * kParallelTrim : 1.0f;

            for (int ch = 0; ch < numCh; ++ch)
                filt[ch][b].set (c);
        }

        for (int i = start; i < start + len; ++i)
        {
            const float mix  = mixSm.getNextValue();
            const float outG = outSm.getNextValue();
            float sum = 0.0f;

            for (int ch = 0; ch < numCh; ++ch)
            {
                const float x = data[ch][i];
                float y;

                if (parallel)
                {
                    y = 0.0f;
                    for (int b = 0; b < kNumBands; ++b)
                        y += gLin[b] * filt[ch][b].process (x);
                }
                else
                {
                    y = x;
                    for (int b = 0; b < kNumBands; ++b)
                        y = filt[ch][b].process (y);
                }

                y = (x + (y - x) * mix) * outG;
                y = softClip (y);           // safety: keep stacked resonances from blowing up
                data[ch][i] = y;
                sum += y;
            }
            pushSpectrumSample (sum / (float) numCh);
        }
    }

    if (! hostSync)
        seqBeats += beatsPerSample * (double) n;
    playingStep.store (seq ? stepNow : 0, std::memory_order_relaxed);

    for (int b = 0; b < kNumBands; ++b)
    {
        liveFreq[(size_t) b].store (std::exp2 (curLogF[b]), std::memory_order_relaxed);
        liveGain[(size_t) b].store (curGain[b],             std::memory_order_relaxed);
        liveQ[(size_t) b].store    (std::exp2 (curLogQ[b]), std::memory_order_relaxed);
    }
}

//==============================================================================
std::array<BandTarget, kNumBands> FormantRouletteAudioProcessor::rollSet (const std::array<BandTarget, kNumBands>& cur)
{
    const bool  vowel = modePtr->load() < 0.5f;
    const float chaos = chaosPtr->load();

    // Typical formant ranges F1..F5 (Hz) for the "Vowel" mode
    static const float fLo[]   = { 250.f, 600.f, 1800.f, 3000.f, 4000.f };
    static const float fHi[]   = { 1000.f, 2800.f, 3400.f, 4500.f, 5500.f };
    static const float gBase[] = { 16.f, 13.f, 10.f, 8.f, 6.f };

    std::array<BandTarget, kNumBands> fresh;
    int freeIdx[kNumBands];
    int nFree = 0;

    for (int b = 0; b < kNumBands; ++b)
    {
        if (bandLocked (b)) continue;       // locked formants hold their value

        BandTarget t;
        if (vowel)
        {
            t.f = fLo[b] * std::pow (fHi[b] / fLo[b], rng.nextFloat());
            t.g = gBase[b] + rng.nextFloat() * 7.f - 4.f;
            t.q = 4.f * std::pow (3.5f, rng.nextFloat());          // 4 .. 14
        }
        else
        {
            t.f = 80.f * std::pow (150.f, rng.nextFloat());        // 80 Hz .. 12 kHz
            t.g = rng.nextFloat() * 40.f - 20.f;
            t.q = 0.7f * std::pow (20.f, rng.nextFloat());         // 0.7 .. 14
        }
        fresh[(size_t) nFree] = t;
        freeIdx[nFree] = b;
        ++nFree;
    }

    std::sort (fresh.begin(), fresh.begin() + nFree,
               [] (const BandTarget& a, const BandTarget& b) { return a.f < b.f; });

    auto out = cur;
    for (int k = 0; k < nFree; ++k)
    {
        const int b = freeIdx[k];
        const auto& c = cur[(size_t) b];
        const auto& t = fresh[(size_t) k];

        // Chaos = how far we travel from the current setting towards the new random one
        out[(size_t) b].f = c.f * std::pow (t.f / c.f, chaos);
        out[(size_t) b].g = c.g + (t.g - c.g) * chaos;
        out[(size_t) b].q = c.q * std::pow (t.q / c.q, chaos);
    }
    return out;
}

void FormantRouletteAudioProcessor::randomise()
{
    if (sequencerOn())
    {
        rerollStep (juce::jlimit (0, kMaxSteps - 1, editStep.load()));
        return;
    }

    std::array<BandTarget, kNumBands> cur;
    for (int b = 0; b < kNumBands; ++b)
        cur[(size_t) b] = { fPtr[b]->load(), gPtr[b]->load(), qPtr[b]->load() };

    const auto t = rollSet (cur);
    for (int b = 0; b < kNumBands; ++b)
    {
        if (bandLocked (b)) continue;
        setParam (apvts.getParameter (bandId ("f", b)), t[(size_t) b].f);
        setParam (apvts.getParameter (bandId ("g", b)), t[(size_t) b].g);
        setParam (apvts.getParameter (bandId ("q", b)), t[(size_t) b].q);
    }
}

void FormantRouletteAudioProcessor::rerollStep (int s)
{
    std::array<BandTarget, kNumBands> cur;
    for (int b = 0; b < kNumBands; ++b)
        cur[(size_t) b] = { getStepValue (s, b, 0), getStepValue (s, b, 1), getStepValue (s, b, 2) };

    const auto t = rollSet (cur);
    for (int b = 0; b < kNumBands; ++b)
    {
        if (bandLocked (b)) continue;
        setStepValue (s, b, 0, t[(size_t) b].f);
        setStepValue (s, b, 1, t[(size_t) b].g);
        setStepValue (s, b, 2, t[(size_t) b].q);
    }
}

void FormantRouletteAudioProcessor::randomiseAllSteps()
{
    for (int s = 0; s < kMaxSteps; ++s)
        rerollStep (s);
}

//==============================================================================
juce::AudioProcessorEditor* FormantRouletteAudioProcessor::createEditor()
{
    return new FormantRouletteAudioProcessorEditor (*this);
}

void FormantRouletteAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::StringArray tokens;
    for (auto& a : stepData)
        tokens.add (juce::String (a.load(), 3));
    apvts.state.setProperty ("steps", tokens.joinIntoString (","), nullptr);

    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void FormantRouletteAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));

            const auto s = apvts.state.getProperty ("steps").toString();
            if (s.isNotEmpty())
            {
                const auto tokens = juce::StringArray::fromTokens (s, ",", "");
                const int count = juce::jmin (tokens.size(), (int) stepData.size());
                for (int i = 0; i < count; ++i)
                    stepData[(size_t) i].store (tokens[i].getFloatValue());
            }
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FormantRouletteAudioProcessor();
}
