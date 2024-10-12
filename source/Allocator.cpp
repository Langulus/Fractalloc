///                                                                           
/// Langulus::Fractalloc                                                      
/// Copyright (c) 2015 Dimo Markov <team@langulus.com>                        
/// Part of the Langulus framework, see https://langulus.com                  
///                                                                           
/// SPDX-License-Identifier: MIT                                              
///                                                                           
#include <Fractalloc/Allocator.hpp>
#include <RTTI/Assume.hpp>
#include "Pool.inl"
#include "Allocation.inl"

#if 0
   #include <Logger/Logger.hpp>
   #define VERBOSE(...)      Langulus::Logger::Info(__VA_ARGS__)
   #define VERBOSE_TAB(...)  const auto tab = Langulus::Logger::InfoTab(__VA_ARGS__)
#else
   #define VERBOSE(...)      LANGULUS(NOOP)
   #define VERBOSE_TAB(...)  LANGULUS(NOOP)
#endif


namespace Langulus::Fractalloc
{
   
   using RTTI::MetaData;

   /// MSVC will likely never support std::aligned_alloc, so we use           
   /// a custom portable routine that's almost the same                       
   /// https://stackoverflow.com/questions/62962839                           
   ///                                                                        
   /// Each allocation has the following prefixed bytes:                      
   /// [padding][T::GetSize()][client bytes...]                               
   ///                                                                        
   ///   @param size - the number of client bytes to allocate                 
   ///   @return a newly allocated memory that is correctly aligned           
   template<AllocationPrimitive T>
   T* AlignedAllocate(DMeta hint, Offset size) IF_UNSAFE(noexcept) {
      const auto finalSize = T::GetNewAllocationSize(size) + Alignment;
      const auto base = ::std::malloc(finalSize);
      if (not base)
         return nullptr;

      // Align pointer to the alignment LANGULUS was built with         
      auto ptr = reinterpret_cast<T*>(
         (reinterpret_cast<Offset>(base) + Alignment)
         & ~(Alignment - Offset {1})
      );

      // Place the entry there                                          
      new (ptr) T {hint, size, base};
      return ptr;
   }

   /// Global allocator interface                                             
   Allocator Instance {};

   /// Allocate a memory entry                                                
   ///   @attention doesn't call any constructors                             
   ///   @attention doesn't throw - check if return is nullptr                
   ///   @attention assumes size is not zero                                  
   ///   @param hint - optional meta data to associate pool with              
   ///   @param size - the number of bytes to allocate                        
   ///   @return the allocation, or nullptr if out of memory                  
   Allocation* Allocator::Allocate(RTTI::DMeta hint, Offset size) IF_UNSAFE(noexcept) {
      LANGULUS_ASSUME(DevAssumes, size, "Zero allocation is not allowed");

      // Decide pool chain, based on hint                               
      Pool* pool = nullptr;
      if (hint) {
         switch (hint->mPoolTactic) {
         case RTTI::PoolTactic::Size:
            pool = Instance.mSizePoolChain[Inner::FastLog2(hint->mSize)];
            break;
         case RTTI::PoolTactic::Type:
            pool = hint->GetPool<Pool>();
            break;
         case RTTI::PoolTactic::Main:
            pool = Instance.mMainPoolChain;
            break;
         }
      }
      else pool = Instance.mMainPoolChain;

      //	Attempt to place allocation in the default chain               
      Allocation* memory = nullptr;
      while (pool) {
         memory = pool->Allocate(size);
         if (memory)
            break;
         pool = pool->mNext;
      }

      if (memory) {
         VERBOSE_TAB(
            "Fractalloc: ", Logger::Green, "New allocation ", Logger::Hex(memory),
            " of size ", Size {size}, ", in pool ", Logger::Hex(pool)
         );

         if (hint) {
            switch (hint->mPoolTactic) {
            case RTTI::PoolTactic::Size:
               VERBOSE("Type was: ", hint->mToken, " (size pool tactic)");
               break;
            case RTTI::PoolTactic::Type:
               VERBOSE("Type was: ", hint->mToken, " (type pool tactic)");
               break;
            case RTTI::PoolTactic::Main:
               VERBOSE("Type was: ", hint->mToken, " (default pool tactic)");
               break;
            }
         }
         else VERBOSE("Type was unknown (default pool tactic)");

         #if LANGULUS_FEATURE(MEMORY_STATISTICS)
            auto& stats = Instance.mStatistics;
            stats.mEntries += 1;
            stats.mBytesAllocatedByFrontend += memory->GetTotalSize();
         #endif
         return memory;
      }

      // If reached, pool chain can't contain the memory                
      // Allocate a new pool and add it at the front of hinted chain    
      pool = AllocatePool(nullptr, Allocation::GetNewAllocationSize(size));
      if (not pool)
         return nullptr;

      VERBOSE(
         "Fractalloc: ", Logger::Cyan, "New pool ", Logger::Hex(pool),
         " of size ", Size {pool->GetAllocatedByBackend()}
      );

      memory = pool->Allocate(size);

      VERBOSE_TAB(
         "Fractalloc: ", Logger::Green, "New allocation ", Logger::Hex(memory),
         " of size ", Size {size}, ", in pool ", Logger::Hex(pool)
      );

      if (hint) {
         switch (hint->mPoolTactic) {
         case RTTI::PoolTactic::Size: {
            VERBOSE("Type was: ", hint->mToken, " (size pool tactic)");
            auto& sizeChain = Instance.mSizePoolChain[Inner::FastLog2(hint->mSize)];
            pool->mNext = sizeChain;
            sizeChain = pool;
            break;
         }
         case RTTI::PoolTactic::Type: {
            VERBOSE("Type was: ", hint->mToken, " (type pool tactic)");
            auto& relevantPool = hint->GetPool<Pool>();
            pool->mNext = relevantPool;
            relevantPool = pool;
            Instance.mInstantiatedTypes.insert(&*hint);
            break;
         }
         case RTTI::PoolTactic::Main:
            VERBOSE("Type was: ", hint->mToken, " (main pool tactic)");
            pool->mNext = Instance.mMainPoolChain;
            Instance.mMainPoolChain = pool;
            break;
         }
      }
      else {
         VERBOSE("Type was unknown (main pool tactic)");
         pool->mNext = Instance.mMainPoolChain;
         Instance.mMainPoolChain = pool;
      }

      #if LANGULUS_FEATURE(MEMORY_STATISTICS)
      Instance.mStatistics.AddPool(pool);
      #endif
      return memory;
   }

