///                                                                           
/// Langulus::Fractalloc                                                      
/// Copyright(C) 2015 Dimo Markov <langulusteam@gmail.com>                    
///                                                                           
/// Distributed under GNU General Public License v3+                          
/// See LICENSE file, or https://www.gnu.org/licenses                         
///                                                                           
#pragma once
#include <Fractalloc/Allocator.hpp>
#include <bit>

namespace Langulus::Fractalloc
{

   /// Initialize a pool                                                      
   ///   @attention relies that size is a power-of-two                        
   ///   @attention this constructor relies that instance is placed in the    
   ///      beginning of a heap allocation of size Pool::NewAllocationSize()  
   ///   @param meta - optional meta data associated with pool                
   ///   @param size - bytes of the usable block to initialize with           
   ///   @param memory - handle for use with std::free()                      
   inline Pool::Pool(DMeta meta, const Size& size, void* memory) noexcept
      : mAllocatedByBackend {size}
      , mAllocatedByBackendLog2 {Inner::FastLog2(size)}
      , mAllocatedByBackendLSB {Inner::LSB(size >> Size{1})}
      , mThreshold {size}
      , mThresholdPrevious {size}
      , mThresholdMin {Roof2cexpr(meta 
         ? meta->mAllocationPage 
         : Allocation::GetMinAllocation())}
      , mMeta {meta}
      , mHandle {memory} {
      mMemory = GetPoolStart<Byte>();
      mMemoryEnd = mMemory + mAllocatedByBackend;
      mNextEntry = mMemory;

      // Touching is mandatory for pools - without touching the         
      // memory, it might remain just a promise by the OS, making       
      // initial pool allocations very, very, VERY slow                 
      Touch();
   }

   /// Get the minimum allocation for an entry inside this pool               
   ///   @return the size in bytes, always a power-of-two                     
   constexpr Size Pool::GetMinAllocation() const noexcept {
      return mThresholdMin;
   }

   /// Get the total byte size of the pool, including overhead                
   ///   @return the size in bytes                                            
   constexpr Size Pool::GetTotalSize() const noexcept {
      return Pool::GetSize() + mAllocatedByBackend;
   }

   /// Get the max number of possible entries                                 
   /// (if all of them are as small as possible)                              
   ///   @return the size in bytes, always a power-of-two                     
   constexpr Count Pool::GetMaxEntries() const noexcept {
      return mAllocatedByBackend / GetMinAllocation();
   }

   /// Free the whole pool chain                                              
   ///   @attention make sure this is called for the first pool in the chain  
   inline void Pool::FreePoolChain() {
      if (mNext)
         mNext->FreePoolChain();
      ::std::free(mHandle);
   }

   /// Get the size of the Pool structure, rounded up for alignment           
   ///   @return the byte size of the pool, including alignment               
   constexpr Size Pool::GetSize() noexcept {
      return sizeof(Pool) + Alignment - (sizeof(Pool) % Alignment);
   }

   /// Get the size for a new pool allocation, with alignment/additional      
   /// memory requirements                                                    
   ///   @assumes size is a power-of-two                                      
   ///   @assumes size can contain at least one Allocation::GetMinAllocation  
   ///   @param size - the number of bytes to request for the pool            
   ///   @return the number of bytes to allocate for use in the pool          
   constexpr Size Pool::GetNewAllocationSize(const Size& size) noexcept {
      constexpr Size minimum {Pool::DefaultPoolSize + Pool::GetSize()};
      return ::std::max(size + Pool::GetSize(), minimum);
   }

   /// Get the start of the usable memory for the pool                        
   ///   @return the start of the memory                                      
   template<class T>
   inline T* Pool::GetPoolStart() noexcept {
      const auto poolStart = reinterpret_cast<Byte*>(this);
      return reinterpret_cast<T*>(poolStart + Pool::GetSize());
   }

   /// Get the start of the usable memory for the pool (const)                
   ///   @return the start of the memory                                      
   template<class T>
   inline const T* Pool::GetPoolStart() const noexcept {
      return const_cast<Pool*>(this)->template GetPoolStart<T>();
   }

