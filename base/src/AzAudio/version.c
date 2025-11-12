/*
	File: version.c
	Author: Philip Haynes
	Separating the actual version values out into a separate file since we plan to update these regularly.
*/

#define AZA_VERSION_MAJOR 0
#define AZA_VERSION_MINOR 4
#define AZA_VERSION_PATCH 0
/*
	A little note to explain what kind of patch the current build is on, put at the end of the full version string.
	Possible values are:
		- "rel", indicating a proper release
		- "rc", indicating a release candidate
		- "dev", indicating an incomplete development build
*/
#define AZA_VERSION_NOTE "dev"
#define STRINGIFY(a) STRINGIFY_FOR_REAL_THIS_TIME(a)
#define STRINGIFY_FOR_REAL_THIS_TIME(a) #a

const unsigned short azaVersionMajor = AZA_VERSION_MAJOR;
const unsigned short azaVersionMinor = AZA_VERSION_MINOR;
const unsigned short azaVersionPatch = AZA_VERSION_PATCH;
const char *azaVersionNote = AZA_VERSION_NOTE;
const char *azaVersionString = "" STRINGIFY(AZA_VERSION_MAJOR) "." STRINGIFY(AZA_VERSION_MINOR) "." STRINGIFY(AZA_VERSION_PATCH) "-" AZA_VERSION_NOTE;
