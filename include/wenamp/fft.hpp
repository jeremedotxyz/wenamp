#pragma once
#include <complex>
#include <cstddef>
#include <vector>

namespace wenamp {

// In-place iterative radix-2 Cooley-Tukey FFT with precomputed twiddles.
class FFT {
 public:
  explicit FFT(std::size_t n);  // n must be a power of two (>= 2)

  std::size_t size() const { return n_; }
  void forward(std::vector<std::complex<float>>& data) const;

  static bool isPowerOfTwo(std::size_t n) { return n >= 2 && (n & (n - 1)) == 0; }

 private:
  std::size_t n_;
  std::vector<std::size_t> rev_;
  std::vector<std::complex<float>> tw_;
};

}  // namespace wenamp
