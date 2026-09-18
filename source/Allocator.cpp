///                                                                           
/// Langulus::Fractalloc                                                      
/// Copyright (c) 2015 Dimo Markov <team@langulus.com>                        
/// Part of the Langulus framework, see https://langulus.com                  
///                                                                           
/// SPDX-License-Identifier: MIT                                              
///                                                                           
#include <Langulus/Fractalloc/Allocator.hpp>
#include <unordered_map>
#include <map>
#include <ranges>

#if 0
   #include <Langulus/Logger/EnableVerbose.hpp>
#else
   #include <Langulus/Logger/NoVerbose.hpp>
#endif

#include "PoolBank.inl"


namespace
{
   #if LANGULUS_FEATURE(MEMORY_STATISTICS)
      /// Memory statistics                                                   
      Langulus::Fractalloc::Statistics gStatistics {};
   #endif

   using Langulus::Fractalloc::Pool;
   using Langulus::Fractalloc::PoolBank;
   using Langulus::RTTI::DMeta;

   /// Default pool chain.                                                    
   /// Won't accept types that have size or alignment larger than Alignment.  
   PoolBank gMainPoolChain {};
   
   /// The last succesfull Find() result in default pool chain                
   Pool const* gLastFoundPool {};

   /// Pool chains for types that use PoolTactic::Size and have alignment     
   /// smaller or equal to Langulus::Alignment.                               
   constexpr size_t SizeBuckets = sizeof(size_t) * 8;
   PoolBank gSizePoolChain[SizeBuckets] {};
   
   /// The set of types that are currently in use and their corresponding     
   /// pool chains. Used to detect if a shared object is safe to be unloaded. 
   /// Also used to pack/unpack pointers.                                     
   ::std::unordered_map<DMeta, PoolBank> gTypePoolChain;

   /// The set of all pools. Used to quickly determine if a pointer resides   
   /// inside the memory manager.                                             
   ::std::unordered_map<uintptr_t, Pool*> gPools;

   /// Used to mask pointers in order to determine whether memory belongs to  
   /// us or not. Updated on each new allocated pool.                         
   uintptr_t gPossiblePoolMemorySpace = 0;

   PoolBank* SelectPoolBank(DMeta meta) assumptious {
      LglsAssumeDevAndOptimize((bool) meta, "Invalid meta data");
      switch (meta.GetPoolTactic()) {
      case Langulus::PoolTactic::Size:
         return &gSizePoolChain[Langulus::Fractalloc::FastLog2(meta.GetSize())];
      case Langulus::PoolTactic::Type:
         return &gTypePoolChain[meta];
      case Langulus::PoolTactic::Main:
         return &gMainPoolChain;
      }
      return nullptr;
   }
}

namespace Langulus::Fractalloc
{   
   /// Each pool allocation has the following structure:                      
   /// [pool data][alignment][allocation data][alignment][client bytes...]    
   ///   @param type the pooled type                                          
   ///   @param size the number of client bytes to allocate                   
   ///   @return a newly allocated memory that is correctly aligned           
   Pool* AlignedAllocate(const DMeta& type, pot_t size) assumptious {
      LglsAssumeDev((bool) type,
         "Invalid type");
      LglsAssumeDev(size >= type.GetSize(),
         "Pool can't contain a single instance of provided type");
      
      const pot_t  data_align = type.GetAlignment();
      const pot_t  data_minal = type.GetMinAllocation();
      const size_t size_int = static_cast<size_t>(size);
      const size_t pool_cost = Pool::Cost(data_align, data_minal, size);
      const size_t pool_alignment = ::std::bit_ceil(pool_cost);
      const size_t backendSize = pool_cost + size_int;
      #if LANGULUS_COMPILER(MSVC) or LANGULUS_COMPILER(CLANG_CL)
         const auto pool = _aligned_malloc(backendSize, pool_alignment);
      #else
         const auto pool = ::std::aligned_alloc(pool_alignment, backendSize);
      #endif

      if (not pool)
         return nullptr;

      new (pool) Pool {data_align, data_minal, pot_t(pool_alignment), size};
      auto typed_pool = static_cast<Pool*>(pool);

      // Add all pointer masks that point to the pool                   
      auto ptr = reinterpret_cast<uintptr_t>(pool);
      const auto ptrEnd = reinterpret_cast<uintptr_t>(typed_pool->GetClientData())
                        + static_cast<uintptr_t>(typed_pool->GetAllocatedByBackend());
      const auto ptrStep = ptr & -ptr;
      gPools[ptr] = typed_pool;
      gPossiblePoolMemorySpace |= ptr;
      
      while (ptr + ptrStep < ptrEnd) {
         ptr += ptrStep;
         gPools[ptr] = typed_pool;
         gPossiblePoolMemorySpace |= ptr;
      }      
      
      return typed_pool;
   }

