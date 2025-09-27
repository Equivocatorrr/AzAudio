# Changelog

## Philosophy

This project follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html), and this document will describe notable changes between versions.
Note that not all changes will be documented here, mainly just the ones that change API functionality.
The goal is that this changelog shouldn't be a big burden on either the developers of AzAudio, nor those that use it.
To this end, the changes for each version will be organized based on importance, with API-breaking changes coming first.
As a bonus, we may try to document porting steps for users of this library that want to upgrade (we'll see how that goes).

## [Unreleased]

Nothing yet...

## [v0.3.0](https://github.com/Equivocatorrr/AzAudio/releases/tag/v0.3.0)

### Removed
- AZA_TRUE and AZA_FALSE defines from AzAudio.h, instead choosing to opt for stdbool.h

### Changed
- azaSampler interface
	- azaSamplerConfig is almost completely different
	- Uses ADSR envelopes
	- Can now have multiple instances of sounds with independent pitch and gain
	- Pitch and gain follows targets in a predictable, linear fashion
- azaFilter interface
	- Has up to 16 poles
	- azaFilterConfig has a `poles` field which determines the rolloff of the filter
```C
typedef struct azaFilterConfig {
	azaFilterKind kind;
	// pole count - 1 (defaults to AZA_FILTER_6_DB_O)
	uint32_t poles; /* new!!! */
	// Cutoff frequency in Hz
	float frequency;
	// Blends the effect output with the dry signal where 1 is fully dry and 0 is fully wet.
	float dryMix;
} azaFilterConfig;
```
- azaMake... functions no longer set the owned bit. Instead, it's set when created through the Mixer GUI specifically. This puts the responsibility of freeing things back into the hands of whoever called azaMake, as the examples were assuming already, resulting in double frees whenever something was removed.
- Do a better job about actually freeing things. Deinitting an azaTrack now frees any owned DSP on it.
- AZA_MIN, AZA_MAX, and AZA_CLAMP macros were moved from helpers.h to math.h
- azaSampleWithKernel was almost completely redone to allow for arbitrary rates, as is needed in the general case to avoid aliasing in azaSampler and azaDelayDynamic (and by extension azaSpatialize). Some serious SIMD performance work was done to make this highly dynamic, high-quality, and low performance cost.
	- There is further specialization opportunities for deinterlaced (and/or single-channel) sources which may come in handy for many sampler instances.
	- Some code paths may be pruned later to reduce duplication, as they're not all particularly worthwhile (looking at you sse3), pending some more testing. For now, unless a bug is found in this code, it should be okay to leave it be for a while as I'm fairly happy with the outcome.
	- There remains a minor issue with azaSampler and azaDelayDynamic where the swapping of lanczos kernel sizes (to follow changing rates at an almost constant performance cost) causes very quiet (think -72dB) pops in the output. This may be difficult to hear with a rich audio source, but if the source is low-pitched or narrow-band I imagine it would become audible to a careful listener. I'm not quite sure what the best course of action is to handle this, as interpolating would double our kernel sampling costs (I worked hard for that performance dammit!). Doing such an interpolation optimally would likely increase the code complexity a good amount, and it's already a lot to look at (1200 lines of specialization code for a 50-line function is already a lot!).

### Added
- ADSR envelopes
- azaFollowerLinear for linearly following target values at a given rate
- in AzAudio/math.h
	- azaWindow functions (Hann, Blackman, Blackman-Harris, Nuttall)
	- Additional windowed sinc functions

### Changed (Internal)
- Mixer GUI
	- Finished mouse dragging and implemented it for track scrollbar and sliders
	- Replaced overcomplicated context menu system with straightforward functions and helpers
	- Made context menus smarter about context
	- Added confirmation before removing a track
	- Most mouseovers check mouseDepth now, effectively communicating their lack of interactivity when a context menu is open
	- Add GUI error reporting using context menus and some personality ;)

### Fixed
- azaFFT sinusoid integration error causing erroneous readings in azaMonitorSpectrum

## [v0.2.1](https://github.com/Equivocatorrr/AzAudio/releases/tag/v0.2.1)

### Added
- Complex FFT
- Spectrum Monitor plugin

## [v0.2.0](https://github.com/Equivocatorrr/AzAudio/releases/tag/v0.2.0)

### Added
- Changelog