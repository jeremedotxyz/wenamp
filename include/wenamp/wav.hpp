#pragma once
#include <string>
#include <vector>

namespace wenamp {

// Minimal 16-bit PCM WAV writer used by `gen-tone`, the demo generator and tests.
// `interleaved` holds channels * frames samples in [-1, 1].
bool writeWav16(const std::string& path, const std::vector<float>& interleaved, unsigned channels,
                unsigned sampleRate);

// Convenience: stereo sine tone. Left carries `hz`, right carries `hz * rightRatio`.
std::vector<float> makeTone(double hz, double seconds, unsigned sampleRate = 44100,
                            double amplitude = 0.5, double rightRatio = 1.0);

// A short chord sweep with harmonics - gives the visualizer something to chew on.
std::vector<float> makeDemoMusic(double seconds, unsigned sampleRate = 44100);

}  // namespace wenamp
