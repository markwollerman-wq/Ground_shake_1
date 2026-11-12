#include "PluginEditor.h"

using namespace gsui;

//==============================================================================
//  Palette helpers
//==============================================================================
void gsui::ledPanel (juce::Graphics& g, juce::Rectangle<float> r, float radius, float glow)
{
    // glossy black body
    g.setGradientFill (juce::ColourGradient (panelTop, r.getX(), r.getY(), panelBot, r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, radius);
    // soft gloss on the top third
    auto gloss = r.withHeight (r.getHeight() * 0.35f).reduced (2.0f, 2.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.06f), gloss.getX(), gloss.getY(),
                                             juce::Colours::white.withAlpha (0.0f), gloss.getX(), gloss.getBottom(), false));
    g.fillRoundedRectangle (gloss, radius - 2.0f);
    // red LED border with a glow
    for (auto [w, a] : { std::pair { 7.0f, 0.06f }, { 4.0f, 0.14f }, { 2.0f, 0.35f } })
    {
        g.setColour (red.withAlpha (a * glow));
        g.drawRoundedRectangle (r, radius, w);
    }
    g.setColour (red.withAlpha (0.95f * glow));
    g.drawRoundedRectangle (r, radius, 1.0f);
}

gsui::Match gsui::classify (float d, float tol)
{
    if (std::abs (d) <= tol) return Match::matched;
    if (std::abs (d) <= 3.0f) return Match::slightlyOff;
    return d < 0 ? Match::wellUnder : Match::wellOver;
}

juce::Colour gsui::colourFor (Match m)
{
    switch (m)
    {
        case Match::matched:     return green;
        case Match::slightlyOff: return yellow;
        case Match::wellUnder:   return blue;
        case Match::wellOver:    return over;
        case Match::none:
        default:                 return muted;
    }
}

juce::String gsui::labelFor (Match m)
{
    switch (m)
    {
        case Match::matched:     return "MATCHED";
        case Match::slightlyOff: return "SLIGHTLY OFF";
        case Match::wellUnder:   return "WELL UNDER";
        case Match::wellOver:    return "WELL OVER";
        case Match::none:
        default:                 return "WAITING";
    }
}

LedLookAndFeel::LedLookAndFeel()
{
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::Label::textColourId, text);
    setColour (juce::Label::textWhenEditingColourId, juce::Colours::white);
    setColour (juce::Label::backgroundWhenEditingColourId, juce::Colours::black);
    setColour (juce::Label::outlineWhenEditingColourId, red);
    setColour (juce::TextEditor::highlightColourId, redDim);
    setColour (juce::CaretComponent::caretColourId, red);
}

void LedLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (2.0f);
    const bool on = b.getToggleState();
    g.setGradientFill (juce::ColourGradient (on ? juce::Colour (0xff3a0a0a) : panelTop, r.getX(), r.getY(),
                                             on ? juce::Colour (0xff1a0303) : panelBot, r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 6.0f);
    const float glow = on ? 1.0f : (over ? 0.6f : 0.35f);
    if (on)
        for (auto [w, a] : { std::pair { 6.0f, 0.10f }, { 3.0f, 0.25f } })
        {
            g.setColour (red.withAlpha (a));
            g.drawRoundedRectangle (r, 6.0f, w);
        }
    g.setColour (red.withAlpha (glow * (down ? 1.0f : 0.9f)));
    g.drawRoundedRectangle (r, 6.0f, 1.2f);
}

void LedLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (b.getToggleState() ? juce::Colours::white : (b.isEnabled() ? text : muted.withAlpha (0.5f)));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
}

juce::Font LedLookAndFeel::getTextButtonFont (juce::TextButton&, int h)
{
    return juce::Font (juce::FontOptions (juce::jmin (15.0f, h * 0.48f), juce::Font::bold));
}

