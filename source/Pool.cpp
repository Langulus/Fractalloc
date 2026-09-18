///                                                                           
/// Langulus::Fractalloc                                                      
/// Copyright (c) 2015 Dimo Markov <team@langulus.com>                        
/// Part of the Langulus framework, see https://langulus.com                  
///                                                                           
/// SPDX-License-Identifier: MIT                                              
///                                                                           
#include "Allocator.hpp"

#if not LANGULUS_FEATURE(MANAGED_MEMORY)
   #error "This file shouldn't be included if MANAGED_MEMORY is disabled"
#endif

#if 0
   #include <Langulus/Logger/EnableVerbose.hpp>
#else
   #include <Langulus/Logger/NoVerbose.hpp>
#endif


namespace Langulus::Fractalloc
{   
   /// Get least significant bit                                              
   /// https://stackoverflow.com/questions/757059                             
   LANGULUS(ALWAYS_INLINED)
   constexpr size_t LSB(const size_t n) noexcept {
      static_assert(sizeof(size_t) == 4 or sizeof(size_t) == 8,
         "Unsupported size_t");

      if constexpr (sizeof(size_t) == 4) {
         constexpr uint32_t DeBruijnBitPosition[32] = {
            0,  1, 28,  2, 29, 14, 24, 3, 30, 22, 20, 15, 25, 17,  4, 8,
            31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18,  6, 11,  5, 10, 9
         };
         const uint32_t nn = static_cast<uint32_t>(n);
         constexpr uint32_t f = 0x077CB531u;
         return DeBruijnBitPosition[(uint32_t {nn & (0 - nn)} * f) >> uint32_t {27}];
      }
      else {
         constexpr uint64_t DeBruijnBitPosition[64] = {
            0,   1,  2, 53,  3,  7, 54, 27,  4, 38, 41,  8, 34, 55, 48, 28,
            62,  5, 39, 46, 44, 42, 22,  9, 24, 35, 59, 56, 49, 18, 29, 11,
            63, 52,  6, 26, 37, 40, 33, 47, 61, 45, 43, 21, 23, 58, 17, 10,
            51, 25, 36, 32, 60, 20, 57, 16, 50, 31, 19, 15, 30, 14, 13, 12
         };
         const uint64_t nn = static_cast<uint64_t>(n);
         constexpr uint64_t f = 0x022fdd63cc95386dul;
         return DeBruijnBitPosition[(uint64_t {nn & (0 - nn)} * f) >> uint64_t {58}];
      }
   }

   /// Initialize a pool                                                      
   ///   @param dataAlignment taken from meta.GetAlignment()                  
   ///   @param dataMinAlloc taken from meta.GetMinAlloc()                    
   ///   @param poolAlignment the alignment of the pool itself                
   ///   @param size bytes of the usable block to initialize with             
   Pool::Pool(
      pot_t dataAlignment,
      pot_t dataMinAlloc,
      pot_t poolAlignment,
      pot_t size
   ) assumptious
      : mAllocationData {reinterpret_cast<Allocation*>(Align(reinterpret_cast<uintptr_t>(this + 1), alignof(Allocation)))}
      , mClientData     {reinterpret_cast<uint8_t*>(this) + Cost(dataAlignment, dataMinAlloc, size)}
      , mDataAlignment  {dataAlignment}
      , mDataMinAlloc   {dataMinAlloc}
      , mAllocatedByBackend {size}
      , mAlign          {::std::max(dataAlignment, pot_t(Alignment))}
      , mPoolAlignment  {poolAlignment}
      , mThresholdMin   {::std::max(dataMinAlloc, mAlign)}
      , mThresholdMax   {size}
      , mBiggestEntry   {mThresholdMin}
      , mMaxEntries     {size / mThresholdMin}
   {
      LglsAssumeDev(size >= mThresholdMin,
         "Size must be able to hold at least one allocation");
      LglsAssumeDev(mClientData >= reinterpret_cast<uint8_t*>(mAllocationData + static_cast<size_t>(mMaxEntries)),
         "Client data intersects allocation data");

      IF_LANGULUS_MEMORY_STATISTICS(mStep = Allocator::GetStatistics().mStep);

      // Touching is mandatory for pools - without touching the         
      // memory, it might remain just a promise by the OS, making       
      // initial pool allocations very, very, VERY slow at the most     
      // inappropriate of times.                                        
      Touch();
   }
   