   /// Allocate a memory entry                                                
   ///   @attention doesn't call any constructors                             
   ///   @attention doesn't throw - check if return is nullptr                
   ///   @attention assumes meta data is valid                                
   ///   @param meta meta data for finding the proper pool                    
   ///   @param size the number of bytes to allocate                          
   ///   @return the allocation, or nullptr if out of memory                  
   auto Allocator::Allocate(DMeta meta, pot_t size) assumptious -> Allocation* {
      // Decide pool chain based on meta data                           
      auto pool_bank = SelectPoolBank(meta);
      LglsAssumeDevAndOptimize(pool_bank, "Pool bank should always be valid");
      
      //	Attempt to place allocation in the chosen chain                
      unsigned pool_misses = 0;
      Allocation* entry = nullptr;
      auto pool = pool_bank->unindexed;
      while (pool) {
         entry = pool->Allocate(size);
         if (entry)
            break;
         pool = pool->mNext;
         ++pool_misses;
      }

      if (entry) {
         // We're done                                                  
         #if LANGULUS_FEATURE(MEMORY_STATISTICS)
            gStatistics.mEntries += 1;
            gStatistics.mBytesAllocatedByFrontend += static_cast<size_t>(entry->GetSize());
            LglsAssumeDev(
               gStatistics.mBytesAllocatedByFrontend <= gStatistics.mBytesAllocatedByBackend,
               "Impossible amount of frontend allocation"
            );
         #endif
         LglsVerbose(
            "Fractalloc: ", Logger::Green, "Allocation ", Logger::Hex(entry),
            " was allocated with ", Logger::Size {static_cast<size_t>(entry->GetSize())},
            " (associated type `", meta.GetName(), "`)"
         );
         return entry;
      }

      //                                                                
      // If reached, chosen pool chain can't contain the memory.        
      // Allocate a new pool and add it at the front of the chain.      
      // Make new pool bigger, depending on how many pool misses we had.
      // Many pool misses indicate that type is hot and used often.     
      const pot_t new_pool_size = meta.GetMinPoolsize() << (pool_misses / 2);
      pool = AllocatePool(meta, size < new_pool_size ? new_pool_size : size);
      if (not pool)
         return nullptr;

      LglsVerbose(
         "Fractalloc: ", Logger::Cyan, "New pool ", Logger::Hex(pool),
         " of size ", Logger::Size {static_cast<size_t>(pool->GetAllocatedByBackend())}
      );

      // Place allocation in the new pool. This is guaranteed to work.  
      entry = pool->Allocate(size);
      LglsVerbose(
         "Fractalloc: ", Logger::Green, "Allocation ", Logger::Hex(entry),
         " was allocated with ", Logger::Size {static_cast<size_t>(entry->GetSize())},
         " (associated type `", meta.GetName(), "`)"
      );

      // Time to update the pool chain with the new pool.               
      pool_bank->LinkPool(pool);
      IF_LANGULUS_MEMORY_STATISTICS(gStatistics.AddPool(pool));
      return entry;
   }
   
