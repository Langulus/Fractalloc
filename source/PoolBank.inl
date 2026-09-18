///                                                                           
/// Langulus::Fractalloc                                                      
/// Copyright (c) 2015 Dimo Markov <team@langulus.com>                        
/// Part of the Langulus framework, see https://langulus.com                  
///                                                                           
/// SPDX-License-Identifier: MIT                                              
///                                                                           
#pragma once

namespace Langulus::Fractalloc
{   
   /// Helper structure for keeping track of free pool IDs                    
   struct PoolBank {
      using Pool = Langulus::Fractalloc::Pool;
      Pool* unindexed = nullptr;
      ::std::map<unsigned, Pool*> indexed;
      unsigned lastId = 0;
      unsigned freeIds = 0;

      /// Link a new pool, adding it to the unindexed chain, as well as       
      /// giving it a unique ID                                               
      void LinkPool(Pool* pool) {
         // Bring the new pool in front, so that next allocation is     
         // placed in it, as it is most likely still empty.             
         pool->mNext = unindexed;
         unindexed = pool;
         
         // Give the new pool a unique ID as well, so that packed       
         // pointers can utilize it.                                    
         for (unsigned reused = 1; reused < lastId and freeIds; ++reused) {
            // Always try reusing IDs                                   
            if (not indexed.contains(reused)) {
               pool->mID = reused;
               indexed[reused] = pool;
               --freeIds;
               return;
            }
         }

         pool->mID = ++lastId;
         indexed[lastId] = pool;
      }

      /// Unlink a pool (usually before destroying it), from the unindexed    
      /// chain, as well as the ID map                                        
      void UnlinkPool(Pool* pool) {
         if (unindexed == pool)
            unindexed = pool->mNext;

         if (lastId == pool->mID) {
            indexed.erase(pool->mID);
            if (not indexed.empty()) {
               const auto prevLastId = lastId;
               lastId = indexed.rbegin()->first;
               freeIds -= prevLastId - lastId;
            }
            else lastId = freeIds = 0;
            return;
         }

         indexed.erase(pool->mID);
         ++freeIds;
      }
      
      /// Trims and eventually deallocates all unused pools in a chain        
      ///   @param on_pool_deletion function to call on pool deletion         
      Pool* CollectGarbage(auto&& on_pool_deletion) {
         Pool* prev = nullptr;
         Pool* pool = unindexed;
         while (pool) {
            if (pool->IsInUse()) {
               // Pool is in use, just trim it and move on              
               pool->Trim();
               prev = pool;
               pool = pool->mNext;
               continue;
            }

            // If reached, the pool is not in use and is deleted        
            const auto next = pool->mNext;
            LglsVerbose(
               "Fractalloc: ", Logger::DarkCyan, "Typed pool ", Logger::Hex(pool),
               " of size ", Logger::Size {static_cast<size_t>(pool->GetAllocatedByBackend())},
               " was deallocated"
            );
            
            UnlinkPool(pool);            
            on_pool_deletion(pool);
            pool = next;
            if (prev)
               prev->mNext = pool;
         }
         
         return unindexed;
      }