//==============================================================================
//  Feet
//==============================================================================
namespace
{
    // A right foot, sole view, toes up, in a 0..1 box
    juce::Path makeFoot()
    {
        juce::Path p;
        p.startNewSubPath (0.50f, 0.98f);
        p.cubicTo (0.27f, 0.98f, 0.21f, 0.82f, 0.25f, 0.64f);
        p.cubicTo (0.29f, 0.47f, 0.17f, 0.36f, 0.21f, 0.25f);
        p.cubicTo (0.25f, 0.14f, 0.70f, 0.12f, 0.79f, 0.26f);
        p.cubicTo (0.86f, 0.38f, 0.75f, 0.50f, 0.75f, 0.64f);
        p.cubicTo (0.75f, 0.82f, 0.73f, 0.98f, 0.50f, 0.98f);
        p.closeSubPath();
        const float toes[5][3] = { { 0.30f, 0.085f, 0.090f }, { 0.47f, 0.045f, 0.064f }, { 0.60f, 0.062f, 0.056f },
                                   { 0.71f, 0.105f, 0.048f }, { 0.795f, 0.170f, 0.042f } };
        for (auto& t : toes)
            p.addEllipse (t[0] - t[2], t[1] - t[2], t[2] * 2.0f, t[2] * 2.0f);
        return p;
    }

    float shakeFromDb (float db)                     // sub share in dB -> 0..1 shake
    {
        return juce::jlimit (0.0f, 1.0f, (db + 24.0f) / 18.0f);
    }
}

void FeetView::tick (double t)
{
    time = t;
    const bool youOn = proc.liveHasData() || ! proc.isHostPlaying();
    const float yt = youOn ? shakeFromDb (proc.getLiveSubDbNow()) : 0.0f;
    const float rt = proc.hasReference() ? shakeFromDb (proc.getRefSubDbNow()) : 0.0f;
    auto follow = [] (float cur, float target) { return cur + (target - cur) * (target > cur ? 0.6f : 0.12f); };
    youShake = follow (youShake, yt);
    refShake = follow (refShake, rt);
    repaint();
}

void FeetView::drawFoot (juce::Graphics& g, juce::Rectangle<float> area, bool leftFoot, juce::Colour c, float shake, double t)
{
    const float h = area.getHeight(), w = h * 0.62f;
    auto box = juce::Rectangle<float> (w, h).withCentre (area.getCentre());
    // the shake: fast jitter whose size follows the sub
    const float amp = shake * h * 0.035f;
    const float dx = amp * (float) std::sin (t * 2.0 * juce::MathConstants<double>::pi * 31.0)
                   + amp * 0.5f * (float) std::sin (t * 2.0 * juce::MathConstants<double>::pi * 47.0);
    const float dy = amp * 0.6f * (float) std::cos (t * 2.0 * juce::MathConstants<double>::pi * 37.0);
    const float rot = shake * 0.03f * (float) std::sin (t * 2.0 * juce::MathConstants<double>::pi * 23.0);

    auto path = makeFoot();
    auto tr = juce::AffineTransform::scale (leftFoot ? -1.0f : 1.0f, 1.0f).translated (leftFoot ? 1.0f : 0.0f, 0.0f)
                  .scaled (w, h)
                  .rotated (rot, w * 0.5f, h * 0.6f)
                  .translated (box.getX() + dx, box.getY() + dy);
    path.applyTransform (tr);

    // ground under the foot: vibration lines
    auto ground = juce::Rectangle<float> (area.getWidth() * 0.9f, h * 0.12f)
                      .withCentre ({ area.getCentreX(), area.getBottom() + h * 0.07f });
    for (int line = 0; line < 3; ++line)
    {
        juce::Path wave;
        const float y = ground.getY() + line * ground.getHeight() * 0.45f;
        const int pts = 48;
        for (int i = 0; i <= pts; ++i)
        {
            const float x = ground.getX() + ground.getWidth() * i / (float) pts;
            const float ph = (float) (t * (18.0 + line * 5.0)) + i * 0.7f;
            const float yy = y + std::sin (ph) * shake * (6.0f - line * 1.5f);
            i == 0 ? wave.startNewSubPath (x, yy) : wave.lineTo (x, yy);
        }
        g.setColour (c.withAlpha (0.15f + 0.5f * shake * (1.0f - line * 0.3f)));
        g.strokePath (wave, juce::PathStrokeType (1.4f));
    }

    juce::DropShadow (c.withAlpha (0.35f + 0.5f * shake), (int) (14 + 26 * shake), {}).drawForPath (g, path);
    g.setGradientFill (juce::ColourGradient (c.brighter (0.25f), box.getCentreX(), box.getY(),
                                             c.darker (0.6f), box.getCentreX(), box.getBottom(), false));
    g.fillPath (path);
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.strokePath (path, juce::PathStrokeType (1.2f));
}

