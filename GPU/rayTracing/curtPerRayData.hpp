#pragma once

#include <optix.h>

#include <curtRNGState.hpp>
#include <curtUtilities.hpp>

#include <utGDT.hpp>

template <typename T> struct PerRayData {
  T rayWeight = 1.;
  gdt::vec3f pos;
  gdt::vec3f dir;

  curtRNGState RNGstate;

  T energy;
};

// this can only get compiled if included in a cuda kernel
#ifdef __CUDACC__
static __forceinline__ __device__ void *unpackPointer(uint32_t i0,
                                                      uint32_t i1) {
  const uint64_t uptr = static_cast<uint64_t>(i0) << 32 | i1;
  void *ptr = reinterpret_cast<void *>(uptr);
  return ptr;
}

static __forceinline__ __device__ void packPointer(void *ptr, uint32_t &i0,
                                                   uint32_t &i1) {
  const uint64_t uptr = reinterpret_cast<uint64_t>(ptr);
  i0 = uptr >> 32;
  i1 = uptr & 0x00000000ffffffff;
}

template <typename T> static __forceinline__ __device__ T *getPRD() {
  const uint32_t u0 = optixGetPayload_0();
  const uint32_t u1 = optixGetPayload_1();
  return reinterpret_cast<T *>(unpackPointer(u0, u1));
}
#endif