   /// Get the cost of allocating a pool - this includes sizeof(Pool), all    
   /// possible entry overhead, including padding for alignment               
   size_t Pool::Cost(pot_t dataAlignment, pot_t dataMinAlloc, pot_t size) noexcept {
      const pot_t align = ::std::max(dataAlignment, pot_t(Alignment));
      const pot_t minAlloc = ::std::max(dataMinAlloc, align);
      const pot_t maxEntries = size / minAlloc;
      return Align(
         Align(sizeof(Pool), alignof(Allocation)) + maxEntries * sizeof(Allocation),
         align
      );
   }

   /// Free the whole pool chain                                              
   ///   @attention make sure this is called for the first pool in the chain  
   void Pool::FreePoolChain() {
      if (mNext)
         mNext->FreePoolChain();

      #if LANGULUS_COMPILER(MSVC) or LANGULUS_COMPILER(CLANG_CL)
         _aligned_free(this);
      #else
         ::std::free(this);
      #endif
   }

   /// Allocate an entry inside the pool                                      
   ///   @param bytes number of bytes to allocate                             
   ///   @return the new allocation, or nullptr if pool is full               
   auto Pool::Allocate(pot_t bytes) assumptious -> Allocation* {
      // Check if we can add a new entry                                
      if (mThresholdMin > bytes)
         bytes = mThresholdMin;
      if (not CanContain(bytes))
         return nullptr;

      Allocation* newEntry;
      if (mLastFreed) {
         // Recycle entries                                             
         newEntry = mLastFreed;
         LglsVerbose("Used last freed entry: ", Logger::Hex(mLastFreed));
         mLastFreed = mLastFreed->GetNextFreeEntry();
         LglsVerbose("Next freed entry is: ", Logger::Hex(mLastFreed));
         new (newEntry) Allocation {bytes, mPoolAlignment};

         if (bytes > mBiggestEntry)
            mBiggestEntry = bytes;
      }
      else {
         if (IsClogged())
            return nullptr;

         // The entire pool is full or empty, skip search for free      
         // spot, add a new allocation directly	instead                 
         newEntry = AllocationFromIndex(mNextEntry);
         new (newEntry) Allocation {bytes, mPoolAlignment};

         if (bytes > mBiggestEntry)
            mBiggestEntry = bytes;

         ++mNextEntry;

         if (mThresholdMax > mThresholdMin and ::std::has_single_bit(mNextEntry)) {
            // Next entry will move to the next level                   
            // Immediately adapt threshold accordingly, by allowing     
            // ever smaller entries, as long the size of the new        
            // allocation doesn't prevent it                            
            --mThresholdMax.bit;// >>= 1u;
         }
      }

      // Update the distribution                                        
      ++mDistribution[bytes.bit];
      LglsAssumeDev(
         mAllocatedByFrontend + static_cast<size_t>(bytes) > mAllocatedByFrontend,
         "mAllocatedByFrontend overflowed"
      );
      mAllocatedByFrontend += static_cast<size_t>(bytes);
      ++mValidEntries;
      LglsAssumeDev(mNextEntry >= mValidEntries,
         "Impossible number of valid entries: ", mNextEntry, " < ", mValidEntries);
      return newEntry;
   }

