// An allocator for std::vector that puts the elements in PSRAM (8 MB on
// this board) instead of the scarce internal heap. Off the board (the host
// tests) it is plain malloc.
#pragma once
#include <stdlib.h>
#include <new>
#include <vector>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

inline void *psMalloc(size_t n) {
#ifdef ESP_PLATFORM
  void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : malloc(n);  // no PSRAM: the internal heap, rather than nothing
#else
  return malloc(n);
#endif
}

template <class T>
struct PsAlloc {
  typedef T value_type;
  PsAlloc() noexcept {}
  template <class U>
  PsAlloc(const PsAlloc<U> &) noexcept {}
  T *allocate(size_t n) {
    void *p = psMalloc(n * sizeof(T));
    if (!p) abort();  // out of every heap: as the core's own new does without exceptions
    return (T *)p;
  }
  void deallocate(T *p, size_t) noexcept { free(p); }
};
template <class T, class U>
bool operator==(const PsAlloc<T> &, const PsAlloc<U> &) { return true; }
template <class T, class U>
bool operator!=(const PsAlloc<T> &, const PsAlloc<U> &) { return false; }

template <class T>
using PsVec = std::vector<T, PsAlloc<T>>;
