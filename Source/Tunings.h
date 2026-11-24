#pragma once
#include <juce_core/juce_core.h>
#include <vector>

// Saiten als MIDI-Noten, von tief nach hoch (E2 = 40, A2 = 45, ...)
struct Tuning
{
    juce::String name;
    std::vector<int> midi;
};

inline const std::vector<Tuning>& getTunings()
{
    static const std::vector<Tuning> t = {
        // ---- Gitarre ----
        { "Gitarre: Standard (EADGBE)",        { 40, 45, 50, 55, 59, 64 } },
        { "Gitarre: Halbton tiefer (Eb)",      { 39, 44, 49, 54, 58, 63 } },
        { "Gitarre: Ganzton tiefer (D)",       { 38, 43, 48, 53, 57, 62 } },
        { "Gitarre: Drop D",                   { 38, 45, 50, 55, 59, 64 } },
        { "Gitarre: Drop C",                   { 36, 43, 48, 53, 57, 62 } },
        { "Gitarre: DADGAD",                   { 38, 45, 50, 55, 57, 62 } },
        { "Gitarre: Open G (DGDGBD)",          { 38, 43, 50, 55, 59, 62 } },
        { "Gitarre: Open D (DADF#AD)",         { 38, 45, 50, 54, 57, 62 } },
        // ---- Bass ----
        { "Bass: 4-Saiter Standard (EADG)",    { 28, 33, 38, 43 } },
        { "Bass: 4-Saiter Drop D",             { 26, 33, 38, 43 } },
        { "Bass: 4-Saiter Halbton tiefer",     { 27, 32, 37, 42 } },
        { "Bass: 5-Saiter (BEADG)",            { 23, 28, 33, 38, 43 } },
        { "Bass: 6-Saiter (BEADGC)",           { 23, 28, 33, 38, 43, 48 } },
    };
    return t;
}

inline double midiToHz (int midiNote, double refA)
{
    return refA * std::pow (2.0, (midiNote - 69) / 12.0);
}