void FeetView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    ledPanel (g, r, 12.0f);

    const auto live = proc.getLiveProfile();
    const auto& ref = proc.getReferenceProfile();
    const bool haveYou = live.valid, haveRef = proc.hasReference() && ref.valid;
    const float diff = haveYou && haveRef ? live.shakeDb() - ref.shakeDb() : 0.0f;
    const auto match = haveYou && haveRef ? classify (diff) : Match::none;

    const float colW = r.getWidth() * 0.30f;
    auto leftCol = r.withWidth (colW).reduced (12.0f, 14.0f);
    auto rightCol = r.withTrimmedLeft (r.getWidth() - colW).reduced (12.0f, 14.0f);
    auto mid = r.reduced (colW, 14.0f);

    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.setColour (muted);
    g.drawText ("YOUR MIX", leftCol.removeFromTop (18.0f), juce::Justification::centred);
    g.drawText ("REFERENCE", rightCol.removeFromTop (18.0f), juce::Justification::centred);

    const auto youColour = haveYou && haveRef ? colourFor (match) : (haveYou ? red : redDim);
    drawFoot (g, leftCol.reduced (10.0f, 8.0f).withTrimmedBottom (26.0f), true, youColour, youShake, time);
    drawFoot (g, rightCol.reduced (10.0f, 8.0f).withTrimmedBottom (26.0f), false, haveRef ? red : redDim, refShake, time);

    // centre read-out
    g.setColour (muted);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("MATCH", mid.removeFromTop (22.0f), juce::Justification::centred);

    auto big = mid.removeFromTop (mid.getHeight() * 0.42f);
    g.setFont (juce::FontOptions (54.0f, juce::Font::bold));
    if (match == Match::none)
    {
        g.setColour (muted.withAlpha (0.6f));
        g.drawText ("--", big, juce::Justification::centred);
    }
    else
    {
        g.setColour (colourFor (match));
        g.drawText (juce::String (diff >= 0 ? "+" : "") + juce::String (diff, 1) + " dB", big, juce::Justification::centred);
    }

    auto pill = mid.removeFromTop (34.0f).withSizeKeepingCentre (200.0f, 30.0f);
    const auto mc = colourFor (match);
    g.setColour (mc.withAlpha (0.15f));
    g.fillRoundedRectangle (pill, 15.0f);
    g.setColour (mc.withAlpha (0.9f));
    g.drawRoundedRectangle (pill, 15.0f, 1.4f);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText (labelFor (match), pill, juce::Justification::centred);

    mid.removeFromTop (12.0f);
    g.setFont (juce::FontOptions (13.0f));
    g.setColour (muted);
    auto line = [] (bool ok, float v) { return ok ? juce::String (v, 1) + " dB" : juce::String ("--"); };
    g.drawText ("you " + line (haveYou, live.shakeDb()) + "    ref " + line (haveRef, ref.shakeDb()),
                mid.removeFromTop (20.0f), juce::Justification::centred);
    g.setFont (juce::FontOptions (11.5f));
    g.setColour (muted.withAlpha (0.8f));
    g.drawText ("sub share of the whole mix, level-matched  |  green = within 1 dB",
                mid.removeFromTop (18.0f), juce::Justification::centred);
}