   /// Allocate an entry inside the pool                                      
   ///   @param entry_budget the biggest allowed entry ID (including)
   ///   @param bytes number of bytes to allocate                             
   ///   @return the new allocation, or nullptr if pool is full               
   auto Pool::AllocatePacked(size_t entry_budget, pot_t bytes)
   assumptious -> Allocation* {
      // Check if we can add a new entry                                
      if (mThresholdMin > bytes)
         bytes = mThresholdMin;
      if (not CanContain(bytes))
         return nullptr;

      Allocation* newEntry;
      if (mLastFreed and IndexFromAllocation(mLastFreed) <= entry_budget) {
         // Recycle entries                                             
         newEntry = mLastFreed;
         LglsVerbose("Used last freed entry: ", Logger::Hex(mLastFreed));
         mLastFreed = mLastFreed->GetNextFreeEntry();
         LglsVerbose("Next freed entry is: ", Logger::Hex(mLastFreed));
         new (newEntry) Allocation {bytes, mPoolAlignment};

         if (bytes > mBiggestEntry)
            mBiggestEntry = bytes;
      }
      else if (mNextEntry <= entry_budget) {
         if (IsClogged())
            return nullptr;

         // The entire pool is full or empty, skip search for free      
         // spot, add a new allocation directly	instead                 
         newEntry = AllocationFromIndex(mNextEntry);
         new (newEntry) Allocation {bytes, mPoolAlignment};

         if (bytes > mBiggestEntry)
            mBiggestEntry = bytes;

         ++mNextEntry;

         if (mThresholdMax > mThresholdMin and ::std::has_single_bit(mNextEntry)) {
            // Next entry will move to the next level                   
            // Immediately adapt threshold accordingly, by allowing     
            // ever smaller entries, as long the size of the new        
            // allocation doesn't prevent it                            
            --mThresholdMax.bit;// >>= 1u;
         }
      }
      else {
         // Entry ID exceeds budget
         return nullptr;
      }

      // Update the distribution                                        
      ++mDistribution[bytes.bit];
      LglsAssumeDev(
         mAllocatedByFrontend + static_cast<size_t>(bytes) > mAllocatedByFrontend,
         "mAllocatedByFrontend overflowed"
      );
      mAllocatedByFrontend += static_cast<size_t>(bytes);
      ++mValidEntries;
      LglsAssumeDev(mNextEntry >= mValidEntries,
         "Impossible number of valid entries: ", mNextEntry, " < ", mValidEntries);
      return newEntry;
   }

   /// Resize an entry                                                        
   ///   @param entry entry to resize                                         
   ///   @param bytes new number of bytes                                     
   ///   @return true if entry was enlarged without conflict                  
   bool Pool::Reallocate(Allocation* entry, pot_t bytes) assumptious {
      LglsAssumeDev(ContainsAllocation(entry) and entry->GetUses(),
         "Invalid deallocation");
      
      if (mThresholdMin > bytes)
         bytes = mThresholdMin;
      
      if (bytes > entry->GetSize()) {
         // We're enlarging the entry                                   
         // Make sure we don't violate max threshold                    
         if (bytes > mThresholdMax)
            return false;

         // Update the distribution                                     
         if (bytes > mBiggestEntry)
            mBiggestEntry = bytes;
         mAllocatedByFrontend += bytes - entry->GetSize();

         size_t it = entry->mSize;
         LglsAssumeDev(mDistribution[it], "Distribution underflow");
         --mDistribution[it];
         ++mDistribution[bytes.bit];
      }
      else {
         // We're shrinking the entry                                   
         // No checks required, just update the distribution            
         const size_t removal = entry->GetSize() - bytes;
         LglsAssumeDevAndOptimize(mAllocatedByFrontend >= removal,
            "mAllocatedByFrontend underflowed");
         mAllocatedByFrontend -= removal;

         size_t it = entry->mSize;
         LglsAssumeDev(mDistribution[it], "Distribution underflow");
         --mDistribution[it];
         ++mDistribution[bytes.bit];
         if (mBiggestEntry == entry->GetSize() and 0 == mDistribution[it]) {
            // All biggest entries have been removed and we can safely  
            // increase mThresholdMax, so collisions are less likely    
            while (not mDistribution[--it]);
            mBiggestEntry.bit = static_cast<uint8_t>(it);
            Trim();
         }
      }

      entry->mSize = bytes.bit;
      return true;
   }

