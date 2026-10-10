#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <complex>

constexpr int kNumBands = 5;

//==============================================================================
// RBJ peaking biquad. Coefficients are computed in double (also used by the UI
// to draw the magnitude response), the audio-thread filter runs in float.
struct BiquadCoeffs
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

    static BiquadCoeffs peak (double fs, double freq, double q, double gainDb)
    {
        freq = juce::jlimit (10.0, fs * 0.49, freq);
        const double A     = std::pow (10.0, gainDb / 40.0);
        const double w0    = juce::MathConstants<double>::twoPi * freq / fs;
        const double alpha = std::sin (w0) / (2.0 * q);
        const double c     = std::cos (w0);
        const double a0    = 1.0 + alpha / A;

        BiquadCoeffs k;
        k.b0 = (1.0 + alpha * A) / a0;
        k.b1 = (-2.0 * c) / a0;
        k.b2 = (1.0 - alpha * A) / a0;
        k.a1 = (-2.0 * c) / a0;
        k.a2 = (1.0 - alpha / A) / a0;
        return k;
    }

    std::complex<double> response (double freq, double fs) const
    {
        const double w = juce::MathConstants<double>::twoPi * freq / fs;
        const auto e1 = std::polar (1.0, -w);
        const auto e2 = std::polar (1.0, -2.0 * w);
        return (b0 + b1 * e1 + b2 * e2) / (1.0 + a1 * e1 + a2 * e2);
    }
};

struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void set (const BiquadCoeffs& c)
    {
        b0 = (float) c.b0; b1 = (float) c.b1; b2 = (float) c.b2;
        a1 = (float) c.a1; a2 = (float) c.a2;
    }
    void reset() { z1 = z2 = 0.0f; }

    inline float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

//==============================================================================
class FormantRouletteAudioProcessor : public juce::AudioProcessor,
                                      private juce::AsyncUpdater
{
public:
    static constexpr int fftOrder = 11;
    static constexpr int fftSize  = 1 << fftOrder;

    FormantRouletteAudioProcessor();
    ~FormantRouletteAudioProcessor() override { cancelPendingUpdate(); }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Formant Roulette"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    /** Message thread only. Picks new formant targets and writes them to the parameters. */
    void randomise();

    static juce::String bandId (const char* prefix, int band)
    {
        return juce::String (prefix) + juce::String (band + 1);
    }

    juce::AudioProcessorValueTreeState apvts;

    // Smoothed values actually used by the DSP, for the UI to draw.
    std::array<std::atomic<float>, kNumBands> liveFreq, liveGain, liveQ;

    /** Copies fftSize output samples if a new block is ready. */
    bool fetchSpectrumBlock (float* dest);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void handleAsyncUpdate() override { randomise(); }
    void runAutoRandomise (int numSamples);
    void pushSpectrumSample (float s);
    void snapToParams();

    std::atomic<float>* fPtr[kNumBands];
    std::atomic<float>* gPtr[kNumBands];
    std::atomic<float>* qPtr[kNumBands];
    std::atomic<float>* mixPtr   = nullptr;
    std::atomic<float>* outPtr   = nullptr;
    std::atomic<float>* glidePtr = nullptr;
    std::atomic<float>* chaosPtr = nullptr;
    std::atomic<float>* modePtr  = nullptr;
    std::atomic<float>* autoPtr  = nullptr;

    double sampleRate = 44100.0;
    Biquad filt[2][kNumBands];
    float curLogF[kNumBands], curGain[kNumBands], curLogQ[kNumBands];
    juce::SmoothedValue<float> mixSm, outSm;

    double autoCounter = 0.0;
    juce::Random rng;

    std::array<float, fftSize> fifo {}, ready {};
    int fifoIndex = 0;
    std::atomic<bool> blockReady { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FormantRouletteAudioProcessor)
};