//==============================================================================
//  16th step grid
//==============================================================================
void StepGridView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    ledPanel (g, r, 12.0f);
    auto inner = r.reduced (16.0f, 10.0f);

    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.setColour (muted);
    auto title = inner.removeFromTop (20.0f);
    g.drawText ("16th STEP GRID", title, juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (11.5f));
    g.drawText ("sub on every 16th, averaged over the loop   |   dim = gap / dropout   |   ^ / v = 3 dB+ above / below ref",
                title, juce::Justification::centredRight);

    const auto live = proc.getLiveProfile();
    const auto& ref = proc.getReferenceProfile();
    const bool haveYou = live.valid, haveRef = proc.hasReference() && ref.valid;

    const float labelW = 46.0f;
    auto rows = inner.reduced (0.0f, 4.0f);
    const float rowH = rows.getHeight() / 2.0f;
    const float colW = (rows.getWidth() - labelW) / (float) gs::kStepsPerBar;
    const int current = (int) std::floor (std::fmod (proc.getHostPositionSteps(), 16.0));

    auto rowMax = [] (const gs::StepProfile& p)
    {
        float m = -100.0f;
        for (int s = 0; s < gs::kStepsPerBar; ++s) m = std::max (m, p.stepDb (s));
        return m;
    };
    const float youMax = haveYou ? rowMax (live) : 0.0f, refMax = haveRef ? rowMax (ref) : 0.0f;
    // both rows share one scale (the loudest step of either, 10 dB down to the floor) so you
    // can compare heights directly; quiet steps and gaps stand out
    const float top = std::max (haveYou ? youMax : -100.0f, haveRef ? refMax : -100.0f);
    const float range = 10.0f;

    for (int row = 0; row < 2; ++row)
    {
        const bool isYou = row == 0;
        const bool have = isYou ? haveYou : haveRef;
        const auto& p = isYou ? live : ref;
        auto rr = juce::Rectangle<float> (rows.getX(), rows.getY() + row * rowH, rows.getWidth(), rowH).reduced (0, 3.0f);
        g.setColour (isYou ? text : muted);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (isYou ? "YOU" : "REF", rr.withWidth (labelW), juce::Justification::centredLeft);

        for (int s = 0; s < gs::kStepsPerBar; ++s)
        {
            auto cell = juce::Rectangle<float> (rr.getX() + labelW + s * colW, rr.getY(), colW, rr.getHeight()).reduced (3.0f, 1.0f);
            g.setColour (juce::Colour (0xff111116));
            g.fillRoundedRectangle (cell, 3.0f);
            if (s == current && (proc.isHostPlaying() || ! haveYou))
            {
                g.setColour (red.withAlpha (0.25f));
                g.drawRoundedRectangle (cell.expanded (1.5f), 4.0f, 2.0f);
            }
            if (! have) continue;

            const float db = p.stepDb (s);
            const float lvl = juce::jlimit (0.05f, 1.0f, (db - (top - range)) / range);
            const bool gap = db < (isYou ? youMax : refMax) - 9.0f;
            auto bar = cell.withTrimmedTop (cell.getHeight() * (1.0f - lvl));
            const auto c = gap ? redDim : (s % 4 == 0 ? red.brighter (0.25f) : red);
            g.setColour (c.withAlpha (gap ? 0.5f : 0.9f));
            g.fillRoundedRectangle (bar, 2.5f);
            if (! gap)
            {
                g.setColour (c.withAlpha (0.25f));
                g.drawRoundedRectangle (bar.expanded (1.0f), 3.0f, 2.0f);
            }

            if (isYou && haveRef)
            {
                const float d = db - ref.stepDb (s);
                if (std::abs (d) >= 3.0f)
                {
                    g.setColour (d > 0 ? over : blue);
                    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
                    g.drawText (d > 0 ? "^" : "v", cell.withHeight (14.0f), juce::Justification::centred);
                }
            }
        }
    }

    // beat numbers + beat lines
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    for (int beat = 0; beat < 4; ++beat)
    {
        const float x = rows.getX() + labelW + beat * 4 * colW;
        g.setColour (muted.withAlpha (0.8f));
        g.drawText (juce::String (beat + 1), juce::Rectangle<float> (x, rows.getBottom() - 2.0f, colW, 12.0f),
                    juce::Justification::centred);
        if (beat > 0)
        {
            g.setColour (red.withAlpha (0.35f));
            g.drawVerticalLine ((int) x, rows.getY(), rows.getBottom() - 2.0f);
        }
    }
}