   /// Remove an entry                                                        
   ///   @attention assumes entry is valid                                    
   ///   @param entry entry to remove                                         
   void Pool::Deallocate(Allocation* entry) assumptious {
      LglsAssumeDev(ContainsAllocation(entry),
         "Invalid deallocation - entry is not from this pool");
      LglsAssumeDev(entry->GetUses(),
         "Invalid deallocation - entry has already been deallocated");
      LglsAssumeDevAndOptimize(mNextEntry,
         "Pool shows no entries exists, yet a valid entry needs to be deallocated");
      LglsAssumeDev(mAllocatedByFrontend >= entry->GetSize(),
         "Bad frontend allocation size");

      const auto size = entry->GetSize();
      mAllocatedByFrontend -= static_cast<size_t>(size);
      entry->mReferences = 0;

      if (0 == mAllocatedByFrontend) {
         // The freed entry was the last used entry.                    
         // Reset the entire pool.                                      
         mThresholdMax = mAllocatedByBackend;
         mBiggestEntry = mThresholdMin;
         LglsVerbose("Freed entry chain reset completely - all entries were deallocated");
         mLastFreed = nullptr;
         mNextEntry = 0;
         mDistribution[size.bit] = 0;
         LglsAssumeDev(mValidEntries == 1, "Incorrect mValidEntries");
         mValidEntries = 0;
      }
      else {
         // Push the removed entry to the last freed list.              
         // The removed entry becomes the last freed entry, and its     
         // pool pointer becomes a jump to the previous last freed.     
         if (mLastFreed) {
            LglsAssumeDev(mLastFreed != entry,
               "Oops");
            LglsAssumeDev(mLastFreed->GetUses() == 0,
               "Free entry is in use - shouldn't be possible");
            entry->SetNextFreeEntry(mLastFreed);
         }
         else entry->ResetNextFreeEntry();
         
         LglsVerbose("New entry was freed, previous last freed was: ", Logger::Hex(mLastFreed));
         mLastFreed = entry;
         LglsVerbose("New last freed is: ", Logger::Hex(mLastFreed));
         LglsAssumeDev(mValidEntries > 1, "Incorrect mValidEntries");
         --mValidEntries;

         // Update the distribution                                     
         size_t it = size.bit;
         --mDistribution[it];
         if (mBiggestEntry == size and 0 == mDistribution[it]) {
            // All biggest entries have been removed and we can try to  
            // increase mThresholdMax, so collisions are less likely.   
            // This however is possible only after trimming entries     
            while (not mDistribution[--it])
               ;
            mBiggestEntry.bit = static_cast<uint8_t>(it);
            Trim();
         }
         else if (::std::has_single_bit(mValidEntries+1))
            Trim();
      }

      LglsAssumeDev(mNextEntry >= mValidEntries,
         "Impossible number of valid entries: ", mNextEntry, " < ", mValidEntries);
   }

   /// Get valid entry that corresponds to an arbitrary pointer               
   ///   @attention assumes ptr is inside pool                                
   ///   @param ptr the pointer to get the element index of                   
   ///   @return pointer to the valid allocation, or nullptr if unused        
   auto Pool::AllocationFromAddress(const void* ptr) const assumptious -> Allocation* {
      // Step up until a valid entry inside bounds is hit               
      auto index = IndexFromAddress(ptr);
      while (index != 0
        and (index >= mNextEntry or 0 == AllocationFromIndex(index)->GetUses()))
         index = UpIndex(index);

      // Check if we reached root of pool and it is unused              
      if (index == 0 and 0 == mAllocationData->GetUses())
         return nullptr;
      
      return AllocationFromIndex(index);
   }

   /// Null the client data                                                   
   void Pool::Null() {
      memset(mClientData, 0, static_cast<size_t>(mAllocatedByBackend));
   }

   /// Touch client data                                                      
   /// https://stackoverflow.com/questions/18929011                           
   void Pool::Touch() {
      auto it = mClientData;
      const auto itEnd = mClientData + static_cast<size_t>(mAllocatedByBackend);
      while (it < itEnd) {
         volatile auto touch = *it;
         (void) touch;
         it += 4096;
      }
   }
   
