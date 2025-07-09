# AzAudio
Cross-platform realtime audio library for games, written in C.

### Broad-Strokes Goal
Provide high-quality 3D audio for games with more control surfaces than other free libraries with a focus on developer usability (should be accessible for teams with non-programmer sound techs).

### Development Phase
Move fast and break things! I'm currently exploring this problem space, so expect things to change quickly and profoundly. As of now I don't recommend trying to use this library for serious projects, but I am shifting gradually towards API stabilization and documenting changes so users aren't totally in the dark about the goings on.

### General Feature States and TODOs
- [x] Simple spacial attenuation for speakers
- [x] Advanced filtering for headphones (binaural modeling)
- [x] Mixer GUI (Implemented with raylib)
- [ ] Mixer state serialization (saving and loading from disk, etc.)
- [ ] DSP technical analysis tools
	- [x] RMS/Peak Meters (struct azaMeters)
	- [x] Frequency response diagrams
	- [ ] Phase response diagrams
- [ ] Frequency-space
	- [x] Fast Fourier Transform
	- [ ] Frequency-space convolutions
- [ ] MIDI support (for samplers, synths, etc.)
- [ ] Fully dynamic control over effects, samplers, etc. (automation, probably with scheduling)
	- [x] DSP handling config changes without crashing
	- [ ] DSP gracefully handling config changes (without any popping, or other similar artifacts)
- [ ] Plugin chain latency compensation
	- [ ] Measure DSP chain latency
	- [ ] Add delays where necessary to match timing of busses
	- [ ] Gracefully handle latency changes (possibly with some kinda crossfade?)
- [ ] (Ongoing) DSP optimized for realtime games (possibly at the cost of some quality, also preferring lower latency if feasible)
	- [x] Runtime CPU feature detection

### Platform/Backend Support
- [x] Windows
	- [x] WASAPI backend
	- [ ] XAudio2 backend
- [x] Linux
	- [x] Pipewire backend
	- [ ] PulseAudio backend
	- [ ] ALSA backend
	- [ ] JACK backend
- [ ] Mac support
	- [ ] Core Audio backend

### ISA Support
- [x] x86
	- [x] 64-bit
	- [ ] 32-bit (currently untested)
- [ ] ARM
	- [ ] 64-bit
	- [ ] 32-bit

[Documentation (Currently a Stub)](https://singularityazure.github.io/AzAudio)
