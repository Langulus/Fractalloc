///                                                                           
/// Langulus::Fractalloc                                                      
/// Copyright (c) 2015 Dimo Markov <team@langulus.com>                        
/// Part of the Langulus framework, see https://langulus.com                  
///                                                                           
/// SPDX-License-Identifier: MIT                                              
///                                                                           
#pragma once
#include <Langulus/Core.hpp>
#include <Langulus/MetaOf.hpp>
#include "Allocation.hpp"

#if not LANGULUS_FEATURE(MANAGED_MEMORY)
   #error "This file shouldn't be included if MANAGED_MEMORY is disabled"
#endif

#if LANGULUS_FEATURE(MEMORY_STATISTICS)
   #include "Statistics.hpp"
#endif


namespace Langulus::Fractalloc
{
   using RTTI::DMeta;

   #if not LANGULUS_FEATURE(MEMORY_STATISTICS)
      struct State {
         consteval bool Assert() const noexcept { return true; }
      };
   #endif

   ///                                                                        
   ///   Memory allocator                                                     
   ///                                                                        
   /// Basically an overcomplicated wrapper for malloc/free. Manages pools.   
   struct Allocator {
      Allocator() = delete;
      
      LANGULUS_API(FRACTALLOC)
      static auto Allocate(DMeta, pot_t) assumptious -> Allocation*;
      
      LANGULUS_API(FRACTALLOC)
      static auto Reallocate(DMeta, pot_t, Allocation*) assumptious -> Allocation*;

      LANGULUS_API(FRACTALLOC)
      static void Deallocate(Allocation*) assumptious;

      LANGULUS_API(FRACTALLOC)
      static auto Find(const void*) assumptious -> Allocation const*;

      LANGULUS_API(FRACTALLOC)
      static bool CheckAuthority(const void*) assumptious;

      LANGULUS_API(FRACTALLOC)
      static auto AllocatePool(DMeta, pot_t) assumptious -> Pool*;

      LANGULUS_API(FRACTALLOC)
      static void DeallocatePool(Pool*) assumptious;

      LANGULUS_API(FRACTALLOC)
      static bool CollectGarbage();

      #if LANGULUS_FEATURE(MANAGED_REFLECTION)
         LANGULUS_API(FRACTALLOC)
         static size_t CheckBoundary(const Token&) noexcept;
      #endif

      #if LANGULUS_FEATURE(MEMORY_STATISTICS)
         LANGULUS_API(FRACTALLOC)
         static auto GetStatistics() noexcept -> const Statistics&;

         LANGULUS_API(FRACTALLOC)
         static void DumpPools() noexcept;

         LANGULUS_API(FRACTALLOC)
         static void Diff(const Statistics&) noexcept;
         
         LANGULUS_API(FRACTALLOC)
         static bool IntegrityCheck();
      #endif

      
      ///                                                                     
      /// Packed pointer support                                              
      template<CT::CustomPointer T>
      static auto Find(T ptr) assumptious -> Allocation const* {
         return GetAllocation(
            MetaDataOf<Deptr<T>>(), ptr.GetPoolId(), ptr.GetEntryId()
         );
      }

      template<CT::CustomPointer T>
      static auto AllocatePacked(DMeta type, pot_t size)
      assumptious -> Allocation* {
         return AllocatePackedInner(T::Specification, type, size);
      }

      template<CT::CustomPointer T>
      static auto ReallocatePacked(DMeta type, pot_t size, T* prev)
      assumptious -> Allocation* {
         return ReallocatePackedInner(T::Specification,
            type, size, reinterpret_cast<Allocation*>(prev)
         );
      }
      
      LANGULUS_API(FRACTALLOC)
      static auto GetAllocation(
         DMeta meta, size_t poolId, size_t entryId
      ) assumptious -> Allocation*;
      
      LANGULUS_API(FRACTALLOC)
      static auto AllocatePackedInner(
         PointerSpecification const&,
         DMeta, pot_t
      ) assumptious -> Allocation*;
      
      LANGULUS_API(FRACTALLOC)
      static auto ReallocatePackedInner(
         PointerSpecification const&,
         DMeta, pot_t, Allocation*
      ) assumptious -> Allocation*;

      LANGULUS_API(FRACTALLOC)
      static void* UnpackPointer(
         PointerSpecification const&,
         DMeta deptr_type, uintptr_t packed
      ) assumptious;

      LANGULUS_API(FRACTALLOC)
      static auto FindPackedPointer(
         PointerSpecification const&,
         DMeta deptr_type, uintptr_t packed
      ) assumptious -> Allocation const*;
   };   
}
