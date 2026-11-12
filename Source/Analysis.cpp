#include "Analysis.h"
#include <cmath>
#include <algorithm>
#include <numeric>

namespace gs
{
//==============================================================================
void Biquad::setLowPass (double sr, double f, double q)
{
    const double w0 = juce::MathConstants<double>::twoPi * f / sr;
    const double c = std::cos (w0), alpha = std::sin (w0) / (2.0 * q), a0 = 1.0 + alpha;
    b0 = (1.0 - c) * 0.5 / a0;  b1 = (1.0 - c) / a0;  b2 = b0;
    a1 = -2.0 * c / a0;         a2 = (1.0 - alpha) / a0;
}

void Biquad::setHighPass (double sr, double f, double q)
{
    const double w0 = juce::MathConstants<double>::twoPi * f / sr;
    const double c = std::cos (w0), alpha = std::sin (w0) / (2.0 * q), a0 = 1.0 + alpha;
    b0 = (1.0 + c) * 0.5 / a0;  b1 = -(1.0 + c) / a0;  b2 = b0;
    a1 = -2.0 * c / a0;         a2 = (1.0 - alpha) / a0;
}

void BandSplitter::prepare (double sr, float feelLo, float split, float punchHi)
{
    // two cascaded Butterworth stages = 4th-order (24 dB/oct) slopes
    fHp1.setHighPass (sr, feelLo);  fHp2.setHighPass (sr, feelLo);
    fLp1.setLowPass  (sr, split);   fLp2.setLowPass  (sr, split);
    pHp1.setHighPass (sr, split);   pHp2.setHighPass (sr, split);
    pLp1.setLowPass  (sr, punchHi); pLp2.setLowPass  (sr, punchHi);
    reset();
}

void BandSplitter::reset()
{
    for (auto* b : { &fHp1, &fHp2, &fLp1, &fLp2, &pHp1, &pHp2, &pLp1, &pLp2 })
        b->reset();
}

//==============================================================================
float powerToDb (float p) noexcept { return 10.0f * std::log10 (std::max (p, 1.0e-12f)); }

float StepProfile::shakeDb() const
{
    if (! valid || fullTotal <= 0.0f) return -100.0f;
    return powerToDb ((feelTotal + kPunchWeight * punchTotal) / fullTotal);
}

float StepProfile::stepDb (int s) const
{
    if (! valid || fullTotal <= 0.0f) return -100.0f;
    return powerToDb ((feel[(size_t) s] + kPunchWeight * punch[(size_t) s]) / fullTotal);
}

//==============================================================================
bool loadReference (const juce::File& file, ReferenceTrack& out, juce::String& error)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr)
    {
        error = "Can't read this file. Use WAV, AIFF or MP3.";
        return false;
    }

    const double sr = reader->sampleRate;
    const auto maxSamples = (juce::int64) (sr * 60.0 * 15.0);          // 15 minutes is plenty
    const int n = (int) std::min (reader->lengthInSamples, maxSamples);
    if (n < (int) (sr * 1.0))
    {
        error = "This file is shorter than 1 second.";
        return false;
    }

    out = ReferenceTrack();
    out.file = file;
    out.name = file.getFileNameWithoutExtension();
    out.sampleRate = sr;
    out.audio.setSize (2, n);
    reader->read (&out.audio, 0, n, 0, true, reader->numChannels > 1);
    if (reader->numChannels == 1)
        out.audio.copyFrom (1, 0, out.audio, 0, 0, n);

    // envelopes
    out.hop = juce::jmax (64, (int) std::round (sr / 172.0));          // ~5.8 ms
    const int frames = n / out.hop;
    out.feelEnv.assign ((size_t) frames, 0.0f);
    out.punchEnv.assign ((size_t) frames, 0.0f);
    out.fullEnv.assign ((size_t) frames, 0.0f);
    out.clickEnv.assign ((size_t) frames, 0.0f);

    BandSplitter split;
    split.prepare (sr);
    Biquad c1, c2, c3, c4;                       // 1-4 kHz band for the kick's click
    c1.setHighPass (sr, 1000.0); c2.setHighPass (sr, 1000.0);
    c3.setLowPass (sr, 4000.0);  c4.setLowPass (sr, 4000.0);
    const float* L = out.audio.getReadPointer (0);
    const float* R = out.audio.getReadPointer (1);
    for (int f = 0; f < frames; ++f)
    {
        double fe = 0, pu = 0, fu = 0, cl = 0;
        for (int i = f * out.hop, end = i + out.hop; i < end; ++i)
        {
            const float m = 0.5f * (L[i] + R[i]);
            float a, b;
            split.process (m, a, b);
            const float c = c4.process (c3.process (c2.process (c1.process (m))));
            fe += (double) a * a;  pu += (double) b * b;  fu += (double) m * m;  cl += (double) c * c;
        }
        out.clickEnv[(size_t) f] = (float) (cl / out.hop);
        out.feelEnv[(size_t) f]  = (float) (fe / out.hop);
        out.punchEnv[(size_t) f] = (float) (pu / out.hop);
        out.fullEnv[(size_t) f]  = (float) (fu / out.hop);
    }

    // waveform overview
    const int points = 1600;
    out.overviewMin.assign (points, 0.0f);
    out.overviewMax.assign (points, 0.0f);
    for (int p = 0; p < points; ++p)
    {
        const int a = (int) ((juce::int64) n * p / points), b = (int) ((juce::int64) n * (p + 1) / points);
        float lo = 0, hi = 0;
        for (int i = a; i < b; ++i)
        {
            const float m = 0.5f * (L[i] + R[i]);
            lo = std::min (lo, m);  hi = std::max (hi, m);
        }
        out.overviewMin[(size_t) p] = lo;  out.overviewMax[(size_t) p] = hi;
    }

    detectTempo (out);
    return true;
}