//==============================================================================
//  Reference waveform + 8-bar window
//==============================================================================
juce::Rectangle<float> ReferenceView::waveArea() const
{
    return getLocalBounds().toFloat().reduced (16.0f, 0.0f).withTrimmedTop (34.0f).withTrimmedBottom (10.0f);
}

float ReferenceView::xForTime (double sec) const
{
    auto* ref = proc.getReference();
    auto a = waveArea();
    if (ref == nullptr) return a.getX();
    return a.getX() + (float) (sec / ref->lengthSeconds()) * a.getWidth();
}

double ReferenceView::timeForX (float x) const
{
    auto* ref = proc.getReference();
    auto a = waveArea();
    if (ref == nullptr) return 0.0;
    return (x - a.getX()) / a.getWidth() * ref->lengthSeconds();
}

void ReferenceView::paint (juce::Graphics& g)
{
    auto a = waveArea();                 // the panel itself is drawn by the parent (it wraps the controls too)
    g.setColour (juce::Colour (0xff0d0d11));
    g.fillRoundedRectangle (a, 6.0f);

    auto* ref = proc.getReference();
    if (ref == nullptr || proc.isLoading())
    {
        g.setColour (muted);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText (proc.isLoading() ? "Analyzing reference..." : "Drop a WAV, AIFF or MP3 here, or press LOAD",
                    a, juce::Justification::centred);
        return;
    }

    const double bpm = proc.getBpm(), barSec = 240.0 / bpm;
    const int barsAvail = proc.getNumReferenceBars();
    const int bars = juce::jlimit (1, gs::kBars, barsAvail);
    const double winStart = proc.getGridStartSec() + proc.getWindowStartBar() * barSec;
    const double winEnd = winStart + bars * barSec;
    const float wx0 = xForTime (winStart), wx1 = xForTime (winEnd);

    // waveform
    const auto n = ref->overviewMin.size();
    float peak = 1.0e-6f;
    for (size_t i = 0; i < n; ++i) peak = std::max ({ peak, ref->overviewMax[i], -ref->overviewMin[i] });
    const float mid = a.getCentreY(), half = a.getHeight() * 0.44f / peak;
    for (size_t i = 0; i < n; ++i)
    {
        const float x = a.getX() + a.getWidth() * (float) i / (float) n;
        const bool inside = x >= wx0 && x <= wx1;
        g.setColour (inside ? red.withAlpha (0.9f) : redDim.withAlpha (0.8f));
        g.drawVerticalLine ((int) x, mid - ref->overviewMax[i] * half, mid - ref->overviewMin[i] * half + 1.0f);
    }

    // bar lines (every 4 bars brighter)
    for (int b = 0;; ++b)
    {
        const double t = proc.getGridStartSec() + b * barSec;
        if (t > ref->lengthSeconds()) break;
        if (t < 0) continue;
        g.setColour (juce::Colours::white.withAlpha (b % 4 == 0 ? 0.18f : 0.06f));
        g.drawVerticalLine ((int) xForTime (t), a.getY(), a.getBottom());
    }

    // the 8-bar window
    auto win = juce::Rectangle<float> (wx0, a.getY(), wx1 - wx0, a.getHeight());
    g.setColour (red.withAlpha (0.10f));
    g.fillRoundedRectangle (win, 4.0f);
    for (auto [w, al] : { std::pair { 6.0f, 0.10f }, { 3.0f, 0.25f } })
    {
        g.setColour (red.withAlpha (al));
        g.drawRoundedRectangle (win, 4.0f, w);
    }
    g.setColour (red);
    g.drawRoundedRectangle (win, 4.0f, 1.4f);

    // playhead while listening to B
    const double ph = proc.getRefPlayPositionSec();
    if (ph >= 0)
    {
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.drawVerticalLine ((int) xForTime (ph), a.getY(), a.getBottom());
    }

    // header text
    auto head = getLocalBounds().toFloat().reduced (16.0f, 0.0f).withHeight (32.0f);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.setColour (muted);
    g.drawText ("REFERENCE", head, juce::Justification::centredLeft);
    g.setColour (text);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (ref->name, head.withTrimmedLeft (90.0f).withTrimmedRight (240.0f), juce::Justification::centredLeft, true);
    g.setColour (red);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    const int first = proc.getWindowStartBar() + 1;
    g.drawText (juce::String (bars) + " BARS  (bars " + juce::String (first) + "-" + juce::String (first + bars - 1)
                    + " of " + juce::String (barsAvail) + ")  - drag to move",
                head, juce::Justification::centredRight);
}