   #if LANGULUS_FEATURE(MEMORY_STATISTICS)      
      /// Dump a single pool                                                  
      ///   @param type pool type                                             
      ///   @param id pool id                                                 
      ///   @param pool the pool to dump                                      
      static void DumpPool(DMeta type, size_t id, const Pool* pool) noexcept {
         const auto scope = Logger::InfoSection(
            Logger::PushCyan, "Pool #", id, " at ",
            Logger::Hex(pool), Logger::Pop
         );

         Logger::Line("In use/reserved: ", 
            Logger::PushGreen, Logger::Size {pool->mAllocatedByFrontend}, Logger::Pop,
            '/',
            Logger::PushRed, Logger::Size {static_cast<size_t>(pool->mAllocatedByBackend)}, Logger::Pop
         );

         Logger::Line("Min/Current/Max threshold: ",
            Logger::PushGreen, Logger::Size {static_cast<size_t>(pool->mThresholdMin)}, Logger::Pop,
            '/',
            Logger::PushYellow, Logger::Size {static_cast<size_t>(pool->mThresholdMax)}, Logger::Pop,
            '/',
            Logger::PushRed, Logger::Size {static_cast<size_t>(pool->mAllocatedByBackend)}, Logger::Pop
         );

         if (type) {
            Logger::Line("Associated type: `",
               type.GetCppName(), "`, of size ", type.GetSize());
         }

         if (pool->mNextEntry) {
            const auto escope = Logger::Section("Entries: ",
               Logger::PushGreen, pool->mValidEntries, " (of max reached ", pool->mNextEntry, ")", Logger::Pop
            );

            size_t consecutiveEmpties = 0;
            size_t ecounter = 0;
            do {
               const auto entry = pool->AllocationFromIndex(ecounter);
               if (entry->GetUses()) {
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
                     Logger::Size {static_cast<size_t>(entry->GetSize())}, ", ",
                     entry->GetUses(), " references: `"
                  );

                  auto raw = entry->GetBlockStart();
                  for (size_t i = 0; i < ::std::min(size_t {16}, static_cast<size_t>(entry->GetSize())); ++i) {
                     if (::isprint(raw[i]))
                        Logger::Append(static_cast<char>(raw[i]));
                     else
                        Logger::Append('?');
                  }

                  if (entry->GetSize() > 16u)
                     Logger::Append("...`");
                  else
                     Logger::Append('`');
               }
               else ++consecutiveEmpties;
            }
            while (++ecounter < pool->mNextEntry);

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

      void DumpPools(DMeta type) const {
         #if LANGULUS_FEATURE(MANAGED_REFLECTION)
            if (type) {
               Logger::Append(" (boundaries: ");
               if (type.GetBoundaries().empty())
                  Logger::Append("MAIN");
               else for (auto& boundary : type.GetBoundaries())
                  Logger::Append(boundary, ' ');
               Logger::Append("): ");
            }
         #endif

         for (auto& val : indexed | std::views::values)
            DumpPool(type, val->mID, val);
      }

      void DiffPools(const Statistics& with, DMeta type) const {
         for (auto& val : indexed | std::views::values) {
            if (val->mStep <= with.mStep)
               continue;

         #if LANGULUS_FEATURE(MANAGED_REFLECTION)
            Logger::Append(" (boundaries: ");
            if (type.GetBoundaries().empty())
               Logger::Append("MAIN");
            else for (auto& boundary : type.GetBoundaries())
               Logger::Append(boundary, ' ');
            Logger::Append(')');
         #endif
            
            DumpPool(type, val->mID, val);
         }
      }
         
      /// Integrity check a pool chain                                        
      ///   @return true if all checks passed                                 
      bool IntegrityCheckChain() {
         auto pool = unindexed;
         while (pool) {
            if (pool->IsInUse()) {
               size_t validAllocations = 0;
               size_t validBytes = 0;
               for (size_t i = 0; i < pool->mNextEntry; ++i) {
                  auto allocation = pool->AllocationFromIndex(i);
                  if (allocation->GetUses()) {
                     if (allocation->GetUses() > 100000) {
                        Logger::Warning(
                           "Fractalloc: Suspicious reference count in allocation ",
                           Logger::Hex(allocation), " of size ", allocation->GetSize(),
                           " in pool ", Logger::Hex(pool), ", entry ", i, "/", pool->mNextEntry
                        );
                     }

                     ++validAllocations;
                     validBytes += static_cast<size_t>(allocation->GetSize());
                  }
               }

               //TODO also check if negative memory space contains a predefined pattern,
               // in order to detect writing outside boundaries

               bool failure = false;
               if (validAllocations != pool->mValidEntries) {
                  Logger::Error("Fractalloc: Valid entry mismatch: found ",
                     validAllocations, " entries, but ",
                     pool->mValidEntries, " were actually registered in pool ",
                     Logger::Hex(pool)
                  );
                  failure = true;
               }

               if (validBytes != pool->mAllocatedByFrontend) {
                  Logger::Error("Fractalloc: Valid byte usage mismatch: found ",
                     validBytes, " bytes in use, but ",
                     pool->mAllocatedByFrontend, " were actually registered in pool ",
                     Logger::Hex(pool)
                  );
                  failure = true;
               }

               if (failure)
                  return false;
            }

            pool = pool->mNext;
         }

         return true;
      }
   #endif
   };
}
