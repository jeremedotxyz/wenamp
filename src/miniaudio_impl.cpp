// Single translation unit that compiles the vendored miniaudio implementation.
// miniaudio (public domain / MIT-0) provides decoding (WAV, FLAC, MP3),
// sample-rate conversion and the cross-platform audio device backends.
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