   /// Reallocate a memory entry                                              
   ///   @attention never calls any constructors                              
   ///   @attention never copies any data                                     
   ///   @attention never deallocates previous entry                          
   ///   @attention returned entry might be different from the previous       
   ///   @attention doesn't throw - check if return is nullptr                
   ///   @param size - the number of bytes to allocate                        
   ///   @param previous - the previous memory entry                          
   ///   @return the reallocated memory entry, or nullptr if out of memory    
   Allocation* Allocator::Reallocate(Offset size, Allocation* previous)
   IF_UNSAFE(noexcept) {
      LANGULUS_ASSUME(DevAssumes, previous,
         "Reallocating nullptr");
      UNUSED() const auto as = previous->GetAllocatedSize();
      LANGULUS_ASSUME(DevAssumes, size != as,
         "Reallocation suboptimal - size is same as previous");
      LANGULUS_ASSUME(DevAssumes, size,
         "Zero reallocation is not allowed");
      LANGULUS_ASSUME(DevAssumes, previous->mReferences,
         "Deallocating an unused allocation");

      #if LANGULUS_FEATURE(MEMORY_STATISTICS)
         const auto oldSize = previous->GetTotalSize();
      #endif

      // New size is bigger, precautions must be taken                  
      if (previous->mPool->Reallocate(previous, size)) {
         #if LANGULUS_FEATURE(MEMORY_STATISTICS)
            auto& stats = Instance.mStatistics;
            stats.mBytesAllocatedByFrontend -= oldSize;
            stats.mBytesAllocatedByFrontend += previous->GetTotalSize();
         #endif

         VERBOSE(
            "Fractalloc: ", Logger::Yellow, "Allocation ", Logger::Hex(previous),
            " was reallocated from ", Size {as}, " to ", Size {size}
         );
         return previous;
      }

      // If this is reached, we have a collision, so new entry is made  
      return Allocate(previous->mPool->mMeta, size);
   }
   
   /// Deallocate a memory allocation                                         
   ///   @attention assumes entry is a valid entry under jurisdiction         
   ///   @attention doesn't call any destructors                              
   ///   @param entry - the memory entry to deallocate                        
   void Allocator::Deallocate(Allocation* entry) IF_UNSAFE(noexcept) {
      LANGULUS_ASSUME(DevAssumes, entry,
         "Deallocating nullptr");
      LANGULUS_ASSUME(DevAssumes, entry->GetAllocatedSize(),
         "Deallocating an empty allocation");
      LANGULUS_ASSUME(DevAssumes, entry->mReferences,
         "Deallocating an unused allocation");
      LANGULUS_ASSUME(DevAssumes, entry->mReferences == 1,
         "Deallocating an allocation used from multiple places");

      VERBOSE(
         "Fractalloc: ", Logger::Red, "Allocation ", Logger::Hex(entry),
         " of size ", Size {entry->GetAllocatedSize()}, " was deallocated"
      );

      #if LANGULUS_FEATURE(MEMORY_STATISTICS)
         auto& stats = Instance.mStatistics;
         stats.mBytesAllocatedByFrontend -= entry->GetTotalSize();
         stats.mEntries -= 1;
      #endif

      entry->mPool->Deallocate(entry);
   }

