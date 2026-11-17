# Siphon

Siphon is a small command-line program in C++20 that records the sound your
Mac plays to a WAV file. You still hear everything as normal through your
speakers or headphones while it records.

It uses Core Audio *process taps*, which arrived in macOS 14.2. You don't
need a virtual audio driver such as BlackHole, and you don't need to change
your sound output settings.

## Requirements
- macOS 14.2 or later
- Xcode or the Xcode Command Line Tools (`xcode-select --install`)
- CMake 3.25 or later (CLion includes it)

## Build in CLion
1. Open this folder in CLion.
2. Select the `siphon` run configuration.
3. Press Run.

## Build from the command line
```bash
cmake -S . -B build
cmake --build build
```
The program is `build/siphon`.

## Use it
```bash
./build/siphon                      # record until Ctrl+C, to siphon-<date>-<time>.wav
./build/siphon song.wav             # record until Ctrl+C, to song.wav
./build/siphon clip.wav -t 30       # record 30 seconds, then stop
./build/siphon clip.wav -t 30 -f    # the same, and replace clip.wav if it exists
./build/siphon --help
```

| Option | What it does |
|---|---|
| `output.wav` | Where to save. Default: `siphon-<date>-<time>.wav` in the current folder. |
| `-t`, `--seconds N` | Stop after N seconds. Decimals such as `2.5` are allowed. |
| `-f`, `--force` | Replace the output file if it already exists. Without this, siphon stops with an error. |
| `-h`, `--help` | Show the usage text. |

While it records, siphon shows the time and a level meter:
```
Recording WH-1000XM5 (48000 Hz, 2 channels) to song.wav
Press Ctrl+C to stop.
  00:42  [####################..........]  -18 dB
```
If the meter stays at -96 dB while sound plays, see
[Permission](#permission) below.

Press Ctrl+C to stop. Siphon then finishes the file and prints how much it
saved. The file is a standard 16-bit PCM WAV, at the sample rate of your
output device (usually 48000 Hz), in stereo. It plays in QuickTime, Music,
VLC, Audacity and any other audio program.

### What gets recorded
- Siphon records **every app's sound, mixed together**: music, videos,
  calls, notification sounds. Mute or close apps you don't want in the
  recording.
- Your volume level doesn't change the recording. Siphon taps the sound
  before the volume control. You can turn your volume down, or mute it, and
  still get a full-level recording.
- Siphon follows the output device that was the default when it started.
  If you switch to other headphones while it records, restart siphon.
- Some music and video services protect their audio (DRM). macOS gives siphon
  silence for that audio.

### Limits
- A WAV file can't be bigger than 4 GiB. At 48000 Hz stereo, that is about
  6 hours 12 minutes. Siphon stops with an error message when it reaches
  the limit, and keeps everything recorded up to that point.
- **Caution:** Do not end siphon with `kill -9`. If siphon does not stop
  correctly, the WAV header does not get the real size. Most programs then
  show the file as empty, although the samples are in it.

  Stop siphon with Ctrl+C.

## Permission
The first time siphon runs, macOS should ask for permission to record
system audio. The question names your terminal app (Terminal, iTerm,
CLion), not siphon, because macOS gives the permission to the app that
started siphon.

If you said no, or no question appeared, the recording is silent. macOS
doesn't report an error for this, so siphon prints a warning when a
recording is completely silent. To fix it:

1. Open **System Settings > Privacy & Security > Screen & System Audio
   Recording**.
2. Turn on your terminal app under **System Audio Recording Only** (or in
   the main list).
3. Quit the terminal app and open it again. Then run siphon again.

## How it works
```
 apps ──► macOS mixer ──► speakers / headphones   (you still hear it)
                 │
                 └─► process tap ──► aggregate device ──► IO proc
                                                            │  real-time audio thread
                                                            ▼
                                                       RingBuffer
                                                            │  main thread, every 50 ms
                                                            ▼
                                       float → 16-bit ──► WavWriter ──► file.wav
```
1. A **process tap** (`AudioHardwareCreateProcessTap`) taps the mixed
   output of every app. It doesn't mute it.
2. A tap can't be read directly. Siphon puts it into a private **aggregate
   device**, where it shows up as an input, like a microphone.
3. Core Audio calls siphon's **IO proc** on its real-time audio thread
   about every 10 ms. That thread must never wait, so it only copies the
   samples into a lock-free **ring buffer**.
4. The **main thread** empties the ring every 50 ms, turns the float samples
   into 16-bit integers, and writes them to the WAV file.
5. On Ctrl+C, siphon stops the tap, writes what is left in the ring, and
   puts the real sizes into the WAV header.

## Files
| File | What it does |
|---|---|
| `src/system_tap.h/.mm` | The tap, the aggregate device and the IO proc (Objective-C++, macOS only) |
| `src/ring_buffer.h/.cpp` | The lock-free queue from the audio thread to the main thread |
| `src/sample_math.h/.cpp` | Float to 16-bit conversion, and the peak level in dB |
| `src/wav_writer.h/.cpp` | The WAV file format |
| `src/options.h/.cpp` | The command-line options |
| `src/main.cpp` | The program: options, the drain loop, the status line |
| `Info.plist` | The text of the permission question, built into the program |

Everything except `system_tap.mm` and `main.cpp` is portable C++. It builds
and its tests run on Linux too, which is what CI does.

## Run the tests
Build the project first, then run CTest:
```bash
ctest --test-dir build --output-on-failure
```
The tests don't record any audio and need no permission.