   /// Reallocate a memory entry                                              
   ///   @attention never calls any constructors                              
   ///   @attention never copies any data                                     
   ///   @attention never deallocates previous entry                          
   ///   @attention returned entry might be different from the previous       
   ///   @attention doesn't throw - check if return is nullptr                
   ///   @param type the type of the allocation                               
   ///   @param size the number of bytes to allocate                          
   ///   @param previous the previous memory entry                            
   ///   @return the reallocated memory entry, or nullptr if out of memory    
   auto Allocator::Reallocate(DMeta type, pot_t size, Allocation* previous)
   assumptious -> Allocation* {
      LglsAssumeDevAndOptimize(previous,
         "Reallocating nullptr");
      LglsAssumeDev(size != previous->GetSize(),
         "Reallocation suboptimal - size is same as previous");
      LglsAssumeDevAndOptimize(previous->mReferences,
         "Reallocating an unused allocation");
      LglsAssumeDevAndOptimize(previous->mReferences == 1,
         "Reallocating allocation used from multiple places");

      // New size is bigger, precautions must be taken                  
      [[maybe_unused]] const auto oldSize = static_cast<size_t>(previous->GetSize());
      auto pool = const_cast<Pool*>(previous->GetPool());
      if (pool->Reallocate(previous, size)) {
         #if LANGULUS_FEATURE(MEMORY_STATISTICS)
            auto& stats = gStatistics;
            stats.mBytesAllocatedByFrontend -= oldSize;
            stats.mBytesAllocatedByFrontend += static_cast<size_t>(previous->GetSize());
            LglsAssumeDev(
               stats.mBytesAllocatedByFrontend <= stats.mBytesAllocatedByBackend,
               "Impossible amount of frontend allocation"
            );
         #endif

         LglsVerbose(
            "Fractalloc: ", Logger::Yellow, "Allocation ", Logger::Hex(previous),
            " was reallocated from ", Logger::Size {oldSize}, " to ",
            Logger::Size {static_cast<size_t>(previous->GetSize())},
            " (associated type `", type.GetName(), "`)"
         );
         return previous;
      }

      // If this is reached we have a collision, so new entry is made   
      return Allocate(type, size);
   }
   
   /// Deallocate a memory allocation                                         
   ///   @attention assumes entry is a valid entry under jurisdiction         
   ///   @attention doesn't call any destructors                              
   ///   @param entry the memory entry to deallocate                          
   void Allocator::Deallocate(Allocation* entry) assumptious {
      LglsAssumeDevAndOptimize(entry,
         "Deallocating nullptr");
      LglsAssumeDevAndOptimize(entry->mReferences,
         "Deallocating an unused allocation");
      LglsAssumeDevAndOptimize(entry->mReferences == 1,
         "Deallocating an allocation used from multiple places");

      [[maybe_unused]] const auto backupSize = static_cast<size_t>(entry->GetSize());
      LglsVerbose(
         "Fractalloc: ", Logger::Red, "Allocation ", Logger::Hex(entry),
         " of size ", Logger::Size {backupSize}, " was deallocated (had ",
         entry->mReferences, " references)"
      );

      auto pool = const_cast<Pool*>(entry->GetPool());
      pool->Deallocate(entry);

      #if LANGULUS_FEATURE(MEMORY_STATISTICS)
         auto& stats = gStatistics;
         stats.mBytesAllocatedByFrontend -= backupSize;
         stats.mEntries -= 1;
      #endif
   }

   /// Allocate a pool of custom size                                         
   ///   @attention the pool must be deallocated with DeallocatePool          
   ///   @param type meta data to associate pool with                         
   ///   @param size the client requested size of the pool (in bytes)         
   ///   @return a pointer to the new pool                                    
   Pool* Allocator::AllocatePool(DMeta type, pot_t size) assumptious {
      return AlignedAllocate(type, size);
   }

   /// Deallocate a pool                                                      
   ///   @attention doesn't call any destructors                              
   ///   @attention entries inside are no longer valid after this             
   ///   @attention assumes pool is a valid pointer                           
   ///   @param pool the pool to deallocate                                   
   void Allocator::DeallocatePool(Pool* pool) assumptious {
      LglsAssumeDevAndOptimize(pool, "Nullptr provided");
      if (gLastFoundPool == pool)
         gLastFoundPool = nullptr;

      // Remove all pointer masks that map to the pool                  
      auto ptr = reinterpret_cast<uintptr_t>(pool);
      const auto ptrEnd = reinterpret_cast<uintptr_t>(pool->GetClientData())
                        + static_cast<uintptr_t>(pool->GetAllocatedByBackend());
      const auto ptrStep = ptr & -ptr;
      gPools.erase(ptr);
      while (ptr + ptrStep < ptrEnd) {
         ptr += ptrStep;
         gPools.erase(ptr);
      }

      #if LANGULUS_COMPILER(MSVC) or LANGULUS_COMPILER(CLANG_CL)
         _aligned_free(pool);
      #else
         ::std::free(pool);
      #endif
   }
   
