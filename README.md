# Formant Roulette

Random formant filter (5 serial peaking bands) with a Pro-Q style display.
Formats: VST3 + Standalone (JUCE 8, CMake).

## Build
Requires CMake 3.22+ and a C++17 compiler (Xcode / Visual Studio 2022 / GCC or Clang; on Linux also JUCE's
usual deps: libasound2-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libfreetype-dev libfontconfig1-dev libgl1-mesa-dev).

    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release

The VST3 ends up in `build/FormantRoulette_artefacts/Release/VST3/`. Copy it to your VST3 folder
(Windows: C:\Program Files\Common Files\VST3, macOS: ~/Library/Audio/Plug-Ins/VST3, Linux: ~/.vst3).
Set COPY_PLUGIN_AFTER_BUILD TRUE in CMakeLists.txt to have JUCE do that for you.

## Use
- RANDOMISE: new formant set. Mode "Vowel" keeps bands inside real F1-F5 ranges; "Wild" goes anywhere.
- CHAOS: how far each randomise travels from the current setting (0-100%).
- GLIDE: how long the filters take to slide to the new targets.
- AUTO: re-randomise every 1 beat / 2 beats / 1 bar / 2 bars (follows host tempo).
- Drag nodes for freq/gain, mouse wheel over a node for Q. Nodes show the target, the curve shows the live (gliding) response.

JUCE is AGPLv3 / commercial licensed - check the terms before distributing.
