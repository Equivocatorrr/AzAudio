#ifndef AZAUDIO_HEADER_UTILS
#define AZAUDIO_HEADER_UTILS

#ifdef __cplusplus
#define AZA_CLITERAL(s) s
#else
#define AZA_CLITERAL(s) (s)
#endif

#if defined(__clang__)
	#define AZA_FORCE_INLINE(...) inline __VA_ARGS__
#elif defined(__GNUG__) || defined(__GNUC__)
	#define AZA_FORCE_INLINE(...) inline __VA_ARGS__ __attribute__((always_inline))
#elif defined(_MSC_VER)
	#define AZA_FORCE_INLINE(...) __forceinline __VA_ARGS__
#else
	#define AZA_FORCE_INLINE(...) inline __VA_ARGS__
#endif

#endif // AZAUDIO_HEADER_UTILS