   /// Allocate a pool                                                        
   ///   @attention the pool must be deallocated with DeallocatePool          
   ///   @param hint - optional meta data to associate pool with              
   ///   @param size - size of the pool (in bytes)                            
   ///   @return a pointer to the new pool                                    
   Pool* Allocator::AllocatePool(DMeta hint, Offset size) IF_UNSAFE(noexcept) {
      const auto poolSize = ::std::max(Pool::DefaultPoolSize, Roof2(size));
      return AlignedAllocate<Pool>(hint, poolSize);
   }

   /// Deallocate a pool                                                      
   ///   @attention doesn't call any destructors                              
   ///   @attention pool or any entry inside is no longer valid after this    
   ///   @attention assumes pool is a valid pointer                           
   ///   @param pool - the pool to deallocate                                 
   void Allocator::DeallocatePool(Pool* pool) IF_UNSAFE(noexcept) {
      LANGULUS_ASSUME(DevAssumes, pool, "Nullptr provided");
      ::std::free(pool->mHandle);
   }

   /// Deallocates all unused pools in a chain                                
   ///   @param chainStart - [in/out] the start of the chain                  
   void Allocator::CollectGarbageChain(Pool*& chainStart) {
      while (chainStart) {
         if (chainStart->IsInUse()) {
            chainStart->Trim();
            break;
         }

         #if LANGULUS_FEATURE(MEMORY_STATISTICS)
            mStatistics.DelPool(chainStart);
         #endif

         auto next = chainStart->mNext;
         VERBOSE(
            "Fractalloc: ", Logger::DarkCyan, "Pool ", Logger::Hex(chainStart),
            " of size ", Size {chainStart->GetAllocatedByBackend()}, " was deallocated"
         );
         DeallocatePool(chainStart);
         chainStart = next;
      }

      if (not chainStart)
         return;

      auto prev = chainStart;
      auto pool = chainStart->mNext;
      while (pool) {
         if (pool->IsInUse()) {
            pool->Trim();
            prev = pool;
            pool = pool->mNext;
            continue;
         }

         #if LANGULUS_FEATURE(MEMORY_STATISTICS)
            mStatistics.DelPool(pool);
         #endif

         const auto next = pool->mNext;
         VERBOSE(
            "Fractalloc: ", Logger::DarkCyan, "Pool ", Logger::Hex(pool),
            " of size ", Size {pool->GetAllocatedByBackend()}, " was deallocated"
         );
         DeallocatePool(pool);
         prev->mNext = next;
         pool = next;
      }
   }
   
   /// Deallocates all unused pools                                           
   void Allocator::CollectGarbage() {
      Instance.mLastFoundPool = nullptr;

      // Cleanup the main chain                                         
      Instance.CollectGarbageChain(Instance.mMainPoolChain);

      // Cleanup all size chains                                        
      for (auto& sizeChain : Instance.mSizePoolChain)
         Instance.CollectGarbageChain(sizeChain);

      // Cleanup all type chains                                        
      auto& types = Instance.mInstantiatedTypes;
      for (auto typeChain =  types.begin(); typeChain != types.end();) {
         auto& relevantPool = (*typeChain)->GetPool<Pool>();
         Instance.CollectGarbageChain(relevantPool);

         // Also discard the type if no pools remain                    
         if (not relevantPool)
            typeChain = types.erase(typeChain);
         else
            ++typeChain;
      }
   }
   
#if LANGULUS_FEATURE(MANAGED_REFLECTION)
   /// Check RTTI boundary for allocated pools                                
   /// Useful to decide when shared library is no longer used and is ready    
   /// to be unloaded. Use it after a call to CollectGarbage                  
   ///   @param boundary - the boundary name                                  
   ///   @return the number of pools                                          
   Count Allocator::CheckBoundary(const Token& boundary) noexcept {
      Count count = 0;
      for (auto type : Instance.mInstantiatedTypes) {
         if (type->mLibraryName == boundary) {
            auto pool = type->GetPool<Pool>();
            while (pool) {
               ++count;
               pool = pool->mNext;
            }
         }
      }
      return count;
   }
#endif