   /// Get the true allocation size, as bytes requested from OS               
   ///   @return bytes allocated for the pool, including alignment/overhead   
   constexpr Size Pool::GetAllocatedByBackend() const noexcept {
      return mAllocatedByBackend;
   }

   /// Get the allocation size, as bytes requested from client                
   ///   @return bytes allocated by the client                                
   constexpr Size Pool::GetAllocatedByFrontend() const noexcept {
      return mAllocatedByFrontend;
   }

   /// Allocate an entry inside the pool - returned pointer is aligned        
   ///   @param bytes - number of bytes to allocate                           
   ///   @return the new allocation, or nullptr if pool is full               
   inline Allocation* Pool::Allocate(const Size bytes) SAFETY_NOEXCEPT() {
      constexpr Offset one {1};

      // Check if we can add a new entry                                
      const auto bytesWithPadding = Allocation::GetNewAllocationSize(bytes);
      if (!CanContain(bytesWithPadding)) UNLIKELY()
         return nullptr;

      Allocation* newEntry;
      if (mLastFreed) {
         // Recycle entries                                             
         newEntry = mLastFreed;
         mLastFreed = mLastFreed->mNextFreeEntry;
         new (newEntry) Allocation {bytesWithPadding - Allocation::GetSize(), this};
      }
      else {
         // The entire pool is full (or empty), skip search for free    
         // spot, add a new allocation directly	instead                 
         newEntry = reinterpret_cast<Allocation*>(mNextEntry);
         new (newEntry) Allocation {bytesWithPadding - Allocation::GetSize(), this};
         ++mEntries;

         // Move carriage to the next entry                             
         mNextEntry += mThresholdPrevious;

         if (mNextEntry >= mMemoryEnd) UNLIKELY() {
            // Reset carriage and shift level when it goes beyond       
            mThresholdPrevious = mThreshold;
            mThreshold >>= one;
            mNextEntry = mMemory + mThreshold;
         }
      }

      // Always adapt min threshold if bigger entry is introduced       
      if (bytesWithPadding > mThresholdMin) {
         mThresholdMin = Roof2(bytesWithPadding);
         //TODO everytime min threshold changes, 
         // part of the freed entry chain may get invalid?
         // traverse and stitch here?
      }

      LANGULUS_ASSUME(DevAssumes,
         mAllocatedByFrontend + bytesWithPadding >= mAllocatedByFrontend,
         "Frontend byte counter overflow");
      mAllocatedByFrontend += bytesWithPadding;
      return newEntry;
   }

   /// Remove an entry                                                        
   ///   @attention assumes entry is valid                                    
   ///   @param entry - entry to remove                                       
   inline void Pool::Deallocate(Allocation* entry) SAFETY_NOEXCEPT() {
      LANGULUS_ASSUME(DevAssumes, entry->mReferences != 0,
         "Removing an invalid entry");
      LANGULUS_ASSUME(DevAssumes, mEntries,
         "Bad valid entry count");
      LANGULUS_ASSUME(DevAssumes, mAllocatedByFrontend >= entry->GetTotalSize(),
         "Bad frontend allocation size");

      mAllocatedByFrontend -= entry->GetTotalSize();
      entry->mReferences = 0;

      if (0 == mAllocatedByFrontend) {
         // The freed entry was the last used entry                     
         // Reset the entire pool                                       
         mThreshold = mThresholdPrevious = mAllocatedByBackend;
         mThresholdMin = Allocation::GetMinAllocation();
         mLastFreed = nullptr;
         mEntries = 0;
         mNextEntry = mMemory;
      }
      else {
         // Push the removed entry to the last freed list               
         // The removed entry becomes the last freed entry, and its     
         // pool pointer becomes a jump to the previous last freed      
         entry->mNextFreeEntry = mLastFreed;
         mLastFreed = entry;

         //TODO: keep track of size distrubution, 
         // shrink min threshold if all leading buckets go empty
      }
   }

