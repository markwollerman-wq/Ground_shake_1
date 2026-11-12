// Runs Ground Shake's reference analysis on audio files and prints the results.
// Usage: GroundShakeAnalysisTest file1.wav [file2.wav ...]
#include "../Source/Analysis.h"
#include <cstdio>

int main (int argc, char* argv[])
{
    for (int a = 1; a < argc; ++a)
    {
        gs::ReferenceTrack ref;
        juce::String err;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        if (! gs::loadReference (juce::File (juce::String::fromUTF8 (argv[a])), ref, err))
        {
            std::printf ("%s: ERROR %s\n", argv[a], err.toRawUTF8());
            continue;
        }
        const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
        const int bars = gs::numBarsInReference (ref, ref.detectedBpm, 0.0);
        const auto prof = gs::profileForWindow (ref, ref.detectedBpm, 0.0, 0);
        std::printf ("%s\n  length %.2f s | BPM %.2f | first downbeat %.3f s | whole bars %d | analysis %.0f ms\n",
                     ref.name.toRawUTF8(), ref.lengthSeconds(), ref.detectedBpm, ref.firstDownbeatSec, bars, ms);
        std::printf ("  shake (sub share of mix) %.1f dB\n  per 16th (dB rel. full):", prof.shakeDb());
        for (int s = 0; s < gs::kStepsPerBar; ++s)
            std::printf ("%s%5.1f", s % 4 == 0 ? " |" : "", prof.stepDb (s));
        std::printf ("\n");
    }
    return 0;
}
