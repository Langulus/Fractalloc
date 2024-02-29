///                                                                           
/// Langulus::Fractalloc                                                      
/// Copyright (c) 2015 Dimo Markov <team@langulus.com>                        
/// Part of the Langulus framework, see https://langulus.com                  
///                                                                           
/// Distributed under GNU General Public License v3+                          
/// See LICENSE file, or https://www.gnu.org/licenses                         
///                                                                           
#pragma once
#include <Fractalloc/Allocator.hpp>


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
      // The next usable entry (not allocated yet)                      
      Byte* mNextEntry {};
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
      NOD() static constexpr Offset GetSize() noexcept;
      NOD() static constexpr Offset GetNewAllocationSize(Offset) noexcept;

      template<class T = Allocation>
      NOD() T* GetPoolStart() noexcept;
      template<class T = Allocation>
      NOD() T const* GetPoolStart() const noexcept;

      NOD() constexpr Offset GetMinAllocation() const noexcept;
      NOD() constexpr Offset GetTotalSize() const noexcept;
      NOD() constexpr Count  GetMaxEntries() const noexcept;
      NOD() constexpr Offset GetAllocatedByBackend() const noexcept;
      NOD() constexpr Offset GetAllocatedByFrontend() const noexcept;
      NOD() constexpr bool IsInUse() const noexcept;
      NOD() constexpr bool CanContain(Offset) const noexcept;
      NOD() bool Contains(const void*) const noexcept;
      NOD() const Allocation* Find(const void*) const IF_UNSAFE(noexcept);

      NOD() Allocation* Allocate(Offset) IF_UNSAFE(noexcept);
      NOD() bool Reallocate(Allocation*, Offset) IF_UNSAFE(noexcept);
      void Deallocate(Allocation*) IF_UNSAFE(noexcept);
      void FreePoolChain();
      void Null();
      void Touch();
      void Trim();

      NOD() Offset ThresholdFromIndex(Offset) const noexcept;
      NOD() const Allocation* AllocationFromIndex(Offset) const noexcept;
      NOD() Offset IndexFromAddress(const void*) const IF_UNSAFE(noexcept);
      NOD() Offset ValidateIndex(Offset) const noexcept;
      NOD() Offset UpIndex(Offset) const noexcept;
      NOD() const Allocation* AllocationFromAddress(const void*) const IF_UNSAFE(noexcept);
   };

} // namespace Langulus::Fractalloc
