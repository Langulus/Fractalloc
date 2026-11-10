///                                                                           
/// Langulus::Fractalloc                                                      
/// Copyright (c) 2015 Dimo Markov <team@langulus.com>                        
/// Part of the Langulus framework, see https://langulus.com                  
///                                                                           
/// SPDX-License-Identifier: MIT                                              
///                                                                           
#pragma once
#include <Langulus/Testing.hpp>
#include <Langulus/Allocator.hpp>
#include <random>

using namespace Langulus;

std::random_device rd;
std::mt19937 gen(rd());

using Type1 = uint8_t;
using Type2 = uint16_t;

#pragma pack(push, 1)
struct Type3 {
   uint16_t m1;
   uint8_t  m2;
};
#pragma pack(pop)

using Type4 = uint32_t;
using Type8 = uint64_t;

struct TypeBig {
   Type1 t1;
   Type2 t2;
   Type4 t4;
   Type8 t8;
};

struct TypeVeryBig {
   TypeBig t1;
   TypeBig t2;
   TypeBig t4;
   TypeBig t8[5];
};

struct alignas(128) TypeVeryBigAligned {
   using CTTI_MinAlloc = Yes<512>;
   using CTTI_Pooled   = PooledByType<2048>;

   TypeBig t1;
   TypeBig t2;
   TypeBig t4;
   TypeBig t8[5];
};

#pragma pack(push, 1)
struct TypeVeryBigPacked {
   TypeBig t1[11];
   TypeBig t2;
   TypeBig t4;
   TypeBig t8[5];
};
#pragma pack(pop)

/// Check if a pointer is aligned to a desired alignment                      
bool IsAligned(const void* a, size_t alignment = Alignment) noexcept {
   return 0 == (reinterpret_cast<uintptr_t>(a) & uintptr_t {alignment - 1});
}
