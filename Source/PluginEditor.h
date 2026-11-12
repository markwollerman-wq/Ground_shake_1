#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

namespace gsui
{
    // shiny black + red LED palette
    const juce::Colour bg        { 0xff07070a };
    const juce::Colour panelTop  { 0xff17171c };
    const juce::Colour panelBot  { 0xff0c0c10 };
    const juce::Colour red       { 0xffff2b2b };
    const juce::Colour redDim    { 0xff6e1212 };
    const juce::Colour text      { 0xfff1f1f3 };
    const juce::Colour muted     { 0xff8b8f99 };
    const juce::Colour green     { 0xff35e07a };
    const juce::Colour yellow    { 0xffffd23f };
    const juce::Colour blue      { 0xff3fa9ff };
    const juce::Colour over      { 0xffff3b3b };

    void ledPanel (juce::Graphics&, juce::Rectangle<float>, float radius = 10.0f, float glow = 1.0f);

    // match state of YOUR foot against the reference (tolerance 1 dB)
    enum class Match { none, matched, slightlyOff, wellUnder, wellOver };
    Match classify (float diffDb, float tolerance = 1.0f);
    juce::Colour colourFor (Match);
    juce::String labelFor (Match);

    class LedLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LedLookAndFeel();
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
        void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
        juce::Font getTextButtonFont (juce::TextButton&, int h) override;
    };
}

//==============================================================================
class FeetView : public juce::Component
{
public:
    explicit FeetView (GroundShakeProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void tick (double timeSec);

private:
    void drawFoot (juce::Graphics&, juce::Rectangle<float> area, bool leftFoot, juce::Colour, float shake, double t);
    GroundShakeProcessor& proc;
    double time = 0;
    float youShake = 0, refShake = 0;          // smoothed 0..1
};

//==============================================================================
class StepGridView : public juce::Component
{
public:
    explicit StepGridView (GroundShakeProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;

private:
    GroundShakeProcessor& proc;
};

//==============================================================================
class ReferenceView : public juce::Component
{
public:
    explicit ReferenceView (GroundShakeProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    float xForTime (double sec) const;
    double timeForX (float x) const;
    juce::Rectangle<float> waveArea() const;
    GroundShakeProcessor& proc;
    int dragGrabBarOffset = 0;
};

//==============================================================================
class GroundShakeContent : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    static constexpr int baseW = 1000, baseH = 700;
    explicit GroundShakeContent (GroundShakeProcessor&);
    ~GroundShakeContent() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick (double t);

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void refreshReferenceControls();
    void chooseFile();
    juce::String statusText() const;

    GroundShakeProcessor& proc;
    gsui::LedLookAndFeel lnf;
    FeetView feet;
    StepGridView grid;
    ReferenceView refView;
    juce::TextButton aButton { "A: YOUR MIX" }, bButton { "B: REFERENCE" };
    juce::TextButton loadButton { "LOAD" }, x2Button { "x2" }, halfButton { "/2" },
                     nudgeLeft { "<" }, nudgeRight { ">" };
    juce::Label bpmLabel, bpmTitle, detectedLabel, offsetLabel, refName;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<float> headerR, feetR, gridR, refR, statusR;
};

//==============================================================================
class GroundShakeEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit GroundShakeEditor (GroundShakeProcessor&);
    ~GroundShakeEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    GroundShakeProcessor& proc;
    GroundShakeContent content;
    double startTime = juce::Time::getMillisecondCounterHiRes();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GroundShakeEditor)
};
