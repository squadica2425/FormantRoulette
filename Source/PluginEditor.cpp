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
        fP[b]    = p.apvts.getParameter (FormantRouletteAudioProcessor::bandId ("f", b));
        gP[b]    = p.apvts.getParameter (FormantRouletteAudioProcessor::bandId ("g", b));
        qP[b]    = p.apvts.getParameter (FormantRouletteAudioProcessor::bandId ("q", b));
        lockP[b] = p.apvts.getParameter (FormantRouletteAudioProcessor::bandId ("l", b));
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
BandTarget EqDisplay::vals (int b) const
{
    if (processor.sequencerOn())
    {
        const int s = juce::jlimit (0, kMaxSteps - 1, processor.editStep.load());
        return { processor.getStepValue (s, b, 0), processor.getStepValue (s, b, 1), processor.getStepValue (s, b, 2) };
    }
    return { fP[b]->convertFrom0to1 (fP[b]->getValue()),
             gP[b]->convertFrom0to1 (gP[b]->getValue()),
             qP[b]->convertFrom0to1 (qP[b]->getValue()) };
}

void EqDisplay::setVal (int b, int k, float v)
{
    if (processor.sequencerOn())
    {
        static const float lo[] = { 20.f, -24.f, 0.3f };
        static const float hi[] = { 20000.f, 24.f, 20.f };
        const int s = juce::jlimit (0, kMaxSteps - 1, processor.editStep.load());
        processor.setStepValue (s, b, k, juce::jlimit (lo[k], hi[k], v));
        return;
    }
    juce::RangedAudioParameter* params[] = { fP[b], gP[b], qP[b] };
    setNormalised (params[k], v);
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
    const auto v = vals (b);
    return { freqToX (v.f), dbToY (juce::jlimit (-kDbRange, kDbRange, v.g)) };
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

    const bool par = processor.apvts.getRawParameterValue ("topo")->load() > 0.5f;

    std::array<BiquadCoeffs, kNumBands> c;
    std::array<double, kNumBands> lin;
    for (size_t b = 0; b < (size_t) kNumBands; ++b)
    {
        const double f = processor.liveFreq[b].load();
        const double q = processor.liveQ[b].load();
        const double gdb = processor.liveGain[b].load();
        c[b]   = par ? BiquadCoeffs::bandpass (fs, f, q) : BiquadCoeffs::peak (fs, f, q, gdb);
        lin[b] = par ? std::pow (10.0, gdb / 20.0) * (double) kParallelTrim : 1.0;
    }

    const int active = dragged >= 0 ? dragged : hovered;
    if (active >= 0)
    {
        auto bandPath = buildCurve (p, [&] (double f)
        {
            return 20.0 * std::log10 (std::max (1.0e-6, std::abs (lin[(size_t) active] * c[(size_t) active].response (f, fs))));
        });
        g.setColour (Palette::band[(size_t) active].withAlpha (0.22f));
        g.fillPath (closeToBaseline (bandPath, p));
    }

    const double mix = (double) processor.apvts.getRawParameterValue ("mix")->load();
    auto total = buildCurve (p, [&] (double f)
    {
        std::complex<double> h = par ? 0.0 : 1.0;
        for (size_t b = 0; b < (size_t) kNumBands; ++b)
        {
            const auto r = lin[b] * c[b].response (f, fs);
            if (par) h += r; else h *= r;
        }
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

        if (isLocked (b))
        {
            g.setColour (juce::Colours::white);
            g.drawEllipse (pos.x - r - 3.f, pos.y - r - 3.f, (r + 3.f) * 2.f, (r + 3.f) * 2.f, 1.6f);
        }

        g.setColour (Palette::panel);
        g.setFont (juce::FontOptions (10.f, juce::Font::bold));
        g.drawText (juce::String (b + 1), juce::Rectangle<float> (pos.x - r, pos.y - r, r * 2.f, r * 2.f),
                    juce::Justification::centred);
    }

    if (active >= 0)
    {
        const auto v = vals (active);
        const auto txt = "F" + juce::String (active + 1) + (isLocked (active) ? " (locked)" : "") + "   "
                         + fmtFreq (v.f) + "   " + juce::String (v.g, 1) + " dB   Q " + juce::String (v.q, 2);
        juce::Rectangle<float> box (250.f, 22.f);
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

    if (processor.sequencerOn())
    {
        g.setColour (Palette::amber);
        g.setFont (juce::FontOptions (11.f, juce::Font::bold));
        g.drawText ("EDITING STEP " + juce::String (processor.editStep.load() + 1),
                    p.reduced (8.f, 6.f), juce::Justification::topLeft);
    }
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
    dragIsParam = ! processor.sequencerOn();
    if (dragged >= 0 && dragIsParam)
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
    setVal (dragged, 0, xToFreq (x));
    setVal (dragged, 1, yToDb (y));
}

void EqDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragged >= 0)
    {
        if (dragIsParam)
        {
            fP[dragged]->endChangeGesture();
            gP[dragged]->endChangeGesture();
        }
        dragged = -1;
    }
}

void EqDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int h = hitTest (e.position);
    if (h < 0) return;
    auto* lp = lockP[h];
    lp->beginChangeGesture();
    lp->setValueNotifyingHost (lp->getValue() > 0.5f ? 0.f : 1.f);
    lp->endChangeGesture();
}

void EqDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int h = hitTest (e.position);
    if (h < 0) return;
    const float q = getQ (h) * std::exp (w.deltaY * 2.0f);

    if (processor.sequencerOn())
    {
        setVal (h, 2, q);
    }
    else
    {
        qP[h]->beginChangeGesture();
        setVal (h, 2, q);
        qP[h]->endChangeGesture();
    }
}

//==============================================================================
juce::Rectangle<float> StepStrip::cell (int i) const
{
    const float w = (float) getWidth() / (float) kMaxSteps;
    return juce::Rectangle<float> (w * (float) i, 0.f, w, (float) getHeight()).reduced (3.f);
}

void StepStrip::paint (juce::Graphics& g)
{
    const bool seq = proc.sequencerOn();
    const int nSteps = proc.numSteps();
    const int play = proc.playingStep.load();
    const int edit = proc.editStep.load();

    for (int i = 0; i < kMaxSteps; ++i)
    {
        const auto r = cell (i);
        const bool active = i < nSteps;
        const float a = (seq ? 1.f : 0.55f) * (active ? 1.f : 0.3f);

        g.setColour (juce::Colour (0xff1c1f25).withAlpha (a));
        g.fillRoundedRectangle (r, 5.f);

        // mini picture of the stored formant set: one bar per band, x = freq, height = gain
        const auto gr = r.reduced (6.f, 7.f);
        for (int b = 0; b < kNumBands; ++b)
        {
            const float f = juce::jlimit (20.f, 20000.f, proc.getStepValue (i, b, 0));
            const float gdb = juce::jlimit (-24.f, 24.f, proc.getStepValue (i, b, 1));
            const float x = gr.getX() + gr.getWidth() * std::log (f / 20.f) / std::log (1000.f);
            const float hgt = juce::jmap (gdb, -24.f, 24.f, 2.f, gr.getHeight());
            g.setColour (Palette::band[(size_t) b].withAlpha (a));
            g.fillRoundedRectangle (x - 1.5f, gr.getBottom() - hgt, 3.f, hgt, 1.5f);
        }

        g.setColour (juce::Colours::white.withAlpha (0.5f * a));
        g.setFont (juce::FontOptions (10.f));
        g.drawText (juce::String (i + 1), r.reduced (5.f, 3.f), juce::Justification::topLeft);

        if (seq && active && i == play)
        {
            g.setColour (Palette::amber);
            g.drawRoundedRectangle (r, 5.f, 2.f);
        }
        if (seq && i == edit)
        {
            g.setColour (juce::Colours::white.withAlpha (0.65f));
            g.drawRoundedRectangle (r.reduced (3.f), 3.f, 1.f);
        }
    }
}

void StepStrip::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < proc.numSteps(); ++i)
        if (cell (i).contains (e.position))
        {
            proc.editStep.store (i);
            repaint();
            return;
        }
}

