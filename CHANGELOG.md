# Changelog

## Philosophy

This project follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html), and this document will describe notable changes between versions.
Note that not all changes will be documented here, mainly just the ones that change API functionality.
The goal is that this changelog shouldn't be a big burden on either the developers of AzAudio, nor those that use it.
To this end, the changes for each version will be organized based on importance, with API-breaking changes coming first.
As a bonus, we may try to document porting steps for users of this library that want to upgrade (we'll see how that goes).

## [Unreleased]

### Changed
- azaSampler interface
	- azaSamplerConfig is almost completely different
	- Uses ADSR envelopes
	- Can now have multiple instances of sounds with independent pitch and gain
	- Pitch and gain follows targets in a predictable, linear fashion
- azaFilter interface
	- Has up to 16 poles
	- azaFilterConfig has a `poles` field after `kind` and before `frequency`

### Added
- ADSR envelopes
- azaFollowerLinear for linearly following target values at a given rate
- in AzAudio/math.h
	- azaWindow functions (Hann, Blackman, Blackman-Harris, Nuttall)
	- Additional windowed sinc functions

### Fixed
- azaFFT sinusoid integration error causing erroneous readings in azaMonitorSpectrum

## [v0.2.1](https://github.com/Equivocatorrr/AzAudio/releases/tag/v0.2.1)

### Added
- Complex FFT
- Spectrum Monitor plugin

## [v0.2.0](https://github.com/Equivocatorrr/AzAudio/releases/tag/v0.2.0)

### Added
- Changelog