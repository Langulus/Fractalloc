///                                                                           
/// Langulus::Fractalloc                                                      
/// Copyright (c) 2015 Dimo Markov <team@langulus.com>                        
/// Part of the Langulus framework, see https://langulus.com                  
///                                                                           
/// SPDX-License-Identifier: MIT                                              
///                                                                           
#pragma once
#include <Langulus/Fractalloc/Allocator.hpp>


namespace Langulus::Fractalloc
{

   using RTTI::DMeta;

   ///                                                                        
   ///   Memory pool                                                          
   ///                                                                        
   class Pool final {
   friend struct Allocator;
   protected:
      // Bytes allocated by the backend                                 
      const Offset mAllocatedByBackend {};
      const Offset mAllocatedByBackendLog2 {};
      const Offset mAllocatedByBackendLSB {};

      // Bytes allocated by the frontend                                
      Offset mAllocatedByFrontend {};
      // Number of entries that have been used overall                  
      Count mEntries {};
      // A chain of freed entries in the range [0-mEntries)             
      Allocation* mLastFreed {};
      // Current threshold, that is, max size of a new entry            
      Offset mThreshold {};
      Offset mThresholdPrevious {};
      // Smallest allocation possible for the pool                      
      Offset mThresholdMin {};
      // Pointer to start of usable memory                              
      Byte* mMemory {};
      Byte* mMemoryEnd {};
      // Associated meta data, when types are reflected with nondefault 
      // PoolTactic                                                     
      DMeta mMeta {};
      // Handle for the pool allocation, for use with ::std::free       
      void* mHandle {};

      // Next pool in the pool chain                                    
      Pool* mNext {};

   #if LANGULUS_FEATURE(MEMORY_STATISTICS)
      // Acts like a timestamp of when the allocation happened          
      Count mStep;
      Count mValidEntries {};
   #endif

   public:
      Pool() = delete;
      Pool(const Pool&) = delete;
      Pool(Pool&&) = delete;
      ~Pool() = delete;

      Pool(DMeta, Offset, void*) noexcept;

      // Default pool allocation is 1 MB                                
      static constexpr Offset DefaultPoolSize = 1024 * 1024;
      static constexpr Offset InvalidIndex = ::std::numeric_limits<Offset>::max();

   public:
      static constexpr Offset GetSize() noexcept;
      static constexpr Offset GetNewAllocationSize(Offset) noexcept;

      template<class T = Allocation>
      auto GetPoolStart() noexcept -> T*;
      template<class T = Allocation>
      auto GetPoolStart() const noexcept -> T const*;

      constexpr Offset GetMinAllocation() const noexcept;
      constexpr Offset GetTotalSize() const noexcept;
      constexpr Count  GetMaxEntries() const noexcept;
      constexpr Offset GetAllocatedByBackend() const noexcept;
      constexpr Offset GetAllocatedByFrontend() const noexcept;
      constexpr bool IsInUse() const noexcept;
      constexpr bool CanContain(Offset) const noexcept;
      bool Contains(const void*) const noexcept;
      auto Find(const void*) const IF_UNSAFE(noexcept) -> const Allocation*;

      auto Allocate(Offset) IF_UNSAFE(noexcept) -> Allocation*;
      bool Reallocate(Allocation*, Offset) IF_UNSAFE(noexcept);
      void Deallocate(Allocation*) IF_UNSAFE(noexcept);
      void FreePoolChain();
      void Null();
      void Touch();
      void Trim();

      Offset ThresholdFromIndex(Offset) const noexcept;
      Offset IndexFromAddress(const void*) const IF_UNSAFE(noexcept);
      Offset ValidateIndex(Offset) const noexcept;
      Offset UpIndex(Offset) const noexcept;
      auto   AllocationFromIndex(Offset) const noexcept -> const Allocation*;
      auto   AllocationFromAddress(const void*) const IF_UNSAFE(noexcept) -> const Allocation*;
   };

} // namespace Langulus::Fractalloc
