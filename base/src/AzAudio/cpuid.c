/*
	File: cpuid.c
	Author: Philip Haynes
*/

#include "cpuid.h"

#include <stdint.h>
#include <string.h>

#if defined(_MSC_VER)
	#include <intrin.h>
#endif

azaCPUID_t azaCPUID = {0};

typedef union azaCPUIDLeaf {
	uint32_t data[4];
	struct {
		uint32_t eax, ebx, ecx, edx;
	};
} azaCPUIDLeaf;

static inline bool azaIsBitSet(uint32_t num, uint32_t bit) {
	return (num & (1 << bit)) != 0;
}

#if defined(_MSC_VER)

static azaCPUIDLeaf azaGetCPUIDLeaf(uint32_t leaf, uint32_t subleaf) {
	azaCPUIDLeaf result;
	__cpuidex((int*)result.data, leaf, subleaf);
	return result;
}

#elif defined(__GNUC__) || defined(__clang__)

#include <cpuid.h>

static azaCPUIDLeaf azaGetCPUIDLeaf(uint32_t leaf, uint32_t subleaf) {
	azaCPUIDLeaf result;
	__get_cpuid_count(leaf, subleaf, &result.eax, &result.ebx, &result.ecx, &result.edx);
	return result;
}

#else

#warning "platform not supported"

#endif

void azaCPUIDInit() {
	azaCPUIDLeaf leaf;

	// Leaf 1
	leaf = azaGetCPUIDLeaf(1, 0);

	azaCPUID.mmx                 = azaIsBitSet(leaf.edx, 23);
	azaCPUID.sse                 = azaIsBitSet(leaf.edx, 25);
	azaCPUID.sse2                = azaIsBitSet(leaf.edx, 26);
	azaCPUID.sse3                = azaIsBitSet(leaf.ecx, 0);
	azaCPUID.ssse3               = azaIsBitSet(leaf.ecx, 9);
	azaCPUID.sse4_1              = azaIsBitSet(leaf.ecx, 19);
	azaCPUID.sse4_2              = azaIsBitSet(leaf.ecx, 20);
	azaCPUID.avx                 = azaIsBitSet(leaf.ecx, 28);
	azaCPUID.fma                 = azaIsBitSet(leaf.ecx, 12);

	// Leaf 7
	leaf = azaGetCPUIDLeaf(7, 0);

	azaCPUID.avx2                = azaIsBitSet(leaf.ebx, 5);
	azaCPUID.avx512f             = azaIsBitSet(leaf.ebx, 16);
	azaCPUID.avx512dq            = azaIsBitSet(leaf.ebx, 17);
	azaCPUID.avx512_ifma         = azaIsBitSet(leaf.ebx, 21);
	azaCPUID.avx512pf            = azaIsBitSet(leaf.ebx, 26);
	azaCPUID.avx512er            = azaIsBitSet(leaf.ebx, 27);
	azaCPUID.avx512cd            = azaIsBitSet(leaf.ebx, 28);
	azaCPUID.avx512bw            = azaIsBitSet(leaf.ebx, 30);
	azaCPUID.avx512vl            = azaIsBitSet(leaf.ebx, 31);
	azaCPUID.avx512_vbmi         = azaIsBitSet(leaf.ecx, 1);
	azaCPUID.avx512_vbmi2        = azaIsBitSet(leaf.ecx, 6);
	azaCPUID.avx512_vnni         = azaIsBitSet(leaf.ecx, 11);
	azaCPUID.avx512_bitalg       = azaIsBitSet(leaf.ecx, 12);
	azaCPUID.avx512_vpopcntdq    = azaIsBitSet(leaf.ecx, 14);
	azaCPUID.avx512_4vnniw       = azaIsBitSet(leaf.edx, 2);
	azaCPUID.avx512_4fmaps       = azaIsBitSet(leaf.edx, 3);
	azaCPUID.avx512_vp2intersect = azaIsBitSet(leaf.edx, 8);
	azaCPUID.avx512_fp16         = azaIsBitSet(leaf.edx, 23);
	azaCPUID.amx_bf16            = azaIsBitSet(leaf.edx, 22);
	azaCPUID.amx_tile            = azaIsBitSet(leaf.edx, 24);
	azaCPUID.amx_int8            = azaIsBitSet(leaf.edx, 25);

	azaCPUID.initted = true;
}