   /// Deallocates all unused pools                                           
   ///   @return true if there's at least one pool remaining allocated        
   bool Allocator::CollectGarbage() {
      const auto on_pool_deletion = [](Pool* pool) {
         IF_LANGULUS_MEMORY_STATISTICS(gStatistics.DelPool(pool));
         DeallocatePool(pool);
      };
      
      bool result = false;
      gLastFoundPool = nullptr;

      // Cleanup the main chain                                         
      if (gMainPoolChain.CollectGarbage(on_pool_deletion))
         result = true;

      // Cleanup all size chains                                        
      for (auto& sizeChain : gSizePoolChain) {
         if (sizeChain.CollectGarbage(on_pool_deletion))
            result = true;
      }

      // Cleanup all type chains                                        
      for (auto t = gTypePoolChain.begin(); t != gTypePoolChain.end();) {
         if (not t->second.CollectGarbage(on_pool_deletion))
            t = gTypePoolChain.erase(t);
         else
            ++t;
      }

      return result or not gTypePoolChain.empty();
   }
   
#if LANGULUS_FEATURE(MANAGED_REFLECTION)
   /// Check RTTI boundary for allocated pools.                               
   /// Useful to decide when shared library is no longer used and is ready    
   /// to be unloaded. Best used after a call to CollectGarbage.              
   ///   @param boundary the boundary name                                    
   ///   @return the number of pools                                          
   size_t Allocator::CheckBoundary(const Token& boundary) noexcept {
      const ::std::string b {boundary};
      size_t count = 0;
      for (const auto& type : gTypePoolChain) {
         if (type.first.GetBoundaries().contains(b))
            count += type.second.indexed.size();
      }
      return count;
   }
#endif

   /// Find a memory entry from pointer.                                      
   /// Allows us to safely interface unknown memory, possibly reusing it.     
   ///   @param memory memory pointer                                         
   ///   @attention assumes memory is a valid pointer                         
   ///   @return the memory entry that contains the memory pointer, or        
   ///      nullptr if memory is not ours, or entry is not in use             
   auto Allocator::Find(const void* memory) assumptious -> const Allocation* {
      LglsAssumeDevAndOptimize(memory, "Nullptr provided");
      
      // Check the last pool that found something (hot region)          
      if (gLastFoundPool) {
         if (auto found = gLastFoundPool->Find(memory))
            return found;
      }

      // Mask out the pointer in order to locate the owning pool        
      uintptr_t test = reinterpret_cast<uintptr_t>(memory) & gPossiblePoolMemorySpace;
      while (test) {
         auto found = gPools.find(test);
         if (found != gPools.end()) {
            gLastFoundPool = found->second;
            return found->second->Find(memory);
         }

         // Continue shrinking the mask until pointer becomes zero      
         test &= ~(-test);                // Flips only the lowest bit  
      }

      // If reached, then memory is out of jurisdiction                 
      return nullptr;
   }

   /// Check if memory is owned by the memory manager.                        
   /// Unlike Allocator::Find, this doesn't check if memory is currently used 
   /// but returns true, as long as the required pool is still available.     
   ///   @attention assumes memory is a valid pointer                         
   ///   @param memory memory pointer                                         
   ///   @return true if we own the memory                                    
   bool Allocator::CheckAuthority(const void* memory) assumptious {
      LglsAssumeDevAndOptimize(memory, "Nullptr provided");

      // Check the last pool that found something (hot region)          
      if (gLastFoundPool and gLastFoundPool->ContainsData(memory))
         return true;
      
      // Mask out the pointer in order to locate the owning pool        
      uintptr_t test = reinterpret_cast<uintptr_t>(memory) & gPossiblePoolMemorySpace;
      while (test) {
         auto found = gPools.find(test);
         if (found != gPools.end()) {
            gLastFoundPool = found->second;
            return found->second->ContainsData(memory);
         }

         // Continue shrinking the mask until pointer becomes zero      
         test &= ~(-test);                // Flips only the lowest bit  
      }

      // If reached, then memory is out of jurisdiction                 
      return false;
   }

