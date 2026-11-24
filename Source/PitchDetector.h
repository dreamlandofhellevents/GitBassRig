#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <cmath>

// YIN-Algorithmus (de Cheveigné & Kawahara, 2002), vereinfachte Fassung.
// x muss mindestens n Samples enthalten. Rückgabe: Frequenz in Hz oder -1.
struct PitchDetector
{
    static float detect (const float* x, int n, double sr,
                         float minHz, float maxHz, float threshold = 0.12f)
    {
        const int tauMax = juce::jmin ((int) (sr / minHz), n / 2);
        const int tauMin = juce::jmax (2, (int) (sr / maxHz));
        const int w = n - tauMax;
        if (w < tauMax || tauMin >= tauMax - 2)
            return -1.f;

        std::vector<float> cmnd ((size_t) tauMax, 1.f);
        float running = 0.f;

        for (int tau = 1; tau < tauMax; ++tau)
        {
            float s = 0.f;
            for (int i = 0; i < w; ++i)
            {
                const float d = x[i] - x[i + tau];
                s += d * d;
            }
            running += s;
            cmnd[(size_t) tau] = running > 0.f ? s * (float) tau / running : 1.f;
        }

        // Erst strenge Schwelle, bei Misserfolg schrittweise lockern (echte Gitarrensignale sind unsauber)
        int found = -1;
        for (float thr : { threshold, 0.2f, 0.3f })
        {
            int t = tauMin;
            while (t < tauMax - 1)
            {
                if (cmnd[(size_t) t] < thr)
                {
                    while (t + 1 < tauMax - 1 && cmnd[(size_t) t + 1] < cmnd[(size_t) t])
                        ++t;
                    found = t;
                    break;
                }
                ++t;
            }
            if (found >= 0) break;
        }
        if (found < 0)
            return -1.f;
        const int tau = found;

        // Parabolische Interpolation für Sub-Sample-Genauigkeit
        const float a = cmnd[(size_t) tau - 1], b = cmnd[(size_t) tau], c = cmnd[(size_t) tau + 1];
        const float denom = a - 2.f * b + c;
        const float shift = std::abs (denom) > 1e-9f ? 0.5f * (a - c) / denom : 0.f;
        return (float) (sr / ((double) tau + shift));
    }
};