void ReferenceView::mouseDown (const juce::MouseEvent& e)
{
    if (proc.getReference() == nullptr) return;
    const double barSec = 240.0 / proc.getBpm();
    const int barUnder = (int) std::floor ((timeForX ((float) e.x) - proc.getGridStartSec()) / barSec);
    const int start = proc.getWindowStartBar();
    const int bars = juce::jlimit (1, gs::kBars, proc.getNumReferenceBars());
    if (barUnder >= start && barUnder < start + bars)
        dragGrabBarOffset = barUnder - start;
    else
    {
        dragGrabBarOffset = bars / 2;
        proc.setWindowStartBar (barUnder - dragGrabBarOffset);
    }
}

void ReferenceView::mouseDrag (const juce::MouseEvent& e)
{
    if (proc.getReference() == nullptr) return;
    const double barSec = 240.0 / proc.getBpm();
    const int barUnder = (int) std::floor ((timeForX ((float) e.x) - proc.getGridStartSec()) / barSec);
    const int target = barUnder - dragGrabBarOffset;
    if (target != proc.getWindowStartBar())
        proc.setWindowStartBar (target);              // snaps to whole bars
}

//==============================================================================
//  Main content (laid out at 1000 x 700, scaled as a whole when resized)
//==============================================================================
GroundShakeContent::GroundShakeContent (GroundShakeProcessor& p)
    : proc (p), feet (p), grid (p), refView (p)
{
    setLookAndFeel (&lnf);
    for (auto* c : std::initializer_list<juce::Component*> { &feet, &grid, &refView, &aButton, &bButton, &loadButton,
                                                             &x2Button, &halfButton, &nudgeLeft, &nudgeRight, &bpmLabel,
                                                             &bpmTitle, &detectedLabel, &offsetLabel })
        addAndMakeVisible (c);

    aButton.setClickingTogglesState (true);
    bButton.setClickingTogglesState (true);
    aButton.setRadioGroupId (1);
    bButton.setRadioGroupId (1);
    (proc.isListeningToReference() ? bButton : aButton).setToggleState (true, juce::dontSendNotification);
    aButton.onClick = [this] { proc.setListenToReference (bButton.getToggleState()); };
    bButton.onClick = [this] { proc.setListenToReference (bButton.getToggleState()); };

    loadButton.onClick = [this] { chooseFile(); };
    x2Button.onClick = [this] { proc.doubleBpm(); };
    halfButton.onClick = [this] { proc.halveBpm(); };
    auto nudge = [this] (int dir)
    {
        // click = 10 ms, shift-click = one 16th
        const bool coarse = juce::ModifierKeys::currentModifiers.isShiftDown();
        proc.nudgeGrid (dir * (coarse ? 15.0 / juce::jmax (1.0, proc.getBpm()) : 0.010));
    };
    nudgeLeft.onClick = [nudge] { nudge (-1); };
    nudgeRight.onClick = [nudge] { nudge (1); };
    nudgeLeft.setTooltip ("Move the grid earlier (shift = one 16th)");
    nudgeRight.setTooltip ("Move the grid later (shift = one 16th)");

    bpmTitle.setText ("BPM", juce::dontSendNotification);
    bpmTitle.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    bpmTitle.setColour (juce::Label::textColourId, muted);
    bpmLabel.setEditable (true);
    bpmLabel.setJustificationType (juce::Justification::centred);
    bpmLabel.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    bpmLabel.setColour (juce::Label::textColourId, red);
    bpmLabel.setColour (juce::Label::outlineColourId, redDim);
    bpmLabel.onTextChange = [this]
    {
        const double v = bpmLabel.getText().retainCharacters ("0123456789.").getDoubleValue();
        if (v >= 40.0 && v <= 300.0) proc.setBpm (v);
        refreshReferenceControls();
    };
    for (auto* l : { &detectedLabel, &offsetLabel })
    {
        l->setFont (juce::FontOptions (12.5f));
        l->setColour (juce::Label::textColourId, muted);
    }

    proc.onReferenceChanged = [this] { refreshReferenceControls(); };
    refreshReferenceControls();
}