   /// Get a memory entry using IDs, usually coming from packed pointers.     
   ///   @attention assumes 'meta' and 'poolId' are valid                     
   ///   @param meta the type of the data                                     
   ///   @param poolId the pool id                                            
   ///   @param entryId the entry id                                          
   ///   @return the corresponding allocation                                 
   auto Allocator::GetAllocation(
      DMeta meta, size_t poolId, size_t entryId
   ) assumptious -> Allocation* {
      LglsAssumeDevAndOptimize(poolId, "Nullptr provided");
      auto pool_bank = SelectPoolBank(meta);
      LglsAssumeDevAndOptimize(pool_bank, "Missing pool");
      LglsAssumeDev(pool_bank->indexed.contains(poolId), "Missing poolId");
      auto pool = pool_bank->indexed.at(poolId);
      LglsAssumeDevAndOptimize(pool, "Missing pool");
      auto entry = pool->AllocationFromIndex(entryId);
      LglsAssumeDev(entry, "Missing entry");
      return entry;
   }

   /// Allocate while conforming to packed pointer limits                     
   ///   @param spec pointer specification                                    
   ///   @param meta data type of the allocation                              
   ///   @param size size of the allocation in bytes                          
   auto Allocator::AllocatePackedInner(
      PointerSpecification const& spec, DMeta meta, pot_t size
   ) assumptious -> Allocation* {
      if (not spec.IsPacked())
         return Allocate(meta, size);

      // Decide pool chain based on meta data                           
      auto pool_bank = SelectPoolBank(meta);
      LglsAssumeDevAndOptimize(pool_bank, "Pool bank should always be valid");

      //	Attempt to place allocation in the chosen chain                
      LglsAssumeDevAndOptimize(pool_bank, "Pool bank should always be valid");
      const size_t max_pool_budget = (1u << spec.PoolBits) - 1u;
      const size_t max_entry_budget = (1u << spec.EntryBits) - 1u;
      size_t pool_misses = 1;
      Allocation* entry = nullptr;
      for (auto& p : pool_bank->indexed) {
         entry = p.second->AllocatePacked(max_entry_budget, size);
         if (entry or p.first > max_pool_budget)
            break;
         ++pool_misses;
      }

      if (entry) {
         // We're done                                                  
         #if LANGULUS_FEATURE(MEMORY_STATISTICS)
            gStatistics.mEntries += 1;
            gStatistics.mBytesAllocatedByFrontend += static_cast<size_t>(entry->GetSize());
            LglsAssumeDev(
               gStatistics.mBytesAllocatedByFrontend <= gStatistics.mBytesAllocatedByBackend,
               "Impossible amount of frontend allocation"
            );
         #endif
         return entry;
      }

      //                                                                
      // If reached, chosen pool chain can't contain the memory.        
      // Allocate a new pool and add it at the front of the chain.      
      if (pool_misses >= max_pool_budget) {
         // We've gone beyond the possible pool ID - request denied     
         return nullptr;
      }

      // Maximize pool size to utilize full entry budget                
      const pot_t max_entry_id = pot_t(1u << spec.EntryBits);
      const pot_t pool_align = ::std::max(meta.GetAlignment(), pot_t(Alignment));
      const pot_t pool_threshold_min = ::std::max(meta.GetMinAllocation(), pool_align);      
      const pot_t new_pool_size = max_entry_id * pool_threshold_min;
      auto new_pool = AllocatePool(meta, size < new_pool_size ? new_pool_size : size);
      if (not new_pool)
         return nullptr;

      LglsVerbose(
         "Fractalloc: ", Logger::Cyan, "New pool ", Logger::Hex(new_pool),
         " of size ", Logger::Size {static_cast<size_t>(new_pool->GetAllocatedByBackend())}
      );

      // Place allocation in the new pool. This is guaranteed to work.  
      entry = new_pool->AllocatePacked(max_entry_budget, size);

      // Time to update the pool chain with the new pool.               
      pool_bank->LinkPool(new_pool);
      IF_LANGULUS_MEMORY_STATISTICS(gStatistics.AddPool(new_pool));
      return entry;
   }
      
