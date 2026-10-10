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

    // Pill-style toggle, coloured by the button's tickColour (used for SEQ and the lock buttons)
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (1.5f);
        const auto col = b.findColour (juce::ToggleButton::tickColourId);
        const bool on = b.getToggleState();

        g.setColour (on ? col : col.withAlpha (highlighted ? 0.25f : 0.12f));
        g.fillRoundedRectangle (r, 6.f);
        g.setColour (col.withAlpha (on ? 1.f : 0.7f));
        g.drawRoundedRectangle (r, 6.f, 1.5f);
        g.setColour (on ? Palette::panel : col);
        g.setFont (juce::FontOptions (12.f, juce::Font::bold));
        g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
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
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void timerCallback() override;

    juce::Rectangle<float> plot() const;
    float freqToX (float f) const;
    float xToFreq (float x) const;
    float dbToY (float db) const;
    float yToDb (float y) const;

    // Node values come from the parameters, or from the edited step when the sequencer is on.
    BandTarget vals (int b) const;
    void setVal (int b, int k, float v);   // k: 0 freq, 1 gain, 2 Q
    float getF (int b) const { return vals (b).f; }
    float getG (int b) const { return vals (b).g; }
    float getQ (int b) const { return vals (b).q; }
    bool isLocked (int b) const { return lockP[b]->getValue() > 0.5f; }

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
    juce::RangedAudioParameter* lockP[kNumBands];

    int hovered = -1, dragged = -1;
    bool dragIsParam = true;
};

//==============================================================================
/** Row of 8 step cells showing a mini picture of each stored formant set. Click to select the step to edit. */
class StepStrip : public juce::Component, private juce::Timer
{
public:
    explicit StepStrip (FormantRouletteAudioProcessor& p) : proc (p) { startTimerHz (30); }
    ~StepStrip() override { stopTimer(); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override { repaint(); }
    juce::Rectangle<float> cell (int i) const;

    FormantRouletteAudioProcessor& proc;
};

//==============================================================================
class FormantRouletteAudioProcessorEditor : public juce::AudioProcessorEditor,
                                            private juce::Timer
{
public:
    explicit FormantRouletteAudioProcessorEditor (FormantRouletteAudioProcessor&);
    ~FormantRouletteAudioProcessorEditor() override { stopTimer(); setLookAndFeel (nullptr); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    using APVTS = juce::AudioProcessorValueTreeState;

    FormantRouletteAudioProcessor& proc;
    RotaryLookAndFeel lnf;
    EqDisplay display;
    StepStrip stepStrip;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<APVTS::SliderAttachment> att;
    };
    std::array<Knob, 4> knobs;

    juce::ComboBox modeBox, autoBox, topoBox, rateBox, stepsBox;
    juce::Label modeLabel, autoLabel, topoLabel, lockLabel;
    std::unique_ptr<APVTS::ComboBoxAttachment> modeAtt, autoAtt, topoAtt, rateAtt, stepsAtt;

    juce::ToggleButton seqBtn;
    std::unique_ptr<APVTS::ButtonAttachment> seqAtt;

    std::array<juce::ToggleButton, kNumBands> lockBtns;
    std::array<std::unique_ptr<APVTS::ButtonAttachment>, kNumBands> lockAtt;

    juce::TextButton randomBtn { "RANDOMISE" };
    juce::TextButton randAllBtn { "RAND ALL STEPS" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FormantRouletteAudioProcessorEditor)
};
