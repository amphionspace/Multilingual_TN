#ifndef TTS_NORMALIZER_CAPI_H
#define TTS_NORMALIZER_CAPI_H

/* Public C API of libtts_normalizer.
 *
 * Plain C (extern "C") on purpose: it is the only exported surface of the shared
 * library, which (a) lets the linker strip every unused ICU symbol, and (b) avoids
 * C++ ABI mismatches across compilers/STLs.
 *
 * Threading: a handle is NOT thread-safe. Use one handle per thread
 * (creation is cheap relative to a line of work; the shared read-only rule data
 * lives inside each handle).
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tts_normalizer tts_normalizer_t;

/* Create a normalizer from a resource directory, e.g. "data/ru" or "data/en".
 * The directory must contain config.json. Returns NULL on failure; call
 * tts_last_error() for the reason. */
tts_normalizer_t* tts_init(const char* resource_dir);

/* Normalize one UTF-8 line. The returned pointer is owned by the library and stays
 * valid until the next tts_normalize() call on the same thread. Returns NULL if
 * handle or line is NULL. */
const char* tts_normalize(tts_normalizer_t* handle, const char* line_utf8);

/* Last error on this thread (e.g. after tts_init returned NULL), or NULL if none. */
const char* tts_last_error(void);

/* Release a handle created by tts_init(). Passing NULL is a no-op. */
void tts_free(tts_normalizer_t* handle);

#ifdef __cplusplus
}
#endif

#endif /* TTS_NORMALIZER_CAPI_H */
