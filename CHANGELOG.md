# Changelog

## Philosophy

This project follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html), and this document will describe notable changes between versions.
Note that not all changes will be documented here, mainly just the ones that change API functionality.
The goal is that this changelog shouldn't be a big burden on either the developers of AzAudio, nor those that use it.
To this end, the changes for each version will be organized based on importance, with API-breaking changes coming first.
As a bonus, we may try to document porting steps for users of this library that want to upgrade (we'll see how that goes).

## [Unreleased]

### Implemented Latency Compensation in the Mixer
- Measure latency
- Use latency measurements to delay tracks with lower latency to keep time parity
- This prevents undesired comb filtering in parallel processing setups

### Complete Rework of azaDSP Plugin Interface
- azaDSP header is very different
	- much simplified as the dynamic allocation size was totally removed in favor of fixed struct sizes
	- fat struct with function pointers instead of a type enum (taking the azaDSPUser interface concept and using it for everything)
	- Now owns its mixer GUI selection state, allowing multiple plugins to be selected at once.
	- Removed pointless bit packing of metadata
	- Explicitly-specified struct size for backwards and forwards compatibility
		- remember to use this instead of sizeof for copying their state
	- Single version number for backwards-compatible changes post-1.0
	- Explicitly-reserved padding for expanding functionality in the future
- azaDSPProcess reflects the new interface, as does the backend
- azaDSP no longer contains `pNext` and `pPrev` fields, as plugin chains are now managed by `azaDSPChain`
```C
// Information about DSP regarding azaBuffer management, used especially in the mixer for latency compensation
typedef azaDSPSpecs (*fp_azaDSPGetSpecs)(void *dsp, uint32_t samplerate);
// Process interface that always takes a src and dst buffer, as well as flags to help gracefully deal with setup changes
// For many cases, dst and src will refer to the exact same buffer
typedef int (*fp_azaDSPProcess_t)(void *dsp, azaBuffer *dst, azaBuffer *src, uint32_t flags);
// Draws all plugin controls and visualizers within the given bounds.
typedef void (*fp_azagDrawDSP)(void *dsp, azagRect bounds);
```
- azaDSPRegistry is somewhat simpler

### New struct: azaDSPChain
- Reads `azaDSPSpecs` requirements for plugins and fulfills their needs for extraneous samples when needed.
- Has storage for carrying buffer frames forward in each step of the chain, handling this requirement automatically so plugin implementations can be simpler.
	- This fulfills the extra buffer space requirement elegantly.
		- *The alternative is to require every step to be transitive (reading a src buffer that's separate from the dst buffer), which would require each step to have a full azaBuffer. That wouldn't necessarily be the worst idea, but could potentially waste lots of memory for long plugin chains. This solution does require more moving around of the data, so I may reevaluate this choice if that is found to be too costly. It may be a question of whether moving data around in cache is faster than not moving data around but missing cache, which would depend on the hardware in use.*

### Refactor of Mixer GUI
- New public GUI interface for implementing plugin GUIs in `AzAudio/gui/`
	- Allows users to create GUIs for user plugins
- azaDSP now has an `fp_draw` field for drawing its own GUI
- Factored out and encapsulated Raylib backend for GUI windows, input, and drawing.
	- Will allow swapping out Raylib in the future with minimal friction
- `mixer_gui.c` is slimmed down, as code that used to live there has better places to live now
	- `gui/types.h` has fundamental types for implementing a GUI
	- `gui/platform.h` has window management and input functions
	- `gui/gui.h` has utilities for implementing plugin GUIs, such as:
		- Theming
		- Mouse Dragging
		- Scissor Stack
		- Common Widgets, such as faders, sliders, textboxes, scrollbars, etc.
- Plugin GUI implementations have been moved into their respective files in `dsp/plugins/`
- `azaMeters` widget has been moved into `dsp/azaMeters.c`

### Splitting of dsp.h Into Many Files in dsp Folder (and dsp/plugins)
- Moved utility.h to dsp/utility.h

### Moved Several Common C-Standard-Adjacent Utilities Into aza_c_sth.h
- Deleted helpers.h (moving some part to math.h and some to aza_c_std.h)
- Deleted header_utils.h (moving everything into aza_c_std.h)

### Added
- azaVersionNote as an additional indicator as to what kind of patch is in use (can be "rel", "rc", and "dev")
- azaVersionString for the full version string in one piece

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
