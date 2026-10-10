#include "PluginEditor.h"
#include <cmath>

namespace
{
    constexpr float kDbRange = 30.f;
    constexpr float kMinF = 20.f, kMaxF = 20000.f;

    juce::String fmtFreq (float f)
    {
        return f >= 1000.f ? juce::String (f / 1000.f, 2) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz";
    }

    void setNormalised (juce::RangedAudioParameter* p, float v)
    {
        p->setValueNotifyingHost (p->convertTo0to1 (v));
    }
}

//==============================================================================
EqDisplay::EqDisplay (FormantRouletteAudioProcessor& p)
    : processor (p),
      fft (FormantRouletteAudioProcessor::fftOrder),
      window ((size_t) FormantRouletteAudioProcessor::fftSize, juce::dsp::WindowingFunction<float>::hann)
{
    for (int b = 0; b < kNumBands; ++b)
    {
        fP[b] = p.apvts.getParameter (FormantRouletteAudioProcessor::bandId ("f", b));
        gP[b] = p.apvts.getParameter (FormantRouletteAudioProcessor::bandId ("g", b));
        qP[b] = p.apvts.getParameter (FormantRouletteAudioProcessor::bandId ("q", b));
    }
    analyser.fill (-120.f);
    startTimerHz (30);
}

void EqDisplay::timerCallback()
{
    constexpr int N = FormantRouletteAudioProcessor::fftSize;
    if (processor.fetchSpectrumBlock (fftData.data()))
    {
        std::fill (fftData.begin() + N, fftData.end(), 0.f);
        window.multiplyWithWindowingTable (fftData.data(), (size_t) N);
        fft.performFrequencyOnlyForwardTransform (fftData.data());

        for (int i = 0; i < N / 2; ++i)
        {
            const float db = juce::Decibels::gainToDecibels (fftData[(size_t) i] / ((float) N * 0.25f), -120.f);
            auto& a = analyser[(size_t) i];
            a = db > a ? a * 0.4f + db * 0.6f : a * 0.85f + db * 0.15f;
        }
    }
    repaint();
}

//==============================================================================
juce::Rectangle<float> EqDisplay::plot() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (36.f).withTrimmedBottom (22.f)
                                     .withTrimmedTop (10.f).withTrimmedRight (10.f);
}

float EqDisplay::freqToX (float f) const
{
    const auto p = plot();
    return p.getX() + p.getWidth() * std::log (f / kMinF) / std::log (kMaxF / kMinF);
}

float EqDisplay::xToFreq (float x) const
{
    const auto p = plot();
    return kMinF * std::pow (kMaxF / kMinF, (x - p.getX()) / p.getWidth());
}

float EqDisplay::dbToY (float db) const
{
    const auto p = plot();
    return p.getCentreY() - db / kDbRange * p.getHeight() * 0.5f;
}

float EqDisplay::yToDb (float y) const
{
    const auto p = plot();
    return (p.getCentreY() - y) / (p.getHeight() * 0.5f) * kDbRange;
}

juce::Point<float> EqDisplay::nodePos (int b) const
{
    return { freqToX (getF (b)), dbToY (juce::jlimit (-kDbRange, kDbRange, getG (b))) };
}

int EqDisplay::hitTest (juce::Point<float> pt) const
{
    int best = -1;
    float bestD = 16.f;
    for (int b = 0; b < kNumBands; ++b)
    {
        const float d = nodePos (b).getDistanceFrom (pt);
        if (d < bestD) { bestD = d; best = b; }
    }
    return best;
}

//==============================================================================
juce::Path EqDisplay::buildCurve (const juce::Rectangle<float>& p, const std::function<double (double)>& dbAt) const
{
    juce::Path path;
    bool first = true;
    for (float x = p.getX(); x <= p.getRight() + 0.5f; x += 2.f)
    {
        const float xx = juce::jmin (x, p.getRight());
        const double db = juce::jlimit (-(double) kDbRange, (double) kDbRange, dbAt ((double) xToFreq (xx)));
        const float y = dbToY ((float) db);
        if (first) { path.startNewSubPath (xx, y); first = false; }
        else         path.lineTo (xx, y);
    }
    return path;
}