   /// Reallocate while conforming to packed pointer limits                   
   ///   @param spec pointer specification                                    
   ///   @param type data type of the allocation                              
   ///   @param size size of the allocation in bytes                          
   ///   @param previous the previous allocation                              
   auto Allocator::ReallocatePackedInner(
      PointerSpecification const& spec, 
      DMeta type, pot_t size, Allocation* previous
   ) assumptious -> Allocation* {
      if (not spec.IsPacked())
         return Reallocate(type, size, previous);

      LglsAssumeDevAndOptimize(previous,
         "Reallocating nullptr");
      LglsAssumeDev(size != previous->GetSize(),
         "Reallocation suboptimal - size is same as previous");
      LglsAssumeDevAndOptimize(previous->mReferences,
         "Reallocating an unused allocation");
      LglsAssumeDevAndOptimize(previous->mReferences == 1,
         "Reallocating allocation used from multiple places");

      // New size is bigger, precautions must be taken                  
      [[maybe_unused]] const auto oldSize = static_cast<size_t>(previous->GetSize());
      auto pool = const_cast<Pool*>(previous->GetPool());
      if (pool->Reallocate(previous, size)) {
         #if LANGULUS_FEATURE(MEMORY_STATISTICS)
            auto& stats = gStatistics;
            stats.mBytesAllocatedByFrontend -= oldSize;
            stats.mBytesAllocatedByFrontend += static_cast<size_t>(previous->GetSize());
            LglsAssumeDev(
               stats.mBytesAllocatedByFrontend <= stats.mBytesAllocatedByBackend,
               "Impossible amount of frontend allocation"
            );
         #endif

         LglsVerbose(
            "Fractalloc: ", Logger::Yellow, "Allocation ", Logger::Hex(previous),
            " was reallocated from ", Logger::Size {oldSize}, " to ",
            Logger::Size {static_cast<size_t>(previous->GetSize())}
         );
         return previous;
      }

      // If this is reached we have a collision, so new entry is made   
      return AllocatePackedInner(spec, type, size);
   }

   /// Unpack a packed pointer                                                
   ///   @param spec pointer specification                                    
   ///   @param deptr_type type the 'packed' points to (T* points to T)       
   ///   @param packed the pointer, packed inside, but not necessarily filling
   ///      an entire uintptr_t                                               
   ///   @return the unpacked raw pointer                                     
   void* Allocator::UnpackPointer(
      PointerSpecification const& spec,
      DMeta deptr_type, uintptr_t packed
   ) assumptious {
      if (not spec.IsPacked())
         return reinterpret_cast<void*>(packed);

      auto e = FindPackedPointer(spec, deptr_type, packed);
      const size_t elementId = packed & ((1u << spec.OffsetBits) - 1u);
      return e->GetBlockStart() + deptr_type.GetSize() * elementId;
   }
   

   /// Find the allocation where a packed pointer resides in                  
   ///   @param spec pointer specification                                    
   ///   @param deptr_type type the 'packed' points to (T* points to T)       
   ///   @param packed the pointer, packed inside, but not necessarily filling
   ///      an entire uintptr_t                                               
   ///   @return the corresponding allocation                                 
   auto Allocator::FindPackedPointer(
      PointerSpecification const& spec,
      DMeta deptr_type, uintptr_t packed
   ) assumptious -> Allocation const* {
      // Decide pool chain based on meta data                           
      if (not packed)
         return nullptr;
      if (not spec.IsPacked())
         return Find(reinterpret_cast<void*>(packed));
      
      auto pool_bank = SelectPoolBank(deptr_type);
      LglsAssumeDevAndOptimize(pool_bank, "Pool bank should always be valid");

      // Unpack indices and return raw pointer                          
      const size_t poolId = packed >> (spec.EntryBits + spec.OffsetBits);
      LglsAssumeDev(pool_bank->indexed.contains(poolId), "Invalid pool id");
      const size_t entryId = (packed >> spec.OffsetBits) & ((1u << spec.EntryBits) - 1u);
      return pool_bank->indexed.at(poolId)->AllocationFromIndex(entryId);
   }
   
#if LANGULUS_FEATURE(MEMORY_STATISTICS)
   /// Get allocator statistics                                               
   ///   @return a reference to the statistics structure                      
   auto Allocator::GetStatistics() noexcept -> const Statistics& {
      return gStatistics;
   }