   /// Search in a pool chain                                                 
   ///   @param memory - memory pointer                                       
   ///   @param pool - start of the pool chain                                
   ///   @return the memory entry that contains the memory pointer, or        
   ///           nullptr if memory is not ours, its entry is no longer used   
   const Allocation* Allocator::FindInChain(const void* memory, const Pool* pool) const IF_UNSAFE(noexcept) {
      while (pool) {
         const auto found = pool->Find(memory);
         if (found) {
            mLastFoundPool = pool;
            return found;
         }

         // Continue inside the poolchain                               
         pool = pool->mNext;
      }

      return nullptr;
   }
   
   /// Search if memory is contained inside a pool chain                      
   ///   @param memory - memory pointer                                       
   ///   @param pool - start of the pool chain                                
   ///   @return true if we have authority over the memory                    
   bool Allocator::ContainedInChain(const void* memory, const Pool* pool) const IF_UNSAFE(noexcept) {
      while (pool) {
         if (pool->Contains(memory))
            return true;

         // Continue inside the poolchain                               
         pool = pool->mNext;
      }

      return false;
   }

   /// Find a memory entry from pointer                                       
   /// Allows us to safely interface unknown memory, possibly reusing it      
   /// Optimized for consecutive searches in near memory                      
   ///   @param hint - the type of data to search for (optional)              
   ///                 always provide hint for optimal performance            
   ///   @param memory - memory pointer                                       
   ///   @return the memory entry that contains the memory pointer, or        
   ///           nullptr if memory is not ours, its entry is no longer used   
   const Allocation* Allocator::Find(DMeta hint, const void* memory) IF_UNSAFE(noexcept) {
      // Scan the last pool that found something (hot region)           
      //TODO consider a whole stack of those?
      if (Instance.mLastFoundPool) {
         const auto found = Instance.mLastFoundPool->Find(memory);
         if (found)
            return found;
      }

      // Decide pool chains, based on hint                              
      const Allocation* result;
      if (hint) {
         switch (hint->mPoolTactic) {
         case RTTI::PoolTactic::Size: {
            // Hint is sized, so check in size pool chain first         
            const auto sizebucket = Inner::FastLog2(hint->mSize);
            result = Instance.FindInChain(memory, Instance.mSizePoolChain[sizebucket]);
            if (result)
               return result;

            // Then check default pool chain                            
            // (pointer could be a member of default-pooled type)       
            result = Instance.FindInChain(memory, Instance.mMainPoolChain);
            if (result)
               return result;

            // Check all typed pool chains                              
            // (pointer could be a member of type-pooled type)          
            for (auto& type : Instance.mInstantiatedTypes) {
               result = Instance.FindInChain(memory, type->GetPool<Pool>());
               if (result)
                  return result;
            }

            // Finally, check all other size pool chains                
            // (pointer could be a member of differently sized type)    
            for (Offset i = 0; i < sizebucket; ++i) {
               result = Instance.FindInChain(memory, Instance.mSizePoolChain[i]);
               if (result)
                  return result;
            }
            for (Offset i = sizebucket + 1; i < SizeBuckets; ++i) {
               result = Instance.FindInChain(memory, Instance.mSizePoolChain[i]);
               if (result)
                  return result;
            }
         } return nullptr;

         case RTTI::PoolTactic::Type: {
            // Hint is typed, so check in its typed pool chain first    
            result = Instance.FindInChain(memory, hint->GetPool<Pool>());
            if (result)
               return result;

            // Then check default pool chain                            
            // (pointer could be a member of default-pooled type)       
            result = Instance.FindInChain(memory, Instance.mMainPoolChain);
            if (result)
               return result;

            // Check all size pool chains                               
            // (pointer could be a member of a size-pooled type)        
            for (auto& sizepool : Instance.mSizePoolChain) {
               result = Instance.FindInChain(memory, sizepool);
               if (result)
                  return result;
            }

            // Finally, check all type pool chains                      
            // (pointer could be a member of a type-pooled type)        
            for (auto& typepool : Instance.mInstantiatedTypes) {
               if (typepool == hint)
                  continue;

               result = Instance.FindInChain(memory, typepool->GetPool<Pool>());
               if (result)
                  return result;
            }

         } return nullptr;

         case RTTI::PoolTactic::Main:
            break;
         }
      }

      // If reached, either no hint is provided, or PoolTactic::Main    
      // Check main pool chain                                          
      result = Instance.FindInChain(memory, Instance.mMainPoolChain);
      if (result)
         return result;

      // Check all size pool chains                                     
      // (pointer could be a member of a size-pooled type)              
      for (auto& sizepool : Instance.mSizePoolChain) {
         result = Instance.FindInChain(memory, sizepool);
         if (result)
            return result;
      }

      // Finally, check all type pool chains                            
      // (pointer could be a member of a type-pooled type)              
      for (auto& typepool : Instance.mInstantiatedTypes) {
         result = Instance.FindInChain(memory, typepool->GetPool<Pool>());
         if (result)
            return result;
      }

      // If reahced, then memory is guaranteed to not be ours           
      return nullptr;
   }