juce::Path EqDisplay::closeToBaseline (juce::Path path, const juce::Rectangle<float>& p) const
{
    path.lineTo (p.getRight(), dbToY (0.f));
    path.lineTo (p.getX(), dbToY (0.f));
    path.closeSubPath();
    return path;
}

void EqDisplay::drawGrid (juce::Graphics& g, const juce::Rectangle<float>& p) const
{
    g.setFont (juce::FontOptions (11.f));

    for (int db = -30; db <= 30; db += 6)
    {
        const float y = dbToY ((float) db);
        g.setColour (juce::Colours::white.withAlpha (db == 0 ? 0.22f : 0.06f));
        g.drawHorizontalLine ((int) y, p.getX(), p.getRight());

        if (db % 12 == 0 && std::abs (db) < 30)
        {
            g.setColour (juce::Colour (0xff7d8594));
            g.drawText ((db > 0 ? "+" : "") + juce::String (db),
                        juce::Rectangle<float> (0.f, y - 7.f, p.getX() - 6.f, 14.f),
                        juce::Justification::centredRight);
        }
    }

    for (float decade : { 10.f, 100.f, 1000.f, 10000.f })
        for (int m = 1; m <= 9; ++m)
        {
            const float f = decade * (float) m;
            if (f < kMinF || f > kMaxF) continue;
            g.setColour (juce::Colours::white.withAlpha (m == 1 ? 0.09f : 0.035f));
            g.drawVerticalLine ((int) freqToX (f), p.getY(), p.getBottom());
        }

    g.setColour (juce::Colour (0xff7d8594));
    for (float f : { 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f })
    {
        const auto txt = f >= 1000.f ? juce::String (f / 1000.f, 0) + "k" : juce::String ((int) f);
        g.drawText (txt, juce::Rectangle<float> (freqToX (f) - 20.f, p.getBottom() + 3.f, 40.f, 14.f),
                    juce::Justification::centred);
    }
}

void EqDisplay::drawAnalyser (juce::Graphics& g, const juce::Rectangle<float>& p) const
{
    const float fs = (float) processor.getSampleRate();
    if (fs <= 0.f) return;
    constexpr int N = FormantRouletteAudioProcessor::fftSize;

    juce::Path path;
    bool started = false;
    float lastX = p.getX();

    for (int i = 1; i < N / 2; ++i)
    {
        const float f = (float) i * fs / (float) N;
        if (f < kMinF) continue;
        if (f > kMaxF) break;

        const float tilt = 4.5f * std::log2 (f / 1000.f);   // pink-ish tilt like Pro-Q
        const float norm = juce::jlimit (0.f, 1.f, (analyser[(size_t) i] + tilt + 100.f) / 80.f);
        const float x = freqToX (f), y = p.getBottom() - norm * p.getHeight() * 0.9f;

        if (! started) { path.startNewSubPath (x, p.getBottom()); path.lineTo (x, y); started = true; }
        else             path.lineTo (x, y);
        lastX = x;
    }
    if (! started) return;

    auto outline = path;
    path.lineTo (lastX, p.getBottom());
    path.closeSubPath();

    g.setGradientFill (juce::ColourGradient (juce::Colour (0x704dabf7), 0.f, p.getY(),
                                             juce::Colour (0x084dabf7), 0.f, p.getBottom(), false));
    g.fillPath (path);
    g.setColour (juce::Colour (0x904dabf7));
    g.strokePath (outline, juce::PathStrokeType (1.f));
}