   /// Resize an entry                                                        
   ///   @param entry - entry to resize                                       
   ///   @param bytes - new number of bytes                                   
   ///   @return true if entry was enlarged without conflict                  
   inline bool Pool::Reallocate(Allocation* entry, const Size bytes) SAFETY_NOEXCEPT() {
      LANGULUS_ASSUME(DevAssumes,
         bytes && Contains(entry) && entry && entry->GetUses(),
         "Invalid reallocation");

      if (bytes > entry->mAllocatedBytes) {
         // We're enlarging the entry                                   
         // Make sure we don't violate threshold                        
         const auto addition = bytes - entry->mAllocatedBytes;
         const auto newtotal = entry->GetTotalSize() + addition;
         if (newtotal > mThreshold)
            return false;

         if (newtotal > mThresholdMin) {
            mThresholdMin = Roof2(newtotal);
            //TODO everytime min threshold changes, 
            // part of the freed entry chain may get invalid?
            // traverse abd stitch here?
         }

         mAllocatedByFrontend += addition;
      }
      else {
         // We're shrinking the entry                                   
         // No checks required                                          
         const auto removal = entry->mAllocatedBytes - bytes;
         LANGULUS_ASSUME(DevAssumes, mAllocatedByFrontend >= removal,
            "Bad frontend allocation size");
         mAllocatedByFrontend -= removal;

         //TODO: keep track of size distrubution, 
         // shrink min threshold if all leading buckets go empty
      }

      entry->mAllocatedBytes = bytes;
      return true;
   }

   /// Get valid entry that corresponds to an arbitrary pointer               
   ///   @attention assumes ptr is inside pool                                
   ///   @param ptr - the pointer to get the element index of                 
   ///   @return pointer to the valid allocation, or nullptr if unused        
   inline Allocation* Pool::AllocationFromAddress(const void* ptr) SAFETY_NOEXCEPT() {
      const auto index = ValidateIndex(IndexFromAddress(ptr));
      return index == InvalidIndex ? nullptr : AllocationFromIndex(index);
   }

   /// Get valid entry that corresponds to an arbitrary pointer               
   ///   @attention assumes ptr is inside pool                                
   ///   @param ptr - the pointer to get the element index of                 
   ///   @return pointer to the valid allocation, or nullptr if unused        
   inline const Allocation* Pool::AllocationFromAddress(const void* ptr) const noexcept {
      return const_cast<Pool*>(this)->AllocationFromAddress(ptr);
   }

   /// Check if there is any used memory                                      
   ///   @return true on at least one valid entry                             
   constexpr bool Pool::IsInUse() const noexcept {
      return mAllocatedByFrontend > 0;
   }

   /// Check if memory can contain a number of bytes                          
   ///   @attention assumes that bytes include any padding and overhead       
   ///   @param bytes - number of bytes to check                              
   ///   @return true if bytes can be contained in a new/recycled element     
   constexpr bool Pool::CanContain(const Size& bytes) const noexcept {
      return mThreshold >= mThresholdMin && bytes <= mThreshold;
   }

   /// Null the memory                                                        
   inline void Pool::Null() {
      ZeroMemory(mMemory, mAllocatedByBackend);
   }

   /// Touch unused memory                                                    
   /// https://stackoverflow.com/questions/18929011                           
   inline void Pool::Touch() {
      auto it = mMemory;
      while (it < mMemoryEnd) {
         volatile auto touch = *it;
         (void) touch;
         it += 4096;
      }
   }

   /// Get threshold associated with an index                                 
   ///   @attention assumes index is not zero                                 
   ///   @param index - the index                                             
   ///   @return the threshold                                                
   inline Size Pool::ThresholdFromIndex(const Offset& index) const noexcept {
      return Size {1} << (mAllocatedByBackendLSB - Inner::FastLog2(index));
   }