   /// Check if memory is owned by the memory manager                         
   /// Unlike Allocator::Find, this doesn't check if memory is currently used 
   /// but returns true, as long as the required pool is still available      
   ///   @attention assumes memory is a valid pointer                         
   ///   @param hint - the type of data to search for (optional)              
   ///   @param memory - memory pointer                                       
   ///   @return true if we own the memory                                    
   bool Allocator::CheckAuthority(DMeta hint, const void* memory) IF_UNSAFE(noexcept) {
      LANGULUS_ASSUME(DevAssumes, memory, "Nullptr provided");

      // Scan the last pool that found something (hot region)           
      //TODO consider a whole stack of those?
      if (Instance.mLastFoundPool) {
         const auto found = Instance.mLastFoundPool->Find(memory);
         if (found)
            return found;
      }

      // Decide pool chains, based on hint                              
      if (hint) {
         switch (hint->mPoolTactic) {
         case RTTI::PoolTactic::Size: {
            // Hint is sized, so check in size pool chain first         
            const auto sizebucket = Inner::FastLog2(hint->mSize);
            if (Instance.ContainedInChain(memory, Instance.mSizePoolChain[sizebucket]))
               return true;

            // Then check default pool chain                            
            // (pointer could be a member of default-pooled type)       
            if (Instance.ContainedInChain(memory, Instance.mMainPoolChain))
               return true;

            // Check all typed pool chains                              
            // (pointer could be a member of type-pooled type)          
            for (auto& type : Instance.mInstantiatedTypes) {
               if (Instance.ContainedInChain(memory, type->GetPool<Pool>()))
                  return true;
            }

            // Finally, check all other size pool chains                
            // (pointer could be a member of differently sized type)    
            for (Offset i = 0; i < sizebucket; ++i) {
               if (Instance.ContainedInChain(memory, Instance.mSizePoolChain[i]))
                  return true;
            }
            for (Offset i = sizebucket + 1; i < SizeBuckets; ++i) {
               if (Instance.ContainedInChain(memory, Instance.mSizePoolChain[i]))
                  return true;
            }
         } return false;

         case RTTI::PoolTactic::Type:
            // Hint is typed, so check in its typed pool chain first    
            if (Instance.ContainedInChain(memory, hint->GetPool<Pool>()))
               return true;

            // Then check default pool chain                            
            // (pointer could be a member of default-pooled type)       
            if (Instance.ContainedInChain(memory, Instance.mMainPoolChain))
               return true;

            // Check all size pool chains                               
            // (pointer could be a member of a size-pooled type)        
            for (auto& sizepool : Instance.mSizePoolChain) {
               if (Instance.ContainedInChain(memory, sizepool))
                  return true;
            }

            // Finally, check all type pool chains                      
            // (pointer could be a member of a type-pooled type)        
            for (auto& typepool : Instance.mInstantiatedTypes) {
               if (typepool == hint)
                  continue;

               if (Instance.ContainedInChain(memory, typepool->GetPool<Pool>()))
                  return true;
            }
            return false;

         case RTTI::PoolTactic::Main:
            break;
         }
      }

      // If reached, either no hint is provided, or PoolTactic::Main    
      // Check main pool chain                                          
      if (Instance.ContainedInChain(memory, Instance.mMainPoolChain))
         return true;

      // Check all size pool chains                                     
      // (pointer could be a member of a size-pooled type)              
      for (auto& sizepool : Instance.mSizePoolChain) {
         if (Instance.ContainedInChain(memory, sizepool))
            return true;
      }

      // Finally, check all type pool chains                            
      // (pointer could be a member of a type-pooled type)              
      for (auto& typepool : Instance.mInstantiatedTypes) {
         if (Instance.ContainedInChain(memory, typepool->GetPool<Pool>()))
            return true;
      }

      return false;
   }
   
#if LANGULUS_FEATURE(MEMORY_STATISTICS)
   bool Allocator::Statistics::operator == (const Statistics& rhs) const noexcept {
      return mBytesAllocatedByBackend == rhs.mBytesAllocatedByBackend
         and mBytesAllocatedByFrontend == rhs.mBytesAllocatedByFrontend
         and mEntries == rhs.mEntries
         and mPools == rhs.mPools
      #if LANGULUS_FEATURE(MANAGED_REFLECTION)
         and mDataDefinitions == rhs.mDataDefinitions
         and mTraitDefinitions == rhs.mTraitDefinitions
         and mVerbDefinitions == rhs.mVerbDefinitions
      #endif
      ;
   }

