///                                                                           
/// Langulus::Fractalloc                                                      
/// Copyright (c) 2015 Dimo Markov <team@langulus.com>                        
/// Part of the Langulus framework, see https://langulus.com                  
///                                                                           
/// SPDX-License-Identifier: MIT                                              
///                                                                           
#pragma once
#include <Langulus/Core.hpp>
#include <Langulus/Utils/Pot.hpp>

#if not LANGULUS_FEATURE(MANAGED_MEMORY)
   #error "This file shouldn't be included if MANAGED_MEMORY is disabled"
#endif

#if defined(LANGULUS_EXPORT_ALL) or defined(LANGULUS_EXPORT_FRACTALLOC)
   #define LANGULUS_API_FRACTALLOC() LANGULUS_EXPORT()
#else
   #define LANGULUS_API_FRACTALLOC() LANGULUS_IMPORT()
#endif


namespace Langulus::Fractalloc
{
   struct Allocation;
   struct PoolBank;
   
   ///                                                                        
   ///   Memory pool                                                          
   ///                                                                        
   /// Manages allocations. Aligned to a dynamically determined cache size,   
   /// with the following structure:                                          
   /// [pool data][padding][allocation data...][padding][client data...]      
   struct Pool {
   protected:
      friend struct Allocator;
      friend struct PoolBank;
      
      // A chain of freed entries in the range [0; mEntries)            
      Allocation* mLastFreed = nullptr;
      // Allocation table                                               
      Allocation* mAllocationData;
      // The size distribution of entries                               
      //    @attention this is indexed by log2(entry->mSize)            
      size_t mDistribution[sizeof(size_t) * 8] = {};
      // Pointer to start of client data                                
      uint8_t* const mClientData;
      // Set by meta.GetAlignment()                                     
      pot_t mDataAlignment;
      // Set by meta.GetMinAlloc()                                      
      pot_t mDataMinAlloc;
      // Id of pool inside a type chain (used for packing pointers)     
      unsigned mID = 0;
      // Next pool in the pool chain                                    
      Pool* mNext = nullptr;

      // Bytes allocated by the frontend                                
      size_t mAllocatedByFrontend = 0;
      // An index that guarantees a new unused entry                    
      size_t mNextEntry = 0;
      // Keeps track of how many entries are currently in use           
      size_t mValidEntries = 0;

      #if LANGULUS_FEATURE(MEMORY_STATISTICS)
         // Acts like a timestamp of when the allocation happened       
         size_t mStep;
      #endif

      // Bytes allocated by the backend (aka the reserved client bytes) 
      const pot_t mAllocatedByBackend;
      // Alignment used when allocating entries                         
      const pot_t mAlign;
      // Alignment used when allocating entries                         
      const pot_t mPoolAlignment;
      // Smallest allocation possible for the pool                      
      const pot_t mThresholdMin;

      // Current threshold, that is, max allowed size of a new entry    
      pot_t mThresholdMax;
      // Currently the biggest allocation present in the pool           
      //    @attention this is provided by entry->mSize                 
      pot_t mBiggestEntry;
      // The biggest possible amount of entries                         
      const pot_t mMaxEntries;

   IF_LANGULUS_TESTING(public:)
      Pool() = delete;
      Pool(const Pool&) = delete;
      Pool(Pool&&) = delete;

      Pool(
         pot_t data_alignment,
         pot_t data_min_alloc,
         pot_t pool_alignment,
         pot_t client_size
      ) assumptious;

      static size_t Cost(pot_t dataAlignment, pot_t dataMinAlloc, pot_t) noexcept;

      /// Get the pool ID                                                     
      auto GetID() const noexcept {
         return mID;
      }

      /// Get the start of the allocation data                                
      auto GetAllocationData() const noexcept -> Allocation* {
         return mAllocationData;
      }

      /// Get the minimum allocation for an entry inside this pool            
      ///   @return the size in bytes, always a power-of-two                  
      auto GetMinAllocation() const noexcept -> pot_t {
         return mThresholdMin;
      }
      