   /// Remove all empty entries at the end, increase mThresholdMax and lower  
   /// mNextEntry as much as possible. Will unclog the pool if able to.       
   void Pool::Trim() {
      if (mNextEntry == mValidEntries) {
         // Nothing to trim                                             
         mLastFreed = nullptr;
         return;
      }

      LglsAssumeDev(mNextEntry >= mValidEntries,
         "Impossible number of valid entries: ", mNextEntry, " < ", mValidEntries);

      {
         LglsAssumeDev(IsInUse(), "Should have at least one valid entry");
         //const size_t max_entries = static_cast<size_t>(mMaxEntries);

         //                                                             
         // First pass checks how many entries we can trim              
         size_t trimmed = mNextEntry - 1;
         //size_t entry_gap = 1u << (mMaxEntries.bit + 1 - ::std::bit_width(mNextEntry));
         //if (entry_gap < 2)
         //   entry_gap = 2;
         //auto entry = AllocationFromIndex(trimmed);
         while (trimmed) {
            auto entry = AllocationFromIndex(trimmed);
            LglsVerboseScoped("Trimming: ", Logger::Hex(entry));
            if (entry->GetUses()) {
               LglsVerbose("Trimming ceased - valid entry encountered");
               break;
            }
         
            //--trimmed;
            
            if (::std::has_single_bit(trimmed + 1) /*entry - entry_gap < mAllocationData*/) {
               // It is now safe to increase mThresholdMax (may unclog) 
               ++mThresholdMax.bit;
            
               // Level up, so wrap around back to the ending entry     
               //entry_gap <<= 1u;
               //entry = mAllocationData + max_entries - entry_gap;
               LglsVerbose("Trimmed and wrapped around");
            }
            else {
               //entry -= entry_gap;
               LglsVerbose("Trimmed");
            }

            --trimmed;
         }
      
         mNextEntry = trimmed + 1;
         LglsAssumeDev(mNextEntry >= mValidEntries,
            "Impossible number of trimmed entries: ", mNextEntry, " < ", mValidEntries,
            " after trimming ", trimmed, " entries"
         );
      }

      //                                                                
      // There's the rare case where trimmed count is all filled up.    
      // In this case, there's no need to stitch the free entry chain.  
      if (mNextEntry == mValidEntries) {
         mLastFreed = nullptr;
         return;
      }

      //                                                                
      // Second pass patches up the free entry chain                    
      auto is_in_range = [this](Allocation const* a) {
         const size_t i = a - mAllocationData;
         if (i == 0)
            return true;
         size_t i_clear_lsb = i & ~(i - 1u);
         size_t index = ((mMaxEntries + i) / i_clear_lsb - 1u) >> 1u;
         return index < mNextEntry;
      };

      LglsVerboseScoped("Remapping free chain, starting with: ", Logger::Hex(mLastFreed));
      while (mLastFreed and not is_in_range(mLastFreed)) {
         LglsVerboseScoped(Logger::Hex(mLastFreed),
            " fell out of range and is getting replaced...");
         mLastFreed = mLastFreed->GetNextFreeEntry();
         LglsVerbose("with ", Logger::Hex(mLastFreed));
      }

      if (mLastFreed) {
         LglsVerboseScoped("Patching up the free chain, starting with: ", Logger::Hex(mLastFreed));

         auto last_valid_freed = mLastFreed;
         auto freed = mLastFreed->GetNextFreeEntry();
         IF_SAFE(::std::unordered_set<Allocation*> mask);
         IF_SAFE(mask.insert(last_valid_freed));

         while (freed) {
            LglsAssumeDev(freed->GetUses() == 0,
               "Next free entry is in use - shouldn't be possible");

            if (is_in_range(freed)) {
               LglsAssumeDev(not mask.contains(freed),
                  "Pool free chain integrity failure");
               IF_SAFE(mask.insert(freed));

               LglsVerbose(Logger::Hex(last_valid_freed), " -> ", Logger::Hex(freed));
               last_valid_freed->SetNextFreeEntry(freed);
               last_valid_freed = freed;
            }
            else {
               LglsVerbose(Logger::Hex(freed), " fell out of range, skipping to: ",
                  Logger::Hex(freed->GetNextFreeEntry()));
            }

            freed = freed->GetNextFreeEntry();
         }
         
         last_valid_freed->ResetNextFreeEntry();
         LglsVerbose("Free chain finalized with: ", Logger::Hex(last_valid_freed));
         LglsAssumeDev(mask.size() == mNextEntry - mValidEntries,
            "Pool free chain count mismatch: ",
            mask.size(), " != ", mNextEntry - mValidEntries
         );
      }
   }

