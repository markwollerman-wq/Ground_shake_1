#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <vector>

// =============================================================================
//  Ground Shake - analysis engine (no UI, no plugin code)
//
//  Two low-end zones:
//      FEEL   20-60 Hz   the sub you feel in your feet / chest
//      PUNCH  60-120 Hz  kick thump and bass body
//
//  "Shake" = the sub's share of the whole mix, in dB:
//      10*log10( (feel + 0.5*punch) / full )
//  Using a share (not an absolute level) is what makes the comparison
//  level-matched: a louder master doesn't automatically "shake more".
// =============================================================================
namespace gs
{
constexpr int kStepsPerBar = 16;
constexpr int kBars        = 8;
constexpr int kLoopSteps   = kStepsPerBar * kBars;   // 128 sixteenths in an 8-bar loop
constexpr float kPunchWeight = 0.5f;

//------------------------------------------------------------------------------
// Simple biquad (RBJ cookbook), per channel state handled by caller
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    void setLowPass  (double sr, double f, double q = 0.70710678);
    void setHighPass (double sr, double f, double q = 0.70710678);
    void reset() { z1 = z2 = 0; }

    inline float process (float x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }
};

// Splits a mono signal into the FEEL and PUNCH zones (4th-order slopes)
struct BandSplitter
{
    void prepare (double sampleRate, float feelLo = 20.0f, float split = 60.0f, float punchHi = 120.0f);
    void reset();
    inline void process (float x, float& feel, float& punch) noexcept
    {
        feel  = fLp2.process (fLp1.process (fHp2.process (fHp1.process (x))));
        punch = pLp2.process (pLp1.process (pHp2.process (pHp1.process (x))));
    }

private:
    Biquad fHp1, fHp2, fLp1, fLp2, pHp1, pHp2, pLp1, pLp2;
};

//------------------------------------------------------------------------------
// One 16-step picture of a loop: energy per sixteenth, folded over all bars
struct StepProfile
{
    std::array<float, kStepsPerBar> feel {};    // mean power per step
    std::array<float, kStepsPerBar> punch {};
    std::array<float, kStepsPerBar> full {};
    float feelTotal = 0, punchTotal = 0, fullTotal = 0;
    bool valid = false;

    // the sub's share of the mix in dB (see top of file)
    float shakeDb() const;
    // per-step sub level in dB relative to the loop's FULL-band mean power
    float stepDb (int step) const;
};

float powerToDb (float p) noexcept;

//------------------------------------------------------------------------------
// A loaded reference track and its pre-computed analysis
struct ReferenceTrack
{
    juce::String name;
    juce::File file;
    double sampleRate = 44100.0;
    juce::AudioBuffer<float> audio;               // stereo, at the analysis rate

    // envelopes, one value per hop
    int hop = 256;
    std::vector<float> feelEnv, punchEnv, fullEnv;
    std::vector<float> clickEnv;                  // 1-4 kHz: the kick's click, used to place the beat

    // waveform overview for the display (min/max pairs)
    std::vector<float> overviewMin, overviewMax;

    // tempo grid
    double detectedBpm = 0.0;
    double firstDownbeatSec = 0.0;                // grid anchor (a bar line)

    double lengthSeconds() const { return audio.getNumSamples() / sampleRate; }
};

// Loads WAV / AIFF / MP3 and runs the analysis. Returns false + error text on failure.
bool loadReference (const juce::File& file, ReferenceTrack& out, juce::String& error);

// Tempo + downbeat from the envelopes (called by loadReference)
void detectTempo (ReferenceTrack& ref);

// Step profile of an 8-bar window starting at bar `startBar` of the grid
// (bars counted from firstDownbeatSec; offsetSec shifts the grid, the nudge)
StepProfile profileForWindow (const ReferenceTrack& ref, double bpm, double gridOffsetSec, int startBar,
                              int bars = kBars);

// How many whole bars the reference holds with this tempo/grid
int numBarsInReference (const ReferenceTrack& ref, double bpm, double gridOffsetSec);

//------------------------------------------------------------------------------
// Live accumulator for YOUR mix, fed sample by sample with the host's position.
// Keeps the latest value for each of the 128 sixteenths in the 8-bar loop.
class LiveStepMeter
{
public:
    void reset();
    // positionInSteps = host ppq * 4 (sixteenths since song start)
    void addSample (double positionInSteps, float feelSq, float punchSq, float fullSq) noexcept;
    StepProfile profile() const;           // folded over the bars that have data
    bool hasData() const;

private:
    struct Slot { double feel = 0, punch = 0, full = 0; int count = 0; long serial = 0; };
    std::array<Slot, kLoopSteps> done {};   // last finished value per slot
    Slot current;
    int currentSlot = -1;
    long serial = 0;                         // counts finished slots; old ones drop out after one loop
};
} // namespace gs