   /// Dump all currently allocated pools and entries, useful to locate leaks 
   void Allocator::DumpPools() noexcept {
      auto section = Logger::InfoSection("MANAGED MEMORY POOL DUMP");

      // Dump main pool chain                                           
      if (gMainPoolChain.unindexed) {
         const auto scope = Logger::InfoSection(Logger::Purple, "MAIN POOL CHAIN: ");
         gMainPoolChain.DumpPools({});
      }

      // Dump every valid size pool chain                               
      for (auto& sized : gSizePoolChain) {
         if (not sized.unindexed)
            continue;

         const auto scope = Logger::InfoSection(Logger::Purple, 
            "SIZE POOL CHAIN FOR ", Logger::Red, Logger::Size {1ul << (&sized - gSizePoolChain)},
            Logger::Purple, ": "
         );

         sized.DumpPools({});
      }
      
      // Dump every type pool chain                                     
      for (auto& type : gTypePoolChain) {
         const auto scope = Logger::InfoSection(Logger::Purple,
            "TYPE POOL CHAIN FOR `", Logger::Red, type.first.GetCppName(),
            Logger::Purple, '`'
         );

         type.second.DumpPools(type.first);
      }
   }

   /// Compare two statistics snapshots, and find the difference              
   ///   @param with previous state                                           
   void Allocator::Diff(const Statistics& with) noexcept {
      auto section = Logger::InfoSection("MANAGED MEMORY DIFF");
      auto& stats = GetStatistics();

      if (stats.mBytesAllocatedByBackend != with.mBytesAllocatedByBackend) {
         Logger::Info(Logger::Purple,
            "Allocated byte difference: ",
            static_cast<int>(stats.mBytesAllocatedByBackend) - static_cast<int>(with.mBytesAllocatedByBackend));
      }

      if (stats.mBytesAllocatedByFrontend != with.mBytesAllocatedByFrontend) {
         Logger::Info(Logger::Purple,
            "Used byte difference: ",
            static_cast<int>(stats.mBytesAllocatedByFrontend) - static_cast<int>(with.mBytesAllocatedByFrontend));
      }

      if (stats.mEntries != with.mEntries) {
         Logger::Info(Logger::Purple,
            "Entries difference: ", static_cast<int>(stats.mEntries) - static_cast<int>(with.mEntries)
         );
      }

   #if LANGULUS_FEATURE(MANAGED_REFLECTION)
      if (stats.mDataDefinitions != with.mDataDefinitions) {
         Logger::Info(Logger::Purple,
            "Data definitions difference: ",
            static_cast<int>(stats.mDataDefinitions) - static_cast<int>(with.mDataDefinitions)
         );
      }

      if (stats.mTraitDefinitions != with.mTraitDefinitions) {
         Logger::Info(Logger::Purple,
            "Trait definitions difference: ",
            static_cast<int>(stats.mTraitDefinitions) - static_cast<int>(with.mTraitDefinitions)
         );
      }

      if (stats.mVerbDefinitions != with.mVerbDefinitions) {
         Logger::Info(Logger::Purple,
            "Verb definitions difference: ",
            static_cast<int>(stats.mVerbDefinitions) - static_cast<int>(with.mVerbDefinitions)
         );
      }
   #endif

      if (stats.mPools != with.mPools) {
         Logger::Info(Logger::Purple,
            "Pool difference: ", static_cast<int>(stats.mPools) - static_cast<int>(with.mPools)
         );
         
         gMainPoolChain.DiffPools(with, {});

         for (auto& sized : gSizePoolChain)
            sized.DiffPools(with, {});

         for (auto& type : gTypePoolChain)
            type.second.DiffPools(with, type.first);
      }
   }   
   
   /// Integrity checks                                                       
   ///   @return true if no memory errors occured                             
   bool Allocator::IntegrityCheck() {
      if (not gMainPoolChain.IntegrityCheckChain())
         return false;

      for (auto& sized : gSizePoolChain) {
         if (not sized.IntegrityCheckChain())
            return false;
      }

      for (auto& type : gTypePoolChain) {
         if (not type.second.IntegrityCheckChain())
            return false;
      }

      return true;
   }
#endif
}
