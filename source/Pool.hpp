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
      const Size mAllocatedByBackend {};
      const Offset mAllocatedByBackendLog2 {};
      const Offset mAllocatedByBackendLSB {};

      // Bytes allocated by the frontend                                
      Size mAllocatedByFrontend {};
      // Number of entries that have been used overall                  
      Count mEntries {};
      // A chain of freed entries in the range [0-mEntries)             
      Allocation* mLastFreed {};
      // The next usable entry (not allocated yet)                      
      Byte* mNextEntry {};
      // Current threshold, that is, max size of a new entry            
      Size mThreshold {};
      Size mThresholdPrevious {};
      // Smallest allocation possible for the pool                      
      Size mThresholdMin {};
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

   public:
      Pool() = delete;
      Pool(const Pool&) = delete;
      Pool(Pool&&) = delete;
      ~Pool() = delete;

      Pool(DMeta, const Size&, void*) noexcept;

      // Default pool allocation is 1 MB                                
      static constexpr Size DefaultPoolSize = 1024 * 1024;
      static constexpr Offset InvalidIndex = ::std::numeric_limits<Offset>::max();

   public:
      NOD() static constexpr Size GetSize() noexcept;
      NOD() static constexpr Size GetNewAllocationSize(const Size&) noexcept;

      template<class T = Allocation>
      NOD() T* GetPoolStart() noexcept;
      template<class T = Allocation>
      NOD() const T* GetPoolStart() const noexcept;

      NOD() constexpr Size GetMinAllocation() const noexcept;
      NOD() constexpr Size GetTotalSize() const noexcept;
      NOD() constexpr Count GetMaxEntries() const noexcept;
      NOD() constexpr Size GetAllocatedByBackend() const noexcept;
      NOD() constexpr Size GetAllocatedByFrontend() const noexcept;
      NOD() constexpr bool IsInUse() const noexcept;
      NOD() constexpr bool CanContain(const Size&) const noexcept;
      NOD() bool Contains(const void*) const noexcept;
      NOD() const Allocation* Find(const void*) const IF_UNSAFE(noexcept);

      NOD() Allocation* Allocate(Size) IF_UNSAFE(noexcept);
      NOD() bool Reallocate(Allocation*, Size) IF_UNSAFE(noexcept);
      void Deallocate(Allocation*) IF_UNSAFE(noexcept);
      void FreePoolChain();
      void Null();
      void Touch();

      NOD() Size ThresholdFromIndex(const Offset&) const noexcept;
      NOD() const Allocation* AllocationFromIndex(const Offset&) const noexcept;
      NOD() Offset IndexFromAddress(const void*) const IF_UNSAFE(noexcept);
      NOD() Offset ValidateIndex(Offset) const noexcept;
      NOD() Offset UpIndex(Offset) const noexcept;
      NOD() const Allocation* AllocationFromAddress(const void*) const IF_UNSAFE(noexcept);
   };

} // namespace Langulus::Fractalloc

#include "Pool.inl"