   /// Check for memory leaks, by retrieving the new memory manager state     
   /// and comparing it against this one                                      
   ///   @return true if no functional difference between the states          
   bool Allocator::State::Assert() {
      CollectGarbage();

      if (not IntegrityCheck()) {
         Logger::Error("Memory integrity check failure");
         return false;
      }

      if (mState.has_value()) {
         if (mState != GetStatistics()) {
            // Assertion failure                                        
            DumpPools();
            Diff(mState.value());
            mState = GetStatistics();
            ++Instance.mStatistics.mStep;
            Logger::Error("Memory state mismatch");
            return false;
         }
      }

      // All is fine                                                    
      mState = GetStatistics();
      ++Instance.mStatistics.mStep;
      return true;
   }
   
   /// Get allocator statistics                                               
   ///   @return a reference to the statistics structure                      
   const Allocator::Statistics& Allocator::GetStatistics() noexcept {
      return Instance.mStatistics;
   }

   /// Dump a single pool                                                     
   ///   @param id - pool id                                                  
   ///   @param pool - the pool to dump                                       
   void Allocator::DumpPool(Offset id, const Pool* pool) noexcept {
      const auto scope = Logger::InfoTab(
         Logger::PushCyan, Logger::Underline, "Pool #", id, " at ",
         fmt::format("{:x}", reinterpret_cast<Pointer>(pool)), Logger::Pop
      );

      Logger::Line("In use/reserved: ", 
         Logger::PushGreen, Size {pool->mAllocatedByFrontend}, Logger::Pop,
         '/',
         Logger::PushRed, Size {pool->mAllocatedByBackend}, Logger::Pop
      );

      Logger::Line("Min/Current/Max threshold: ",
         Logger::PushGreen, Size {pool->mThresholdMin}, Logger::Pop,
         '/',
         Logger::PushYellow, Size {pool->mThreshold}, Logger::Pop,
         '/',
         Logger::PushRed, Size {pool->mAllocatedByBackend}, Logger::Pop
      );

      if (pool->mMeta) {
         Logger::Line("Associated type: `",
            pool->mMeta->mCppName, "`, of size ", pool->mMeta->mSize);
      }

      if (pool->mEntries) {
         const auto escope = Logger::Section("Active entries: ",
            Logger::PushGreen, pool->mEntries, Logger::Pop
         );

         Count consecutiveEmpties = 0;
         Count ecounter = 0;
         do {
            const auto entry = pool->AllocationFromIndex(ecounter);
            if (entry->mReferences) {
               if (consecutiveEmpties) {
                  if (consecutiveEmpties == 1)
                     Logger::Line(Logger::Red, ecounter-1, "] ", "unused entry");
                  else
                     Logger::Line(Logger::Red, ecounter - consecutiveEmpties, '-', ecounter-1, "] ",
                        consecutiveEmpties, " unused entries");
                  consecutiveEmpties = 0;
               }

               Logger::Line(
                  Logger::Green, ecounter, "] ", Logger::Hex(entry), " ",
                  Size {entry->mAllocatedBytes}, ", ",
                  entry->mReferences, " references: `"
               );

               auto raw = entry->GetBlockStart();
               for (Offset i = 0; i < ::std::min(Offset {16}, entry->mAllocatedBytes); ++i) {
                  if (::isprint(raw[i].mValue))
                     Logger::Append(static_cast<char>(raw[i].mValue));
                  else
                     Logger::Append('?');
               }

               if (entry->mAllocatedBytes > 16)
                  Logger::Append("...`");
               else
                  Logger::Append('`');
            }
            else ++consecutiveEmpties;
         }
         while (++ecounter < pool->mEntries);

         if (consecutiveEmpties) {
            if (consecutiveEmpties == 1)
               Logger::Line(Logger::Red, ecounter-1, "] ", "unused entry");
            else
               Logger::Line(Logger::Red, ecounter - consecutiveEmpties, '-', ecounter-1, "] ",
                  consecutiveEmpties, " unused entries");
            consecutiveEmpties = 0;
         }
      }
   }

