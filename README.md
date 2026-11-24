# GitBassRig – Gitarren-/Bass-Rig als VST3 (JUCE)

## Bauen (Windows / macOS / Linux)
1. Voraussetzungen: CMake >= 3.22, C++17-Compiler (Visual Studio 2022 / Xcode / GCC), Git
2. JUCE holen:  `git init && git submodule add https://github.com/juce-framework/JUCE.git JUCE`
3. `cmake -B build -DCMAKE_BUILD_TYPE=Release`
4. `cmake --build build --config Release`
   -> VST3 liegt unter build/GitBassRig_artefacts/Release/VST3/ (wird per COPY_PLUGIN_AFTER_BUILD auch in den System-VST3-Ordner kopiert)
   -> Zum schnellen Testen ohne DAW: Standalone-Variante im selben Artefakte-Ordner.

## Signalweg
Input -> Gate -> Kompressor -> Preamp (4x Oversampling, tanh) -> Bass/Mitten/Höhen/Presence
-> Cabinet (IR-Faltung oder Fallback-Filter) -> Chorus -> Delay -> Hall -> Master

## Tuner
YIN-Pitch-Detection, läuft im GUI-Thread (15 Hz) auf einem Ringpuffer des rohen Eingangssignals.
Stimmungen: Source/Tunings.h (einfach erweiterbar).