//==============================================================================
namespace
{
    // positive log-energy change = how much "new" energy starts in this frame
    std::vector<float> onsetCurve (const std::vector<float>& env)
    {
        std::vector<float> o (env.size(), 0.0f);
        for (size_t i = 1; i < env.size(); ++i)
            o[i] = std::max (0.0f, std::log (env[i] + 1.0e-9f) - std::log (env[i - 1] + 1.0e-9f));
        return o;
    }

    void removeLocalMean (std::vector<float>& o, int radius)
    {
        std::vector<double> csum (o.size() + 1, 0.0);
        for (size_t i = 0; i < o.size(); ++i) csum[i + 1] = csum[i] + o[i];
        std::vector<float> r (o.size());
        for (size_t i = 0; i < o.size(); ++i)
        {
            const size_t a = i > (size_t) radius ? i - (size_t) radius : 0;
            const size_t b = std::min (o.size(), i + (size_t) radius + 1);
            r[i] = std::max (0.0f, o[i] - (float) ((csum[b] - csum[a]) / (double) (b - a)));
        }
        o.swap (r);
    }

    float sampleAt (const std::vector<float>& v, double pos)
    {
        if (pos < 0 || pos >= (double) v.size() - 1) return 0.0f;
        const auto i = (size_t) pos;
        const float t = (float) (pos - (double) i);
        return v[i] * (1.0f - t) + v[i + 1] * t;
    }
}

