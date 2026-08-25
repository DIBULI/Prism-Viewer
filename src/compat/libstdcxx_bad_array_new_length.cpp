#include <new>

// Compatibility shim for static libraries compiled with GCC 11 or newer when
// the final executable is linked by GCC 9/10 (for example on Ubuntu 20.04).
// Keep this source restricted by CMake to that exact configuration.
namespace std {

[[noreturn]] void __throw_bad_array_new_length() {
  throw bad_array_new_length();
}

}  // namespace std
