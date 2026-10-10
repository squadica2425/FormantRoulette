#pragma once
#include "PluginProcessor.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <functional>

namespace Palette
{
    inline const juce::Colour amber  { 0xffffb84d };
    inline const juce::Colour bg     { 0xff15171c };
    inline const juce::Colour panel  { 0xff0f1115 };
    inline const std::array<juce::Colour, kNumBands> band {
        juce::Colour (0xffff6b6b), juce::Colour (0xffffa94d), juce::Colour (0xffffe066),
        juce::Colour (0xff69db7c), juce::Colour (0xff4dabf7) };
}

//==============================================================================
class RotaryLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RotaryLookAndFeel()
    {
        using namespace juce;
        setColour (ComboBox::backgroundColourId, Colour (0xff23262d));
        setColour (ComboBox::outlineColourId, Colour (0xff3a3f49));
        setColour (ComboBox::textColourId, Colours::white);
        setColour (ComboBox::arrowColourId, Palette::amber);
        setColour (PopupMenu::backgroundColourId, Colour (0xff1c1f25));
        setColour (PopupMenu::textColourId, Colours::white);
        setColour (PopupMenu::highlightedBackgroundColourId, Palette::amber);
        setColour (PopupMenu::highlightedTextColourId, Colour (0xff15171c));
        setColour (TextButton::buttonColourId, Palette::amber);
        setColour (TextButton::buttonOnColourId, Palette::amber.brighter (0.2f));
        setColour (TextButton::textColourOffId, Colour (0xff15171c));
        setColour (TextButton::textColourOnId, Colour (0xff15171c));
        setColour (Slider::textBoxTextColourId, Colour (0xffc9ced8));
        setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
        setColour (Label::textColourId, Colour (0xff8b93a1));
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float a0, float a1, juce::Slider&) override
    {
        auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.f);
        const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
        const auto c = b.getCentre();
        const float angle = a0 + pos * (a1 - a0);
        const float arcR = r - 3.f;

        juce::Path track;
        track.addCentredArc (c.x, c.y, arcR, arcR, 0.f, a0, a1, true);
        g.setColour (juce::Colour (0xff2a2e36));
        g.strokePath (track, juce::PathStrokeType (4.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path val;
        val.addCentredArc (c.x, c.y, arcR, arcR, 0.f, a0, angle, true);
        g.setColour (Palette::amber);
        g.strokePath (val, juce::PathStrokeType (4.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float kr = arcR - 8.f;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3b404b), c.x, c.y - kr,
                                                 juce::Colour (0xff1c1f25), c.x, c.y + kr, false));
        g.fillEllipse (c.x - kr, c.y - kr, kr * 2.f, kr * 2.f);

        juce::Path ptr;
        ptr.addRoundedRectangle (-1.5f, -kr, 3.f, kr * 0.45f, 1.5f);
        g.setColour (juce::Colours::white);
        g.fillPath (ptr, juce::AffineTransform::rotation (angle).translated (c));
    }
};

//==============================================================================
/** Pro-Q style display: log-frequency grid, live spectrum, response curve, draggable band nodes. */
class EqDisplay : public juce::Component, private juce::Timer
{
public:
    explicit EqDisplay (FormantRouletteAudioProcessor&);
    ~EqDisplay() override { stopTimer(); }

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void timerCallback() override;

    juce::Rectangle<float> plot() const;
    float freqToX (float f) const;
    float xToFreq (float x) const;
    float dbToY (float db) const;
    float yToDb (float y) const;

    float getF (int b) const { return fP[b]->convertFrom0to1 (fP[b]->getValue()); }
    float getG (int b) const { return gP[b]->convertFrom0to1 (gP[b]->getValue()); }
    float getQ (int b) const { return qP[b]->convertFrom0to1 (qP[b]->getValue()); }
    juce::Point<float> nodePos (int b) const;
    int hitTest (juce::Point<float> pt) const;

    juce::Path buildCurve (const juce::Rectangle<float>& p, const std::function<double (double)>& dbAt) const;
    juce::Path closeToBaseline (juce::Path path, const juce::Rectangle<float>& p) const;
    void drawGrid (juce::Graphics&, const juce::Rectangle<float>&) const;
    void drawAnalyser (juce::Graphics&, const juce::Rectangle<float>&) const;
    void drawCurves (juce::Graphics&, const juce::Rectangle<float>&) const;
    void drawNodes (juce::Graphics&, const juce::Rectangle<float>&) const;

    FormantRouletteAudioProcessor& processor;
    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;
    std::array<float, 2 * FormantRouletteAudioProcessor::fftSize> fftData {};
    std::array<float, FormantRouletteAudioProcessor::fftSize / 2> analyser {};

    juce::RangedAudioParameter* fP[kNumBands];
    juce::RangedAudioParameter* gP[kNumBands];
    juce::RangedAudioParameter* qP[kNumBands];

    int hovered = -1, dragged = -1;
};

//==============================================================================
class FormantRouletteAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit FormantRouletteAudioProcessorEditor (FormantRouletteAudioProcessor&);
    ~FormantRouletteAudioProcessorEditor() override { setLookAndFeel (nullptr); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    FormantRouletteAudioProcessor& proc;
    RotaryLookAndFeel lnf;
    EqDisplay display;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };
    std::array<Knob, 4> knobs;

    juce::ComboBox modeBox, autoBox;
    juce::Label modeLabel, autoLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAtt, autoAtt;
    juce::TextButton randomBtn { "RANDOMISE" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FormantRouletteAudioProcessorEditor)
};