void detectTempo (ReferenceTrack& ref)
{
    const size_t N = ref.feelEnv.size();
    const double hopSec = ref.hop / ref.sampleRate;

    std::vector<float> low (N), full = ref.fullEnv;
    for (size_t i = 0; i < N; ++i) low[i] = ref.feelEnv[i] + ref.punchEnv[i];
    auto oLow = onsetCurve (low), oFull = onsetCurve (full);
    std::vector<float> o (N);
    for (size_t i = 0; i < N; ++i) o[i] = oLow[i] + 0.5f * oFull[i];
    removeLocalMean (o, (int) std::round (0.25 / hopSec));

    // --- coarse: autocorrelation over 70-190 BPM with a beat-multiple comb
    auto ac = [&] (double lag)
    {
        if (lag < 1 || lag >= (double) N / 2) return 0.0;
        double s = 0;
        const size_t L = (size_t) lag;
        const double t = lag - (double) L;
        for (size_t i = 0; i + L + 1 < N; ++i)
            s += o[i] * ((1.0 - t) * o[i + L] + t * o[i + L + 1]);
        return s / (double) (N - L);
    };
    double bestBpm = 120, bestScore = -1;
    for (double bpm = 70; bpm <= 190; bpm += 0.5)
    {
        const double lag = 60.0 / bpm / hopSec;
        const double s = ac (lag) + 0.5 * ac (2 * lag) + 0.25 * ac (4 * lag);
        if (s > bestScore) { bestScore = s; bestBpm = bpm; }
    }
    while (bestBpm < 100) bestBpm *= 2;                 // dance music: fold 75 -> 150 etc.
    while (bestBpm >= 200) bestBpm *= 0.5;

    // --- fine: pulse-train fit over tempo and phase
    double fineBpm = bestBpm, fineScore = -1, fineFrame = 0;
    for (double bpm = bestBpm - 1.5; bpm <= bestBpm + 1.5; bpm += 0.01)
    {
        const double period = 60.0 / bpm / hopSec;
        for (double ph = 0; ph < period; ph += 0.5)
        {
            double s = 0;
            int beats = 0;
            for (double p = ph; p < (double) N - 1; p += period, ++beats)
                s += sampleAt (o, p);
            s /= std::max (1, beats);
            if (s > fineScore) { fineScore = s; fineBpm = bpm; fineFrame = ph; }
        }
    }
    // round to 0.01 BPM; most dance tracks sit on a whole BPM
    if (std::abs (fineBpm - std::round (fineBpm)) < 0.12) fineBpm = std::round (fineBpm);
    ref.detectedBpm = std::round (fineBpm * 100.0) / 100.0;

    // --- beat PHASE: rolling basses put big low-end onsets BETWEEN the kicks and the sub
    //     peaks late, so the beat is placed on the kick's click instead: linear energy
    //     rises in the 1-4 kHz band (tested on rolling-bass techno at 146-155 BPM: 0-4 ms).
    {
        std::vector<float> oKick (N, 0.0f);
        for (size_t i = 1; i < N; ++i) oKick[i] = std::max (0.0f, ref.clickEnv[i] - ref.clickEnv[i - 1]);
        const double per = 60.0 / ref.detectedBpm / hopSec;
        double bestPh = 0, bestS = -1;
        for (double ph = 0; ph < per; ph += 0.25)
        {
            double sc = 0;
            for (double p = ph; p < (double) N - 1; p += per)
                sc += sampleAt (oKick, p);
            if (sc > bestS) { bestS = sc; bestPh = ph; }
        }
        fineFrame = bestPh;
    }

    // --- downbeat: which of the 4 beats gets the most "new" energy (crashes, phrase starts)
    const double period = 60.0 / ref.detectedBpm / hopSec;
    double bestBar = -1;
    int bestOffset = 0;
    for (int m = 0; m < 4; ++m)
    {
        double s = 0;
        for (double p = fineFrame + m * period; p < (double) N - 3; p += 4 * period)
        {
            float pk = 0;
            for (int d = -2; d <= 2; ++d) pk = std::max (pk, sampleAt (oFull, p + d));
            s += pk;
        }
        if (s > bestBar) { bestBar = s; bestOffset = m; }
    }
    double first = (fineFrame + bestOffset * period) * hopSec;
    const double barSec = 240.0 / ref.detectedBpm;
    const double beatSec = barSec / 4.0;

    // exported loops (short files) start on a bar line: if a beat sits at the very start, use it
    const double phase0 = std::fmod (fineFrame * hopSec, beatSec);
    if (ref.lengthSeconds() < 60.0 && (phase0 < 0.035 || beatSec - phase0 < 0.035))
    {
        ref.firstDownbeatSec = phase0 < 0.035 ? phase0 : 0.0;
        if (ref.firstDownbeatSec < 0.035) ref.firstDownbeatSec = 0.0;
        return;
    }
    while (first - barSec >= -0.02) first -= barSec;    // earliest bar line in the file
    if (first < 0) first += barSec;
    if (first > barSec - 0.03) first -= barSec;         // within a few ms of 0 -> start of file
    ref.firstDownbeatSec = std::max (0.0, first);
}

//==============================================================================
int numBarsInReference (const ReferenceTrack& ref, double bpm, double gridOffsetSec)
{
    if (bpm <= 0) return 0;
    const double barSec = 240.0 / bpm;
    const double usable = ref.lengthSeconds() - (ref.firstDownbeatSec + gridOffsetSec);
    return juce::jmax (0, (int) std::floor (usable / barSec + 0.02));
}