GroundShakeContent::~GroundShakeContent()
{
    proc.onReferenceChanged = nullptr;
    setLookAndFeel (nullptr);
}

void GroundShakeContent::refreshReferenceControls()
{
    const bool has = proc.hasReference();
    bpmLabel.setText (has ? juce::String (proc.getBpm(), 2) : juce::String ("--"), juce::dontSendNotification);
    detectedLabel.setText (has ? "detected " + juce::String (proc.getDetectedBpm(), 2) : juce::String(),
                           juce::dontSendNotification);
    const int ms = (int) std::round (proc.getGridOffsetSec() * 1000.0);
    offsetLabel.setText (has ? "grid " + juce::String (ms >= 0 ? "+" : "") + juce::String (ms) + " ms" : juce::String(),
                         juce::dontSendNotification);
    for (auto* b : { &x2Button, &halfButton, &nudgeLeft, &nudgeRight, &bButton })
        b->setEnabled (has);
    bpmLabel.setEditable (has);
    repaint();
}

void GroundShakeContent::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Load a reference track", juce::File(), "*.wav;*.aif;*.aiff;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f.existsAsFile()) proc.loadReferenceAsync (f);
                          });
}

void GroundShakeContent::filesDropped (const juce::StringArray& files, int, int)
{
    for (auto& path : files)
    {
        juce::File f (path);
        if (f.hasFileExtension ("wav;aif;aiff;mp3"))
        {
            proc.loadReferenceAsync (f);
            return;
        }
    }
}

juce::String GroundShakeContent::statusText() const
{
    if (proc.isLoading()) return "Analyzing the reference: tempo, downbeat and sub...";
    if (proc.getLoadError().isNotEmpty()) return proc.getLoadError();
    if (! proc.hasReference()) return "Load a reference track (or drop it here), then play your 8-bar loop.";
    if (! proc.liveHasData()) return "Play your loop in Ableton. Ground Shake listens after the limiter.";
    const auto live = proc.getLiveProfile();
    const float d = live.shakeDb() - proc.getReferenceProfile().shakeDb();
    switch (classify (d))
    {
        case Match::matched:     return "Your feet shake like the reference. Locked in.";
        case Match::slightlyOff: return juce::String ("Close: your sub is ") + juce::String (std::abs (d), 1)
                                        + (d < 0 ? " dB under" : " dB over") + " the reference.";
        case Match::wellUnder:   return "Your sub is " + juce::String (-d, 1) + " dB under the reference. Check the 16th grid for gaps.";
        case Match::wellOver:    return "Your sub is " + juce::String (d, 1) + " dB over the reference. Ease the sub or bass level.";
        case Match::none:
        default: break;
    }
    return {};
}