//==============================================================================
FormantRouletteAudioProcessorEditor::FormantRouletteAudioProcessorEditor (FormantRouletteAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), display (p), stepStrip (p)
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
        k.att = std::make_unique<APVTS::SliderAttachment> (proc.apvts, ids[i], k.slider);
        addAndMakeVisible (k.slider);
        addAndMakeVisible (k.label);
    }

    modeBox.addItemList ({ "Vowel", "Wild" }, 1);
    autoBox.addItemList ({ "Off", "1 beat", "2 beats", "1 bar", "2 bars" }, 1);
    topoBox.addItemList ({ "Serial Peaks", "Parallel Bands" }, 1);
    rateBox.addItemList ({ "1/16", "1/8", "1/4", "1/2", "1 bar" }, 1);
    stepsBox.addItemList ({ "2 steps", "3 steps", "4 steps", "5 steps", "6 steps", "7 steps", "8 steps" }, 1);
    modeAtt  = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "mode", modeBox);
    autoAtt  = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "auto", autoBox);
    topoAtt  = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "topo", topoBox);
    rateAtt  = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "seqrate", rateBox);
    stepsAtt = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "seqsteps", stepsBox);

    modeLabel.setText ("MODE", juce::dontSendNotification);
    autoLabel.setText ("AUTO", juce::dontSendNotification);
    topoLabel.setText ("FILTER", juce::dontSendNotification);
    lockLabel.setText ("LOCK  (or double-click a node)", juce::dontSendNotification);
    for (auto* l : { &modeLabel, &autoLabel, &topoLabel })
    {
        l->setFont (juce::FontOptions (11.f, juce::Font::bold));
        l->setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (l);
    }
    lockLabel.setFont (juce::FontOptions (11.f, juce::Font::bold));
    lockLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (lockLabel);

    seqBtn.setButtonText ("SEQ");
    seqBtn.setColour (juce::ToggleButton::tickColourId, Palette::amber);
    seqAtt = std::make_unique<APVTS::ButtonAttachment> (proc.apvts, "seq", seqBtn);
    addAndMakeVisible (seqBtn);

    for (size_t i = 0; i < lockBtns.size(); ++i)
    {
        lockBtns[i].setButtonText (juce::String ((int) i + 1));
        lockBtns[i].setColour (juce::ToggleButton::tickColourId, Palette::band[i]);
        lockAtt[i] = std::make_unique<APVTS::ButtonAttachment> (proc.apvts,
                        FormantRouletteAudioProcessor::bandId ("l", (int) i), lockBtns[i]);
        addAndMakeVisible (lockBtns[i]);
    }

    randAllBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2e323b));
    randAllBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::white);

    randomBtn.onClick  = [this] { proc.randomise(); };
    randAllBtn.onClick = [this] { proc.randomiseAllSteps(); };

    for (auto* c : std::initializer_list<juce::Component*> { &modeBox, &autoBox, &topoBox, &rateBox, &stepsBox,
                                                              &randomBtn, &randAllBtn, &display, &stepStrip })
        addAndMakeVisible (c);

    setResizable (true, true);
    setResizeLimits (900, 580, 1600, 1100);
    setSize (980, 700);
    startTimerHz (10);
}

void FormantRouletteAudioProcessorEditor::timerCallback()
{
    const juce::String t = proc.sequencerOn() ? "RANDOMISE STEP" : "RANDOMISE";
    if (randomBtn.getButtonText() != t)
        randomBtn.setButtonText (t);
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

    // top bar (right to left)
    auto top = r.removeFromTop (56).reduced (16, 12);
    randomBtn.setBounds (top.removeFromRight (140));
    top.removeFromRight (12);
    autoBox.setBounds (top.removeFromRight (92));
    autoLabel.setBounds (top.removeFromRight (40));
    top.removeFromRight (8);
    modeBox.setBounds (top.removeFromRight (84));
    modeLabel.setBounds (top.removeFromRight (44));
    top.removeFromRight (8);
    topoBox.setBounds (top.removeFromRight (130));
    topoLabel.setBounds (top.removeFromRight (48));

    // knobs
    auto bottom = r.removeFromBottom (128).reduced (24, 6);
    const int w = bottom.getWidth() / (int) knobs.size();
    for (auto& k : knobs)
    {
        auto cell = bottom.removeFromLeft (w).reduced (8, 0);
        k.label.setBounds (cell.removeFromTop (16));
        k.slider.setBounds (cell);
    }

    // lock buttons + sequencer row
    auto seqRow = r.removeFromBottom (92).reduced (14, 8);

    auto lockBlock = seqRow.removeFromLeft (200);
    lockLabel.setBounds (lockBlock.removeFromTop (18));
    lockBlock.removeFromTop (6);
    auto lockRow = lockBlock.removeFromTop (34);
    for (auto& b : lockBtns)
        b.setBounds (lockRow.removeFromLeft (36).reduced (2));

    seqRow.removeFromLeft (14);
    auto ctrl = seqRow.removeFromLeft (236);
    auto row1 = ctrl.removeFromTop (32);
    seqBtn.setBounds (row1.removeFromLeft (56));
    row1.removeFromLeft (8);
    rateBox.setBounds (row1.removeFromLeft (80));
    row1.removeFromLeft (8);
    stepsBox.setBounds (row1);
    ctrl.removeFromTop (10);
    randAllBtn.setBounds (ctrl.removeFromTop (32));

    seqRow.removeFromLeft (14);
    stepStrip.setBounds (seqRow);

    display.setBounds (r.reduced (14, 0));
}