void EqDisplay::drawCurves (juce::Graphics& g, const juce::Rectangle<float>& p) const
{
    const double fs = processor.getSampleRate();
    if (fs <= 0.0) return;

    std::array<BiquadCoeffs, kNumBands> c;
    for (int b = 0; b < kNumBands; ++b)
        c[(size_t) b] = BiquadCoeffs::peak (fs, processor.liveFreq[(size_t) b].load(),
                                            processor.liveQ[(size_t) b].load(),
                                            processor.liveGain[(size_t) b].load());

    const int active = dragged >= 0 ? dragged : hovered;
    if (active >= 0)
    {
        auto bandPath = buildCurve (p, [&] (double f) { return 20.0 * std::log10 (std::max (1.0e-6, std::abs (c[(size_t) active].response (f, fs)))); });
        g.setColour (Palette::band[(size_t) active].withAlpha (0.22f));
        g.fillPath (closeToBaseline (bandPath, p));
    }

    const double mix = (double) processor.apvts.getRawParameterValue ("mix")->load();
    auto total = buildCurve (p, [&] (double f)
    {
        std::complex<double> h = 1.0;
        for (auto& k : c) h *= k.response (f, fs);
        h = (1.0 - mix) + mix * h;
        return 20.0 * std::log10 (std::max (1.0e-6, std::abs (h)));
    });

    g.setGradientFill (juce::ColourGradient (Palette::amber.withAlpha (0.30f), 0.f, p.getY(),
                                             Palette::amber.withAlpha (0.05f), 0.f, p.getCentreY(), false));
    g.fillPath (closeToBaseline (total, p));

    g.setColour (Palette::amber.withAlpha (0.25f));
    g.strokePath (total, juce::PathStrokeType (6.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (juce::Colour (0xffffd98f));
    g.strokePath (total, juce::PathStrokeType (2.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void EqDisplay::drawNodes (juce::Graphics& g, const juce::Rectangle<float>& p) const
{
    const int active = dragged >= 0 ? dragged : hovered;

    for (int b = 0; b < kNumBands; ++b)
    {
        const auto pos = nodePos (b);
        const float r = (b == active) ? 9.f : 7.f;
        const auto col = Palette::band[(size_t) b];

        g.setColour (col.withAlpha (0.22f));
        g.fillEllipse (pos.x - r - 5.f, pos.y - r - 5.f, (r + 5.f) * 2.f, (r + 5.f) * 2.f);
        g.setColour (col);
        g.fillEllipse (pos.x - r, pos.y - r, r * 2.f, r * 2.f);
        g.setColour (Palette::panel);
        g.setFont (juce::FontOptions (10.f, juce::Font::bold));
        g.drawText (juce::String (b + 1), juce::Rectangle<float> (pos.x - r, pos.y - r, r * 2.f, r * 2.f),
                    juce::Justification::centred);
    }

    if (active >= 0)
    {
        const auto txt = "F" + juce::String (active + 1) + "   " + fmtFreq (getF (active)) + "   "
                         + juce::String (getG (active), 1) + " dB   Q " + juce::String (getQ (active), 2);
        juce::Rectangle<float> box (196.f, 22.f);
        box.setCentre (nodePos (active).x, nodePos (active).y - 28.f);
        box = box.constrainedWithin (p.reduced (4.f));

        g.setColour (juce::Colour (0xe0101216));
        g.fillRoundedRectangle (box, 5.f);
        g.setColour (Palette::band[(size_t) active].withAlpha (0.8f));
        g.drawRoundedRectangle (box, 5.f, 1.f);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (12.f));
        g.drawText (txt, box, juce::Justification::centred);
    }
}

void EqDisplay::paint (juce::Graphics& g)
{
    g.setColour (Palette::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.f);

    const auto p = plot();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1b1e25), 0.f, p.getY(),
                                             juce::Colour (0xff0d0f13), 0.f, p.getBottom(), false));
    g.fillRect (p);

    drawGrid (g, p);
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (p.toNearestInt());
        drawAnalyser (g, p);
        drawCurves (g, p);
    }
    drawNodes (g, p);
}

//==============================================================================
void EqDisplay::mouseMove (const juce::MouseEvent& e)
{
    const int h = hitTest (e.position);
    if (h != hovered) { hovered = h; repaint(); }
    setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void EqDisplay::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
}