void GroundShakeContent::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    // subtle vignette
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff14141a), getWidth() * 0.5f, getHeight() * 0.3f,
                                             bg, 0.0f, (float) getHeight(), true));
    g.fillRect (getLocalBounds());

    ledPanel (g, headerR, 10.0f, 0.8f);
    ledPanel (g, refR, 12.0f);
    auto h = headerR.reduced (18.0f, 0.0f);
    g.setColour (muted);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("WOLLER", h.withWidth (80.0f), juce::Justification::centredLeft);
    g.setColour (red);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText ("GROUND SHAKE", h.withTrimmedLeft (72.0f).withWidth (300.0f), juce::Justification::centredLeft);
    g.setColour (muted.withAlpha (0.7f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("phase 1", h.withTrimmedLeft (300.0f).withWidth (80.0f), juce::Justification::centredLeft);

    // status line
    ledPanel (g, statusR, 8.0f, 0.5f);
    g.setColour (text);
    g.setFont (juce::FontOptions (14.0f));
    g.drawText (statusText(), statusR.reduced (16.0f, 0.0f), juce::Justification::centredLeft, true);
}

void GroundShakeContent::resized()
{
    headerR = { 16.0f, 12.0f, 968.0f, 56.0f };
    feetR   = { 16.0f, 80.0f, 968.0f, 250.0f };
    gridR   = { 16.0f, 342.0f, 968.0f, 136.0f };
    refR    = { 16.0f, 490.0f, 968.0f, 152.0f };
    statusR = { 16.0f, 654.0f, 968.0f, 34.0f };

    feet.setBounds (feetR.toNearestInt());
    grid.setBounds (gridR.toNearestInt());
    refView.setBounds (refR.withTrimmedBottom (48.0f).toNearestInt());

    bButton.setBounds (juce::Rectangle<int> (816, 22, 156, 36));
    aButton.setBounds (juce::Rectangle<int> (652, 22, 156, 36));

    // controls row under the waveform
    const int y = (int) refR.getBottom() - 44;
    loadButton.setBounds (32, y, 90, 32);
    bpmTitle.setBounds (150, y, 40, 32);
    bpmLabel.setBounds (190, y + 1, 84, 30);
    detectedLabel.setBounds (280, y, 120, 32);
    x2Button.setBounds (404, y, 50, 32);
    halfButton.setBounds (458, y, 50, 32);
    nudgeLeft.setBounds (540, y, 40, 32);
    nudgeRight.setBounds (584, y, 40, 32);
    offsetLabel.setBounds (630, y, 110, 32);
}

void GroundShakeContent::tick (double t)
{
    feet.tick (t);
    grid.repaint();
    if (proc.getRefPlayPositionSec() >= 0 || proc.isLoading()) refView.repaint();
    repaint (statusR.toNearestInt());
}

//==============================================================================
//  Editor: hosts the content and scales it with the window
//==============================================================================
GroundShakeEditor::GroundShakeEditor (GroundShakeProcessor& p)
    : AudioProcessorEditor (&p), proc (p), content (p)
{
    addAndMakeVisible (content);
    content.setBounds (0, 0, GroundShakeContent::baseW, GroundShakeContent::baseH);
    setResizable (true, true);
    setResizeLimits (700, 490, 1800, 1260);
    getConstrainer()->setFixedAspectRatio ((double) GroundShakeContent::baseW / GroundShakeContent::baseH);
    setSize (GroundShakeContent::baseW, GroundShakeContent::baseH);
    startTimerHz (60);
}

GroundShakeEditor::~GroundShakeEditor() { stopTimer(); }

void GroundShakeEditor::paint (juce::Graphics& g) { g.fillAll (gsui::bg); }

void GroundShakeEditor::resized()
{
    content.setTransform (juce::AffineTransform::scale ((float) getWidth() / GroundShakeContent::baseW));
}

void GroundShakeEditor::timerCallback()
{
    content.tick ((juce::Time::getMillisecondCounterHiRes() - startTime) / 1000.0);
}