      /// Get the start of the usable memory for the pool                     
      auto GetClientData() const noexcept -> uint8_t* {
         return mClientData;
      }

      /// Get the total size of the pool, including this instance and padding 
      ///   @return the size in bytes                                         
      auto GetTotalSize() const noexcept -> size_t {
         return mAllocatedByBackend + Cost(mDataAlignment, mDataMinAlloc, mAllocatedByBackend);
      }

      /// Get the max number of possible entries                              
      /// (if all of them are as small as possible)                           
      ///   @return the size in bytes, always a power-of-two                  
      auto GetMaxEntries() const noexcept -> pot_t {
         return mAllocatedByBackend / mThresholdMin;
      }

      auto GetCurrentEntries() const noexcept -> size_t {
         return mNextEntry;
      }

      auto GetValidEntries() const noexcept -> size_t {
         return mValidEntries;
      }
      
      auto GetLastFreedEntry() const noexcept -> Allocation* {
         return mLastFreed;
      }

      /// Get the bytes reserved for the bool                                 
      ///   @return bytes allocated for the pool                              
      auto GetAllocatedByBackend() const noexcept -> pot_t {
         return mAllocatedByBackend;
      }

      /// Get the used number of bytes - the sum of all allocations           
      ///   @return bytes allocated by the client                             
      auto GetAllocatedByFrontend() const noexcept -> size_t {
         return mAllocatedByFrontend;
      }
      
      /// Check if there is any used memory                                   
      ///   @return true on at least one valid entry                          
      bool IsInUse() const noexcept {
         return mAllocatedByFrontend > 0;
      }

      /// An entry larger than the next allowed mThresholdMax will clog       
      /// the pool, until it is freed. This means that no new entries are     
      /// allowed. Reallocations are allowed, as long as they don't exceed    
      /// the biggest entry size. A pool may unclog after trimming.           
      bool IsClogged() const noexcept {
         return mBiggestEntry > mThresholdMax;
      }

      /// Check if memory can contain a number of bytes                       
      ///   @param bytes number of bytes to check                             
      ///   @return true if bytes can be contained in a new/recycled element  
      bool CanContain(pot_t bytes) const noexcept {
         return bytes <= mThresholdMax
            and (mAllocatedByFrontend + static_cast<size_t>(bytes) <= mAllocatedByBackend);
      }

      LANGULUS_API(FRACTALLOC)
      bool ContainsData(const void*) const noexcept;
      LANGULUS_API(FRACTALLOC)
      bool ContainsAllocation(const Allocation*) const noexcept;
      auto Find(const void*) const assumptious -> const Allocation*;

      LANGULUS_API(FRACTALLOC)
      auto Allocate(pot_t) assumptious -> Allocation*;
      auto AllocatePacked(size_t entry_budget, pot_t) assumptious -> Allocation*;
      bool Reallocate(Allocation*, pot_t) assumptious;
      LANGULUS_API(FRACTALLOC)
      void Deallocate(Allocation*) assumptious;
      
      LANGULUS_API(FRACTALLOC)
      auto ThresholdFromIndex(size_t) const noexcept -> pot_t;
      LANGULUS_API(FRACTALLOC)
      auto IndexFromAddress(const void*) const assumptious -> size_t;
      LANGULUS_API(FRACTALLOC)
      auto IndexFromAllocation(const Allocation*) const assumptious -> size_t;
      auto UpIndex(size_t) const noexcept -> size_t;
      LANGULUS_API(FRACTALLOC)
      auto AllocationFromIndex(size_t) const noexcept -> Allocation*;
      auto AllocationFromAddress(const void*) const assumptious -> Allocation*;

      void FreePoolChain();
      void Null();
      void Touch();
      void Trim();
   };
   
   /// Fast log2                                                              
   /// https://stackoverflow.com/questions/11376288                           
   LANGULUS(ALWAYS_INLINED)
   constexpr size_t FastLog2(const size_t x) noexcept {
      if (x < 2)
         return 0;
      return size_t {8 * sizeof(size_t)} - ::std::countl_zero(x) - size_t {1};
   }
}