   /// Get allocation from index                                              
   ///   @param index - the index                                             
   ///   @return the allocation (not validated and constrained)               
   inline Allocation* Pool::AllocationFromIndex(const Offset& index) noexcept {
      // Credit goes to Vladislav Penchev (G2)                          
      if (index == 0)
         return reinterpret_cast<Allocation*>(mMemory);

      constexpr Size one {1};
      const Size basePower = Inner::FastLog2(index);
      const Size baselessIndex = index - (one << basePower);
      const Size levelIndex = (baselessIndex << one) + one;
      const Size levelSize = (one << (mAllocatedByBackendLSB - basePower));
      return reinterpret_cast<Allocation*>(mMemory + levelIndex * levelSize);
   }

   /// Get allocation from index (const)                                      
   ///   @param index - the index                                             
   ///   @return the allocation (not validated and constrained)               
   inline const Allocation* Pool::AllocationFromIndex(const Offset& index) const noexcept {
      return const_cast<Pool*>(this)->AllocationFromIndex(index);
   }

   /// Get index from address                                                 
   ///   @attention assumes pointer is inside the pool                        
   ///   @param ptr - the address                                             
   ///   @return the index                                                    
   inline Offset Pool::IndexFromAddress(const void* ptr) const SAFETY_NOEXCEPT() {
      LANGULUS_ASSUME(DevAssumes, Contains(ptr), "Entry outside pool");

      // Credit goes to Yasen Vidolov (G1)                              
      const Offset i = static_cast<const Byte*>(ptr) - mMemory;
      if (i < mThreshold || 0 == mEntries)
         return 0;

      // We got the index, but it is not constrained to the pool        
      constexpr Offset one {1};
      Offset index = ((mAllocatedByBackend + i) / (i & ~(i - one)) - one) >> one;
      while (index >= mEntries)
         index = UpIndex(index);
      return index;
   }

   /// Validate an index, check if corresponding to a valid allocation        
   /// or shift it up until one is found                                      
   ///   @param index - index to validate                                     
   ///   @returns the valid index, or InvalidIndex if invalid                 
   inline Offset Pool::ValidateIndex(Offset index) const noexcept {
      // Pool is empty, so search is pointless                          
      if (mEntries == 0)
         return InvalidIndex;

      // Step up until a valid entry inside bounds is hit               
      while (index != 0 && (index >= mEntries || 0 == AllocationFromIndex(index)->GetUses()))
         index = UpIndex(index);

      // Check if we reached root of pool and it is unused              
      if (index == 0 && 0 == reinterpret_cast<const Allocation*>(mMemory)->GetUses())
         return InvalidIndex;
      return index;
   }

   /// Get index above another index                                          
   ///   @param index                                                         
   ///   @return index above the given one                                    
   inline Offset Pool::UpIndex(const Offset index) const noexcept {
      // Credit goes to Vladislav Penchev                               
      return index >> (Inner::LSB(index) + Offset {1});
   }

   /// Check if a memory address resigns inside pool's range                  
   ///   @param address - address to check                                    
   ///   @return true if address belongs to this pool                         
   inline bool Pool::Contains(const void* address) const noexcept {
      return address >= mMemory && address < mMemory + mAllocatedByBackend;
   }

   /// Find a memory entry from pointer                                       
   ///   @param memory - memory pointer                                       
   ///   @return the memory entry that manages the memory pointer, or         
   ///      nullptr if memory is not ours, or is no longer used               
   inline Allocation* Pool::Find(const void* memory) SAFETY_NOEXCEPT() {
      if (Contains(memory)) {
         const auto entry = AllocationFromAddress(memory);
         return entry && entry->Contains(memory) ? entry : nullptr;
      }

      return nullptr;
   }

   /// Find a memory entry from pointer (const)                               
   ///   @attention assumes memory is a valid pointer                         
   ///   @param memory - memory pointer                                       
   ///   @return the memory entry that manages the memory pointer, or         
   ///      nullptr if memory is not ours, or is no longer used               
   inline const Allocation* Pool::Find(const void* memory) const SAFETY_NOEXCEPT() {
      return const_cast<Pool*>(this)->Find(memory);
   }

} // namespace Langulus::Fractalloc