   /// Dump all currently allocated pools and entries, useful to locate leaks 
   void Allocator::DumpPools() noexcept {
      auto section = Logger::InfoTab("MANAGED MEMORY POOL DUMP");

      // Dump default pool chain                                        
      if (Instance.mMainPoolChain) {
         const auto scope = Logger::InfoTab(Logger::Purple, "MAIN POOL CHAIN: ");
         Count counter = 0;
         auto pool = Instance.mMainPoolChain;
         while (pool) {
            DumpPool(counter, pool);
            pool = pool->mNext;
            ++counter;
         }
      }

      // Dump every size pool chain                                     
      for (Offset size = 0; size < sizeof(Offset) * 8; ++size) {
         if (not Instance.mSizePoolChain[size])
            continue;

         const auto scope = Logger::InfoTab(Logger::Purple, 
            "SIZE POOL CHAIN FOR ", Logger::Red, Size {1 << size},
            Logger::Purple, ": "
         );

         Count counter = 0;
         auto pool = Instance.mSizePoolChain[size];
         while (pool) {
            DumpPool(counter, pool);
            pool = pool->mNext;
            ++counter;
         }
      }
      
      // Dump every type pool chain                                     
      for (auto type : Instance.mInstantiatedTypes) {
         auto pool = type->GetPool<Pool>();
         if (not pool)
            continue;

         #if LANGULUS_FEATURE(MANAGED_REFLECTION)
            const auto scope = Logger::InfoTab(Logger::Purple, 
               "TYPE POOL CHAIN FOR `", Logger::Red, type->mCppName, 
               Logger::Purple, "` (BOUNDARY: ", Logger::Red,
               type->mLibraryName, Logger::Purple, "): "
            );
         #else
            const auto scope = Logger::InfoTab(Logger::Purple, 
               "TYPE POOL CHAIN FOR `", Logger::Red, type->mCppName, 
               Logger::Purple, '`'
            );
         #endif

         Count counter = 0;
         while (pool) {
            DumpPool(counter, pool);
            pool = pool->mNext;
            ++counter;
         }
      }
   }

   /// Compare two statistics snapshots, and find the difference              
   void Allocator::Diff(const Statistics& with) noexcept {
      auto section = Logger::InfoTab("MANAGED MEMORY DIFF");
      auto& stats = Instance.mStatistics;

      if (stats.mBytesAllocatedByBackend != with.mBytesAllocatedByBackend) {
         Logger::Info(Logger::Purple,
            "Allocated byte difference: ",
            int(stats.mBytesAllocatedByBackend)
            - int(with.mBytesAllocatedByBackend));
      }

      if (stats.mBytesAllocatedByFrontend != with.mBytesAllocatedByFrontend) {
         Logger::Info(Logger::Purple,
            "Used byte difference: ",
            int(stats.mBytesAllocatedByFrontend)
            - int(with.mBytesAllocatedByFrontend));
      }

   #if LANGULUS_FEATURE(MANAGED_REFLECTION)
      if (stats.mDataDefinitions != with.mDataDefinitions) {
         const auto scope = Logger::InfoTab(Logger::Purple,
            "Data definitions difference: ",
            int(stats.mDataDefinitions) - int(with.mDataDefinitions)
         );
      }
   #endif

      if (stats.mPools != with.mPools) {
         const auto scope = Logger::InfoTab(Logger::Purple,
            "Pool difference: ", int(stats.mPools) - int(with.mPools)
         );

         // Diff default pool chain                                     
         if (Instance.mMainPoolChain) {
            Count counter = 0;
            auto pool = Instance.mMainPoolChain;
            while (pool) {
               if (pool->mStep > with.mStep) {
                  Logger::Info(Logger::Purple, "Default pool: ");
                  DumpPool(counter, pool);
               }
               pool = pool->mNext;
               ++counter;
            }
         }

         // Dump every size pool chain                                  
         for (Offset size = 0; size < sizeof(Offset) * 8; ++size) {
            if (not Instance.mSizePoolChain[size])
               continue;

            Count counter = 0;
            auto pool = Instance.mSizePoolChain[size];
            while (pool) {
               if (pool->mStep > with.mStep) {
                  Logger::Info(Logger::Purple, "Size ", Size {1 << size}, " pool: ");
                  DumpPool(counter, pool);
               }
               pool = pool->mNext;
               ++counter;
            }
         }

         // Dump every type pool chain                                  
         for (auto type : Instance.mInstantiatedTypes) {
            auto pool = type->GetPool<Pool>();
            if (not pool)
               continue;

            Count counter = 0;
            while (pool) {
               if (pool->mStep > with.mStep) {
                  Logger::Info(Logger::Purple, "Type ", type->mCppName, " pool: ");
                  #if LANGULUS_FEATURE(MANAGED_REFLECTION)
                     Logger::Info(Logger::Purple, "(Boundary: ", type->mLibraryName, ")");
                  #endif
                  DumpPool(counter, pool);
               }
               pool = pool->mNext;
               ++counter;
            }
         }
      }

      if (stats.mEntries != with.mEntries) {
         const auto scope = Logger::InfoTab(Logger::Purple,
            "Entries difference: ", int(stats.mEntries) - int(with.mEntries)
         );
      }

   #if LANGULUS_FEATURE(MANAGED_REFLECTION)
      if (stats.mTraitDefinitions != with.mTraitDefinitions) {
         const auto scope = Logger::InfoTab(Logger::Purple,
            "Trait definitions difference: ",
            int(stats.mTraitDefinitions) - int(with.mTraitDefinitions)
         );
      }

      if (stats.mVerbDefinitions != with.mVerbDefinitions) {
         const auto scope = Logger::InfoTab(Logger::Purple,
            "Verb definitions difference: ",
            int(stats.mVerbDefinitions) - int(with.mVerbDefinitions)
         );
      }
   #endif
   }

