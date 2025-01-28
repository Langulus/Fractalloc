///                                                                           
/// Langulus::Fractalloc                                                      
/// Copyright (c) 2015 Dimo Markov <team@langulus.com>                        
/// Part of the Langulus framework, see https://langulus.com                  
///                                                                           
/// SPDX-License-Identifier: MIT                                              
///                                                                           
#pragma once
#include "Config.hpp"
#include <Langulus/RTTI/Meta.hpp>


namespace Langulus::Fractalloc
{

   class Pool;
   
   template<class T>
   concept AllocationPrimitive = requires(T a) { 
      {T::GetNewAllocationSize(0)} -> CT::Unsigned;
   };


   ///                                                                        
   ///   Memory allocation                                                    
   ///                                                                        
   /// This is a single allocation record                                     
   ///                                                                        
   struct Allocation final {
      friend class Pool;
      friend struct Allocator;
   protected:
      // Allocated bytes for this chunk                                 
      Offset mAllocatedBytes;
      // The number of references to this memory                        
      Count mReferences;
      union {
         // This pointer has two uses, depending on mReferences         
         // If mReferences > 0, it refers to the pool that owns the     
         //    allocation, or	handle for std::free() if MANAGED_MEMORY  
         //    feature is not enabled                                   
         // If mReferences == 0, it refers to the next free entry to be 
         //    reused                                                   
         Pool* mPool;
         Allocation* mNextFreeEntry;
      };

   #if LANGULUS_FEATURE(MEMORY_STATISTICS)
      // Acts like a timestamp of when the allocation happened          
      Count mStep;
   #endif

   public:
      Allocation() = delete;
      Allocation(const Allocation&) = delete;
      Allocation(Allocation&&) = delete;
      ~Allocation() = delete;

      constexpr Allocation(Offset, Pool*) noexcept;

      static constexpr Offset GetSize() noexcept;
      static constexpr Offset GetNewAllocationSize(Offset) noexcept;
      static constexpr Offset GetMinAllocation() noexcept;

      constexpr Count GetUses() const noexcept;
      constexpr Offset GetTotalSize() const noexcept;
      constexpr Offset GetAllocatedSize() const noexcept;
      auto GetBlockStart() const noexcept -> Byte*;
      auto GetBlockEnd() const noexcept -> Byte const*;
      bool Contains(const void*) const noexcept;
      bool CollisionFree(const Allocation&) const noexcept;

      template<class T>
      T* As() const noexcept;

      constexpr void Keep() noexcept;
      constexpr void Keep(Count) noexcept;
      constexpr void Free() noexcept;
      constexpr void Free(Count) noexcept;
   };

} // namespace Langulus::Fractalloc