   /// Get threshold associated with an index                                 
   ///   @attention assumes index is not zero                                 
   ///   @param index the index                                               
   ///   @return the threshold                                                
   pot_t Pool::ThresholdFromIndex(size_t index) const noexcept {
      pot_t result;
      result.bit = mAllocatedByBackend.bit - ::std::bit_width(index);
      return result;
   }

   /// Get allocation from index                                              
   ///   @param index the index                                               
   ///   @return the allocation (not validated and constrained)               
   auto Pool::AllocationFromIndex(size_t index) const noexcept -> Allocation* {
      // Credit goes to Vladislav Penchev                               
      if (index == 0)
         return mAllocationData;

      const size_t basePower = ::std::bit_width(index);
      const size_t baselessIndex = index - (1u << (basePower - 1u));
      const size_t levelIndex = (baselessIndex << 1u) + 1u;
      const size_t levelSize = (1u << (mMaxEntries.bit - basePower));
      return mAllocationData + levelIndex * levelSize;
   }

   /// Get index from data pointer                                            
   ///   @attention assumes pointer is inside the pool                        
   ///   @param ptr the address                                               
   ///   @return the index                                                    
   size_t Pool::IndexFromAddress(const void* ptr) const assumptious {
      LglsAssumeDev(ContainsData(ptr), "Pointer is outside pool");

      // Credit goes to Yasen Vidolov                                   
      const size_t i = static_cast<const uint8_t*>(ptr) - mClientData;
      if (i < mThresholdMax or 0 == mNextEntry)
         return 0;

      // We got the index, but it is not constrained to the pool        
      size_t i_clear_lsb = i & ~(i - 1u);
      size_t index = ((mAllocatedByBackend + i) / i_clear_lsb - 1u) >> 1u;
      while (index >= mNextEntry)
         index = UpIndex(index);
      return index;
   }

   /// Get index from allocation pointer                                      
   ///   @attention assumes pointer is inside the pool's allocation data      
   ///   @param ptr the address                                               
   ///   @return the index                                                    
   size_t Pool::IndexFromAllocation(const Allocation* ptr) const assumptious {
      LglsAssumeDev(ContainsAllocation(ptr), "Allocation is outside pool");
      if (0 == mNextEntry or ptr == mAllocationData)
         return 0;

      const size_t i = ptr - mAllocationData;
      size_t i_clear_lsb = i & ~(i - 1u);
      size_t index = ((mMaxEntries + i) / i_clear_lsb - 1u) >> 1u;
      while (index >= mNextEntry)
         index = UpIndex(index);
      return index;
   }

   /// Get index above another index                                          
   ///   @param index                                                         
   ///   @return index above the given one                                    
   size_t Pool::UpIndex(const size_t index) const noexcept {
      // Credit goes to Vladislav Penchev                               
      return index >> (LSB(index) + 1uz);
   }

   /// Check if a memory address resigns inside pool's range                  
   ///   @param address address to check                                      
   ///   @return true if address belongs to this pool                         
   bool Pool::ContainsData(const void* address) const noexcept {
      return address >= mClientData
         and address < mClientData + static_cast<size_t>(mAllocatedByBackend);
   }

   /// Check if an allocation resigns inside pool's range                     
   ///   @param address allocation to check                                   
   ///   @return true if allocation belongs to this pool                      
   bool Pool::ContainsAllocation(const Allocation* address) const noexcept {
      return address >= mAllocationData
         and address < mAllocationData + static_cast<size_t>(mMaxEntries);
   }

   /// Find a memory entry from pointer                                       
   ///   @param memory memory pointer                                         
   ///   @return the memory entry that manages the memory pointer, or         
   ///      nullptr if memory is not ours, or is no longer used               
   auto Pool::Find(const void* memory) const assumptious -> const Allocation* {
      if (not ContainsData(memory))
         return nullptr;

      const auto entry = AllocationFromAddress(memory);
      return entry and entry->Contains(memory) ? entry : nullptr;
   }
}
