/*
	File: cpuid.h
	Author: Philip Haynes
*/

#ifndef AZAUDIO_CPUID_H
#define AZAUDIO_CPUID_H

#include "aza_c_std.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct azaCPUID_t {
	bool initted;

	bool mmx;
	bool sse;
	bool sse2;
	bool sse3;
	bool ssse3;
	bool sse4_1;
	bool sse4_2;
	bool avx;
	bool fma;
	bool avx2;

	// NOTE: The following are not commonly available outside of server CPUs, so might not be worth targeting, but I'm not your dad.

	bool avx512f;
	bool avx512dq;
	bool avx512_ifma;
	bool avx512pf;
	bool avx512er;
	bool avx512cd;
	bool avx512bw;
	bool avx512vl;
	bool avx512_vbmi;
	bool avx512_vbmi2;
	bool avx512_vnni;
	bool avx512_bitalg;
	bool avx512_vpopcntdq;
	bool avx512_4vnniw;
	bool avx512_4fmaps;
	bool avx512_vp2intersect;
	bool avx512_fp16;
	bool amx_bf16;
	bool amx_tile;
	bool amx_int8;
} azaCPUID_t;
extern azaCPUID_t azaCPUID;
void azaCPUIDInit();

#ifdef __cplusplus
}
#endif

#endif // AZAUDIO_CPUID_H