void EqDisplay::mouseDown (const juce::MouseEvent& e)
{
    dragged = hitTest (e.position);
    if (dragged >= 0)
    {
        fP[dragged]->beginChangeGesture();
        gP[dragged]->beginChangeGesture();
    }
}

void EqDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragged < 0) return;
    const auto p = plot();
    const float x = juce::jlimit (p.getX(), p.getRight(), e.position.x);
    const float y = juce::jlimit (p.getY(), p.getBottom(), e.position.y);
    setNormalised (fP[dragged], xToFreq (x));
    setNormalised (gP[dragged], yToDb (y));
}

void EqDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragged >= 0)
    {
        fP[dragged]->endChangeGesture();
        gP[dragged]->endChangeGesture();
        dragged = -1;
    }
}

void EqDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int h = hitTest (e.position);
    if (h < 0) return;
    qP[h]->beginChangeGesture();
    setNormalised (qP[h], getQ (h) * std::exp (w.deltaY * 2.0f));
    qP[h]->endChangeGesture();
}

//==============================================================================
FormantRouletteAudioProcessorEditor::FormantRouletteAudioProcessorEditor (FormantRouletteAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), display (p)
{
    setLookAndFeel (&lnf);

    static const char* ids[]   = { "chaos", "glide", "mix", "out" };
    static const char* names[] = { "CHAOS", "GLIDE", "MIX", "OUTPUT" };

    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& k = knobs[i];
        k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
        k.label.setText (names[i], juce::dontSendNotification);
        k.label.setJustificationType (juce::Justification::centred);
        k.label.setFont (juce::FontOptions (11.f, juce::Font::bold));
        k.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, ids[i], k.slider);
        addAndMakeVisible (k.slider);
        addAndMakeVisible (k.label);
    }

    modeBox.addItemList ({ "Vowel", "Wild" }, 1);
    autoBox.addItemList ({ "Off", "1 beat", "2 beats", "1 bar", "2 bars" }, 1);
    modeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "mode", modeBox);
    autoAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "auto", autoBox);

    modeLabel.setText ("MODE", juce::dontSendNotification);
    autoLabel.setText ("AUTO", juce::dontSendNotification);
    for (auto* l : { &modeLabel, &autoLabel })
    {
        l->setFont (juce::FontOptions (11.f, juce::Font::bold));
        l->setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (l);
    }

    randomBtn.onClick = [this] { proc.randomise(); };

    addAndMakeVisible (modeBox);
    addAndMakeVisible (autoBox);
    addAndMakeVisible (randomBtn);
    addAndMakeVisible (display);

    setResizable (true, true);
    setResizeLimits (720, 460, 1600, 1000);
    setSize (920, 580);
}

void FormantRouletteAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1e2128), 0.f, 0.f,
                                             Palette::bg, 0.f, (float) getHeight(), false));
    g.fillAll();

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (17.f, juce::Font::bold));
    g.drawText ("FORMANT ROULETTE", 20, 10, 260, 22, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xff7d8594));
    g.setFont (juce::FontOptions (11.f));
    g.drawText ("drag nodes  |  scroll on a node for Q", 20, 31, 260, 16, juce::Justification::centredLeft);
}

void FormantRouletteAudioProcessorEditor::resized()
{
    auto r = getLocalBounds();

    auto top = r.removeFromTop (56).reduced (16, 12);
    randomBtn.setBounds (top.removeFromRight (150));
    top.removeFromRight (14);
    autoBox.setBounds (top.removeFromRight (110));
    autoLabel.setBounds (top.removeFromRight (44));
    top.removeFromRight (10);
    modeBox.setBounds (top.removeFromRight (100));
    modeLabel.setBounds (top.removeFromRight (48));

    auto bottom = r.removeFromBottom (128).reduced (24, 6);
    const int w = bottom.getWidth() / (int) knobs.size();
    for (auto& k : knobs)
    {
        auto cell = bottom.removeFromLeft (w).reduced (8, 0);
        k.label.setBounds (cell.removeFromTop (16));
        k.slider.setBounds (cell);
    }

    display.setBounds (r.reduced (14, 0));
}
