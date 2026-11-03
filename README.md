# Palette Melody (stage 1)

A VST3 instrument for composing melodies by motives, inspired by the ideas in
"The Palette - Melody Composing Tool": a piano-roll motive editor, a chord picker that only
offers chords that follow T-S-D functional logic, and motive-development methods.

## Build on Windows (Visual Studio 2022 + CMake)

    cmake -B build -G "Visual Studio 17 2022"
    cmake --build build --config Release --target PaletteMelody_VST3

The plugin ends up in `build\PaletteMelody_artefacts\Release\VST3\Palette Melody.vst3`.
Copy that folder to `C:\Program Files\Common Files\VST3\` and rescan plugins in Reaper.
(There is also a `PaletteMelody_Standalone` target if you want to try it without a DAW.)

## Or let GitHub build it

Push this folder to a GitHub repo, open the Actions tab, run "Build VST3 (Windows)",
and download the `PaletteMelody-VST3` artifact.