   /// Account for a newly allocated pool                                     
   ///   @param pool - the pool to account for                                
   void Allocator::Statistics::AddPool(const Pool* pool) noexcept {
      mBytesAllocatedByBackend += pool->GetTotalSize();
      mBytesAllocatedByFrontend += pool->GetAllocatedByFrontend();
      ++mPools;
      ++mEntries;
   }
   
   /// Account for a removed pool                                             
   ///   @param pool - the pool to account for                                
   void Allocator::Statistics::DelPool(const Pool* pool) noexcept {
      mBytesAllocatedByBackend -= pool->GetTotalSize();
      --mPools;
   }
   
   /// Integrity check a pool chain                                           
   ///   @param chainStart - [in/out] the start of the chain                  
   ///   @return true if all checks passed                                    
   bool Allocator::IntegrityCheckChain(const Pool* chainStart) {
      while (chainStart) {
         if (chainStart->IsInUse()) {
            Count validAllocations = 0;
            Count validBytes = 0;
            for (Count i = 0; i < chainStart->mEntries; ++i) {
               auto allocation = chainStart->AllocationFromIndex(i);
               if (allocation->mReferences) {
                  if (allocation->mReferences > 100000)
                     Logger::Warning("Suspicious reference count");

                  ++validAllocations;
                  validBytes += allocation->GetTotalSize();
               }
            }

            //TODO also check if negative memory space contains a predefined pattern,
            // in order to detect writing outside boundaries

            bool failure = false;
            if (validAllocations != chainStart->mValidEntries) {
               Logger::Error("Valid entry mismatch: found ",
                  validAllocations, " entries, but ",
                  chainStart->mValidEntries, " were actually registered"
               );
               failure = true;
            }

            if (validBytes != chainStart->mAllocatedByFrontend) {
               Logger::Error("Valid byte usage mismatch: found ",
                  validBytes, " bytes in use, but ",
                  chainStart->mAllocatedByFrontend, " were actually registered"
               );
               failure = true;
            }

            if (failure)
               return false;
         }

         chainStart = chainStart->mNext;
      }

      return true;
   }
   
   /// Integrity checks                                                       
   bool Allocator::IntegrityCheck() {
      // Integrity check the default chain                              
      if (Instance.mMainPoolChain) {
         Logger::Info("Integrity check: mMainPoolChain...");
         if (not Instance.IntegrityCheckChain(Instance.mMainPoolChain))
            return false;
      }

      // Integrity check all size chains                                
      int size = 1;
      for (auto& sizeChain : Instance.mSizePoolChain) {
         if (sizeChain) {
            Logger::Info("Integrity check: mSizePoolChain #", size++, "...");
            if (not Instance.IntegrityCheckChain(sizeChain))
               return false;
         }
      }
      
      // Integrity check all type chains                                
      for (auto& typeChain : Instance.mInstantiatedTypes) {
         auto& relevantPool = typeChain->GetPool<Pool>();
         if (relevantPool) {
            Logger::Info("Integrity check for type ", typeChain->mToken, "...");
            if (not Instance.IntegrityCheckChain(relevantPool))
               return false;
         }
      }

      return true;
   }
#endif

} // namespace Langulus::Fractalloc
