///                                                                           
/// Langulus::Fractalloc                                                      
/// Copyright(C) 2015 Dimo Markov <langulusteam@gmail.com>                    
///                                                                           
/// Distributed under GNU General Public License v3+                          
/// See LICENSE file, or https://www.gnu.org/licenses                         
///                                                                           
#pragma once
#include <Fractalloc/Allocator.hpp>

using namespace Langulus;
using namespace Langulus::Fractalloc;

//#define LANGULUS_STD_BENCHMARK

//#ifdef LANGULUS_STD_BENCHMARK
#define CATCH_CONFIG_ENABLE_BENCHMARKING
//#endif

inline Byte* asbytes(void* a) noexcept {
	return reinterpret_cast<Byte*>(a);
}

inline const Byte* asbytes(const void* a) noexcept {
	return reinterpret_cast<const Byte*>(a);
}
