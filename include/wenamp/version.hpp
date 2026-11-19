#pragma once

namespace wenamp {

constexpr const char* kVersion = "0.1.0";

// Engine-wide fixed output format. The decoder resamples everything to this,
// so the DSP chain, analyzer and device all see one stable stream.
constexpr unsigned kSampleRate = 44100;
constexpr unsigned kChannels = 2;

}  // namespace wenamp