StepProfile profileForWindow (const ReferenceTrack& ref, double bpm, double gridOffsetSec, int startBar, int bars)
{
    StepProfile p;
    if (bpm <= 0 || ref.feelEnv.empty()) return p;

    const double hopSec = ref.hop / ref.sampleRate;
    const double barSec = 240.0 / bpm, stepSec = barSec / kStepsPerBar;
    const double start = ref.firstDownbeatSec + gridOffsetSec + startBar * barSec;
    const auto frames = ref.feelEnv.size();

    std::array<double, kStepsPerBar> fe {}, pu {}, fu {};
    std::array<int, kStepsPerBar> cnt {};
    double tf = 0, tp = 0, tu = 0;
    long tc = 0;

    for (int b = 0; b < bars; ++b)
        for (int s = 0; s < kStepsPerBar; ++s)
        {
            const double t0 = start + (b * kStepsPerBar + s) * stepSec;
            const auto i0 = (long) std::floor (t0 / hopSec), i1 = (long) std::floor ((t0 + stepSec) / hopSec);
            for (long i = std::max (0L, i0); i < i1 && i < (long) frames; ++i)
            {
                fe[(size_t) s] += ref.feelEnv[(size_t) i];
                pu[(size_t) s] += ref.punchEnv[(size_t) i];
                fu[(size_t) s] += ref.fullEnv[(size_t) i];
                ++cnt[(size_t) s];
                tf += ref.feelEnv[(size_t) i];  tp += ref.punchEnv[(size_t) i];  tu += ref.fullEnv[(size_t) i];
                ++tc;
            }
        }

    if (tc == 0) return p;
    for (size_t s = 0; s < (size_t) kStepsPerBar; ++s)
        if (cnt[s] > 0)
        {
            p.feel[s] = (float) (fe[s] / cnt[s]);
            p.punch[s] = (float) (pu[s] / cnt[s]);
            p.full[s] = (float) (fu[s] / cnt[s]);
        }
    p.feelTotal = (float) (tf / tc);  p.punchTotal = (float) (tp / tc);  p.fullTotal = (float) (tu / tc);
    p.valid = true;
    return p;
}

//==============================================================================
void LiveStepMeter::reset()
{
    done = {};
    current = {};
    currentSlot = -1;
    serial = 0;
}

void LiveStepMeter::addSample (double positionInSteps, float feelSq, float punchSq, float fullSq) noexcept
{
    long whole = (long) std::floor (positionInSteps);
    int slot = (int) (((whole % kLoopSteps) + kLoopSteps) % kLoopSteps);
    if (slot != currentSlot)
    {
        if (currentSlot >= 0 && current.count > 0)
        {
            current.serial = ++serial;
            done[(size_t) currentSlot] = current;
        }
        current = {};
        currentSlot = slot;
    }
    current.feel += feelSq;  current.punch += punchSq;  current.full += fullSq;
    ++current.count;
}

bool LiveStepMeter::hasData() const
{
    for (auto& s : done) if (s.count > 0) return true;
    return false;
}

StepProfile LiveStepMeter::profile() const
{
    StepProfile p;
    std::array<double, kStepsPerBar> fe {}, pu {}, fu {};
    std::array<int, kStepsPerBar> cnt {};
    double tf = 0, tp = 0, tu = 0;
    long tc = 0;
    for (size_t i = 0; i < done.size(); ++i)
    {
        const auto& s = done[i];
        if (s.count == 0 || s.serial + (long) kLoopSteps < serial) continue;   // only the last loop pass
        const size_t st = i % (size_t) kStepsPerBar;
        fe[st] += s.feel / s.count;  pu[st] += s.punch / s.count;  fu[st] += s.full / s.count;
        ++cnt[st];
        tf += s.feel;  tp += s.punch;  tu += s.full;  tc += s.count;
    }
    if (tc == 0) return p;
    for (size_t s = 0; s < (size_t) kStepsPerBar; ++s)
        if (cnt[s] > 0)
        {
            p.feel[s] = (float) (fe[s] / cnt[s]);
            p.punch[s] = (float) (pu[s] / cnt[s]);
            p.full[s] = (float) (fu[s] / cnt[s]);
        }
    p.feelTotal = (float) (tf / tc);  p.punchTotal = (float) (tp / tc);  p.fullTotal = (float) (tu / tc);
    p.valid = true;
    return p;
}
} // namespace gs
