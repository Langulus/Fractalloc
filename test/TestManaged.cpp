///                                                                           
/// Langulus::Fractalloc                                                      
/// Copyright (c) 2015 Dimo Markov <team@langulus.com>                        
/// Part of the Langulus framework, see https://langulus.com                  
///                                                                           
/// SPDX-License-Identifier: MIT                                              
///                                                                           
#include "TestAllocatorCommon.hpp"
#include <Langulus/MetaOf.hpp>
#include <Langulus/Profiler.hpp>
#include <Langulus/CT/MinAlloc.hpp>
#include <Langulus/CT/Pooled.hpp>
#include <random>

#if not LANGULUS_FEATURE(MANAGED_MEMORY)
   #error "This file shouldn't be included if MANAGED_MEMORY is disabled"
#endif


SCENARIO("Testing FastLog2 calls") {
   const size_t numbers[]{
      0, 1, 2, 3, 4, 5, 6, 11, 16, 64, 99, 120, 128
   };
   const size_t results[]{
      0, 0, 1, 1, 2, 2, 2,  3,  4,  6,  6,   6,   7
   };
   static_assert(sizeof(numbers) == sizeof(results), "Oops");

   for (unsigned i = 0; i < sizeof(numbers) / sizeof(size_t); ++i) {
      REQUIRE(Fractalloc::FastLog2(numbers[i]) == results[i]);
   }
}

TEST_CASE_TEMPLATE("Testing pool functions", TestType,
   TypeVeryBigAligned,
   TypeVeryBigPacked,
   Type1,
   Type2,
   Type3,
   Type4,
   Type8,
   TypeBig,
   TypeVeryBig
) {
   static MemoryState memoryState;
   
   using Fractalloc::Pool;
   const auto meta = MetaDataOf<TestType>();
   constexpr size_t default_size = MinAllocOf<TestType>();
   constexpr size_t min_alloc = MinAllocOf<TestType>();
   constexpr size_t testAlignment = alignof(TestType);

   REQUIRE(meta);
   IF_SAFE(REQUIRE_THROWS(Allocator::AllocatePool(meta, 0_pot)));
   IF_SAFE(REQUIRE_THROWS(Allocator::AllocatePool(meta, 3_pot)));

   GIVEN("A default-sized pool") {
      Pool* pool = Allocator::AllocatePool(meta, pot_t(default_size));
      REQUIRE(pool);

      const auto allocData = pool->GetAllocationData();
      const auto origin = pool->GetClientData();
      const auto smallest = static_cast<size_t>(pool->GetMinAllocation());
      const auto full = static_cast<size_t>(pool->GetAllocatedByBackend());
      const auto half = full / 2;
      const auto quarter = half / 2;

      REQUIRE(smallest == min_alloc);
      REQUIRE(IsAligned(pool->GetClientData(), testAlignment));
      REQUIRE(pool->GetAllocatedByBackend() == default_size);
      REQUIRE(pool->AllocationFromIndex(0) == allocData);
      REQUIRE(pool->AllocationFromIndex(1) == allocData + half/smallest);
      REQUIRE(pool->AllocationFromIndex(2) == allocData + quarter/smallest);
      REQUIRE(pool->AllocationFromIndex(3) == allocData + (quarter + half)/smallest);
      REQUIRE(pool->ThresholdFromIndex(0) == full);
      REQUIRE(pool->ThresholdFromIndex(1) == half);
      REQUIRE(pool->ThresholdFromIndex(2) == quarter);
      REQUIRE(pool->ThresholdFromIndex(3) == quarter);
      REQUIRE(pool->ThresholdFromIndex(4) == quarter / 2);
      REQUIRE(pool->ThresholdFromIndex(5) == quarter / 2);
      REQUIRE(pool->ThresholdFromIndex(6) == quarter / 2);
      REQUIRE(pool->ThresholdFromIndex(7) == quarter / 2);
      REQUIRE(pool->ThresholdFromIndex(8) == quarter / 4);
      REQUIRE(pool->ThresholdFromIndex(pool->GetMaxEntries() - 1u) == smallest);
      REQUIRE(pool->ThresholdFromIndex(pool->GetMaxEntries() - 0u) == smallest / 2);
      REQUIRE(pool->CanContain(1_pot));
      REQUIRE(pool->CanContain(pot_t(Alignment)));
      REQUIRE(pool->CanContain(pot_t(smallest)));
      REQUIRE(pool->CanContain(pot_t(half)));
      REQUIRE(pool->CanContain(pot_t(full)));
      REQUIRE(pool->GetAllocatedByFrontend() == 0);
      REQUIRE(pool->GetMaxEntries() == full / smallest);
      REQUIRE(pool->ContainsData(origin));
      REQUIRE(pool->ContainsData(origin + half));
      REQUIRE(pool->ContainsData(origin + half * 2 - 1));
      REQUIRE_FALSE(pool->ContainsData(origin + half * 2));
      REQUIRE_FALSE(pool->ContainsData(nullptr));
      REQUIRE_FALSE(pool->IsInUse());

      auto is_in_range = [pool](Allocation const* a) {
         if (a < pool->GetAllocationData() or a >= pool->GetAllocationData() + static_cast<size_t>(pool->GetMaxEntries()))
            return false;

         if (a == pool->GetAllocationData())
            return pool->GetCurrentEntries() > 0;

         const size_t i = a - pool->GetAllocationData();
         size_t i_clear_lsb = i & ~(i - 1u);
         size_t index = ((pool->GetMaxEntries() + i) / i_clear_lsb - 1u) >> 1u;
         return index < pool->GetCurrentEntries();
      };

      WHEN("Small entry is allocated") {
         auto entry = pool->Allocate(pot_t(Roof2(sizeof(TestType))));

         REQUIRE(pool->GetAllocatedByFrontend() == entry->GetSize());
         REQUIRE(pool->GetMaxEntries() == full / smallest);
         REQUIRE(pool->ContainsAllocation(entry));
         REQUIRE(pool->ContainsData(entry->GetBlockStart()));
         REQUIRE(pool->IsInUse());
      }

      WHEN("Filled with all possible small entries") {
         // Fill up                                                     
         Logger::Special("Filling up pool...");
         for (size_t i = 0; i < pool->GetMaxEntries(); ++i) {
            auto entry = pool->Allocate(pot_t(Roof2(sizeof(TestType))));
            REQUIRE(entry);
            REQUIRE(entry->GetSize() == min_alloc);
            REQUIRE(IsAligned(entry->GetBlockStart(), testAlignment));
            REQUIRE(pool->IndexFromAllocation(entry) == i);
            REQUIRE(pool->IndexFromAddress(entry->GetBlockStart()) == i);
            REQUIRE(is_in_range(entry));
            if (i < (pool->GetMaxEntries() >> 1u))
               REQUIRE(not is_in_range(entry + 1));
            else
               REQUIRE(not is_in_range(entry + 2));

            entry->AddRef(i);

            // Fill the entire entry to check for heap corruptions      
            for (size_t i2 = 0; i2 < entry->GetSize(); ++i2) {
               entry->GetBlockStart()[i2] = 66;
            }
         }

         // Fail to add more                                            
         Logger::Special("Overflowing pool...");
         for (int i = 0; i < 5; ++i) {
            auto entry = pool->Allocate(1_pot);
            REQUIRE(entry == nullptr);
         }

         REQUIRE(pool->GetAllocatedByFrontend() == pool->GetAllocatedByBackend());
         REQUIRE(pool->GetAllocatedByFrontend() == static_cast<size_t>(pool->GetMaxEntries()) * min_alloc);
         REQUIRE(pool->GetMaxEntries() == full / smallest);
         REQUIRE(pool->AllocationFromIndex(0)->GetBlockStart() == origin);
         REQUIRE(pool->AllocationFromIndex(1)->GetBlockStart() == origin + half);
         REQUIRE(pool->AllocationFromIndex(2)->GetBlockStart() == origin + quarter);
         REQUIRE(pool->AllocationFromIndex(3)->GetBlockStart() == origin + quarter + half);
         REQUIRE(pool->AllocationFromIndex(static_cast<size_t>(pool->GetMaxEntries()) - 1)->GetBlockStart() == origin + half + half - min_alloc);

         Logger::Special("Checking integrity of pool...");
         for (size_t i = 0; i < pool->GetMaxEntries(); ++i) {
            auto entry = pool->AllocationFromIndex(i);
            REQUIRE(pool->ContainsAllocation(entry));
            REQUIRE(pool->ContainsData(entry->GetBlockStart()));
            REQUIRE(entry->GetUses() == static_cast<int32_t>(1 + i));

            for (size_t i2 = 0; i2 < entry->GetSize(); ++i2) {
               REQUIRE(entry->GetBlockStart()[i2] == 66);
            }
         }

         // Deallocate N random entries                                 
         Logger::Special("Deallocating random entries in pool...");
         Allocation* prev_entry = nullptr;
         for (size_t i = 0; i < pool->GetMaxEntries(); i += 20u) {
            auto entry = pool->AllocationFromIndex(i);
            REQUIRE(entry->GetUses() == static_cast<int32_t>(1 + i));
            REQUIRE(pool->ContainsAllocation(entry));
            REQUIRE(pool->ContainsData(entry->GetBlockStart()));
            pool->Deallocate(entry);
            REQUIRE(entry->GetUses() == 0);
            REQUIRE(entry->GetNextFreeEntry() == prev_entry);
            prev_entry = entry;
         }
         REQUIRE(pool->CanContain(pool->GetMinAllocation()));
         REQUIRE_FALSE(pool->CanContain(pot_t(pool->GetMinAllocation()*2u)));

         // Test the integrity of the free entry chain                  
         Logger::Special("Testing free chain integrity...");
         prev_entry = pool->GetLastFreedEntry();
         size_t chain_counter = 0;
         while (prev_entry) {
            REQUIRE(prev_entry->GetUses() == 0);
            ++chain_counter;
            prev_entry = prev_entry->GetNextFreeEntry();
         }
         REQUIRE(chain_counter == pool->GetCurrentEntries() - pool->GetValidEntries());

         // Deallocate right half of entries                            
         Logger::Special("Deallocating right half of entries in pool...");
         for (auto entry = pool->GetAllocationData() + pool->GetMaxEntries()/1u - 1; entry >= pool->GetAllocationData() + pool->GetMaxEntries()/2u; --entry) {
            if (not entry->GetUses())
               continue;
            pool->Deallocate(entry);
            REQUIRE(entry->GetUses() == 0);
         }
         REQUIRE(pool->CanContain(pot_t(pool->GetMinAllocation()*2u)));

         // Test the integrity of the free entry chain                  
         Logger::Special("Testing free chain integrity...");
         prev_entry = pool->GetLastFreedEntry();
         chain_counter = 0;
         while (prev_entry) {
            REQUIRE(prev_entry->GetUses() == 0);
            ++chain_counter;
            prev_entry = prev_entry->GetNextFreeEntry();
         }
         REQUIRE(chain_counter == pool->GetCurrentEntries() - pool->GetValidEntries());

         // Deallocate more entries to enforce shrinking                
         Logger::Special("Deallocate more entries to enforce shrinking of pool...");
         for (size_t i = 16; i < static_cast<size_t>(pool->GetMaxEntries()); ++i) {
            auto entry = pool->AllocationFromIndex(i);
            if (entry->GetUses() == 0)
               continue;
            pool->Deallocate(entry);
         }
         REQUIRE(pool->CanContain(pot_t(pool->GetMinAllocation()*2u)));

         // Allocate a new one, should reuse prev_entry                 
         Logger::Special("Allocating a new entry...");
         prev_entry = pool->GetLastFreedEntry();
         auto new_entry = pool->Allocate(pot_t(pool->GetMinAllocation()*2u));
         REQUIRE(new_entry);
         REQUIRE(new_entry == prev_entry);
      }

      WHEN("An entry larger than the minimum is allocated") {
         auto entry = pool->Allocate(pot_t(min_alloc * 2));
         REQUIRE(entry);

         REQUIRE(pool->GetAllocatedByFrontend() == entry->GetSize());
         REQUIRE(pool->GetMaxEntries() == pool->GetAllocatedByBackend() / smallest);
         REQUIRE(pool->ContainsAllocation(entry));
         REQUIRE(pool->ContainsData(entry->GetBlockStart()));
         REQUIRE(pool->IsInUse());
      }

      WHEN("An entry larger than the pool itself is allocated") {
         auto entry = pool->Allocate(pot_t(default_size * 2));

         REQUIRE(entry == nullptr);
         REQUIRE(pool->GetAllocatedByFrontend() == 0);
         REQUIRE_FALSE(pool->IsInUse());
      }

      Allocator::DeallocatePool(pool);
   }
   
   GIVEN("A custom huge pool") {
      Pool* pool = Allocator::AllocatePool(meta, pot_t(default_size * 256));
      REQUIRE(pool);

      auto entry = pool->Allocate(1_pot);
      const auto full = pool->GetAllocatedByBackend();
      const auto smallest = pool->GetMinAllocation();

      REQUIRE(pool->GetAllocatedByFrontend() == entry->GetSize());
      REQUIRE(pool->GetMaxEntries() == full / smallest);
      REQUIRE(pool->ContainsAllocation(entry));
      REQUIRE(pool->ContainsData(entry->GetBlockStart()));
      REQUIRE(pool->IsInUse());

      Allocator::DeallocatePool(pool);
   }

   REQUIRE(memoryState.Assert());
   REQUIRE_FALSE(Allocator::CollectGarbage());
}

TEST_CASE_TEMPLATE("Testing allocator functions", TestType,
   Type1,
   Type2,
   Type3,
   Type4,
   Type8,
   TypeBig,
   TypeVeryBig,
   TypeVeryBigAligned,
   TypeVeryBigPacked
) {
   static MemoryState memoryState;

   const auto meta = MetaDataOf<TestType>();
   IF_SAFE(REQUIRE_THROWS(Allocator::Allocate(meta, 511_pot)));
   constexpr size_t testAlignment = alignof(TestType);
   constexpr size_t min_alloc = MinAllocOf<TestType>();

   GIVEN("A small allocation") {
      pot_t s = pot_t(Roof2(sizeof(TestType)));
      SUBCASE("") { s = pot_t(Roof2(sizeof(TestType)*2 )); }
      SUBCASE("") { s = pot_t(Roof2(sizeof(TestType)*16)); }
      CAPTURE(s);

      auto rounded_s = ::std::max(static_cast<size_t>(s), min_alloc);
      Allocation* entry = Allocator::Allocate(meta, s);
      REQUIRE(entry);

      WHEN("Memory is allocated on the heap") {
         REQUIRE(IsAligned(entry, alignof(Allocation)));
         REQUIRE(IsAligned(entry->GetBlockStart(), testAlignment));
         REQUIRE(entry->GetSize() == rounded_s);
         REQUIRE(entry->GetUses() == 1);

         size_t matches = 0;
         size_t mismatches = 0;
         for (size_t i = 0; i < rounded_s; ++i) {
            if (entry->Contains(entry->GetBlockStart() + i))
               ++matches;
            if (not entry->Contains(entry->GetBlockStart() - (i+1)))
               ++mismatches;
         }
         REQUIRE(matches == rounded_s);
         REQUIRE(mismatches == rounded_s);
         REQUIRE_FALSE(entry->Contains(entry->GetBlockStart() + rounded_s + 1));
      }

      WHEN("Referenced once") {
         entry->AddRef(1);

         REQUIRE(entry->GetUses() == 2);
         REQUIRE(Allocator::CheckAuthority(entry->GetBlockStart()));
         REQUIRE_FALSE(Allocator::CheckAuthority(entry));
         REQUIRE(Allocator::Find(entry->GetBlockStart()));
         REQUIRE_FALSE(Allocator::Find(entry));

         IF_SAFE(REQUIRE_THROWS(Allocator::Deallocate(entry)));
         entry->AddRef(-1);
      }

      WHEN("Referenced multiple times") {
         entry->AddRef(5);

         REQUIRE(entry->GetUses() == 6);
         IF_SAFE(REQUIRE_THROWS(Allocator::Deallocate(entry)));
         entry->AddRef(-5);
      }

      WHEN("Dereferenced once without deletion") {
         entry->AddRef(1);
         entry->AddRef(-1);

         REQUIRE(entry->GetUses() == 1);
      }

      WHEN("Dereferenced multiple times without deletion") {
         entry->AddRef(5);
         entry->AddRef(-4);

         REQUIRE(entry->GetUses() == 2);
         IF_SAFE(REQUIRE_THROWS(Allocator::Deallocate(entry)));
         entry->AddRef(-1);
      }

      const auto blockStart = entry->GetBlockStart();
      Allocator::Deallocate(entry);

      REQUIRE_FALSE(Allocator::CheckAuthority(entry));
      REQUIRE(Allocator::CheckAuthority(blockStart));
      REQUIRE_FALSE(Allocator::Find(blockStart));
      REQUIRE_FALSE(Allocator::Find(entry));
   }
   
   GIVEN("A large allocation") {
      pot_t s = 1024_pot * 1024_pot;
      Allocation* entry = Allocator::Allocate(meta, s);
      REQUIRE(entry);

      WHEN("Memory is allocated on the heap") {
         REQUIRE(IsAligned(entry, alignof(Allocation)));
         REQUIRE(IsAligned(entry->GetBlockStart(), alignof(TestType)));
         REQUIRE(entry->GetSize() == s);
         REQUIRE(entry->GetUses() == 1);
      }

      Allocator::Deallocate(entry);
   }

   REQUIRE(memoryState.Assert());
   REQUIRE_FALSE(Allocator::CollectGarbage());
}

TEST_CASE("Memory stress test and benchmarking") {
   static MemoryState memoryState;
   std::random_device rd;
   std::mt19937 generator(rd());

   const std::array types {
        MetaDataOf<Type1>()
      , MetaDataOf<Type2>()
      , MetaDataOf<Type3>()
      , MetaDataOf<Type4>()
      , MetaDataOf<Type8>()
      , MetaDataOf<TypeBig>()
      , MetaDataOf<TypeVeryBig>()
      , MetaDataOf<TypeVeryBigAligned>()
      , MetaDataOf<TypeVeryBigPacked>()
   };

   // Perform a million random allocations using the memory manager
   for (int i = 0; i < 500'000; ++i) {
      auto random_type = types[generator() % types.size()];
      auto random_size = pot_t(Roof2(random_type.GetSize() * (generator() % 1000)));
      Allocation* entry;
      {
         CTRACK_NAME_PERSIST("Test/Fractalloc::Allocate");
         entry = Allocator::Allocate(random_type, random_size);
      }

      REQUIRE(entry);

      {
         CTRACK_NAME_PERSIST("Test/Fractalloc::Deallocate");
         Allocator::Deallocate(entry);
      }
   }

   #if LANGULUS(BENCHMARK)
   // Perform a million random allocations using malloc, for comparison
   for (int i = 0; i < 500'000; ++i) {
      auto random_type = types[generator() % types.size()];
      auto random_size = Roof2(random_type.GetSize() * (generator() % 1000));
      void* entry;
      {
         CTRACK_NAME("Test/malloc");
         entry = malloc(random_size);
      }

      REQUIRE(entry);

      {
         CTRACK_NAME("Test/free");
         free(entry);
      }
   }

   // Perform a million random allocations using aligned_malloc, for comparison
   for (int i = 0; i < 500'000; ++i) {
      auto random_type = types[generator() % types.size()];
      auto random_size = Roof2(random_type.GetSize() * (generator() % 1000));
      auto random_alignment = static_cast<size_t>(random_type.GetAlignment());
      void* entry;
      {
         CTRACK_NAME("Test/aligned_malloc");
         #if LANGULUS_COMPILER(MSVC) or LANGULUS_COMPILER(CLANG_CL)
            entry = _aligned_malloc(random_size, random_alignment);
         #else
            entry = ::std::aligned_alloc(random_size, random_alignment);
         #endif
      }

      REQUIRE(entry);

      {
         CTRACK_NAME("Test/aligned_free");
         #if LANGULUS_COMPILER(MSVC) or LANGULUS_COMPILER(CLANG_CL)
            _aligned_free(entry);
         #else
            ::std::free(entry);
         #endif
      }
   }

      auto benchmark = ctrack::result_get_detail_table();
      REQUIRE(benchmark.check_highscore());
      REQUIRE(benchmark.check_faster("Test/Fractalloc::Allocate", "Test/aligned_malloc"));
      REQUIRE(benchmark.check_faster("Test/Fractalloc::Deallocate", "Test/aligned_free"));
   #endif

   REQUIRE(memoryState.Assert());
   REQUIRE_FALSE(Allocator::CollectGarbage());
}

TEST_CASE("Memory stress test and benchmarking (accumulator)") {
   static MemoryState memoryState;
   std::random_device rd;
   std::mt19937 generator(rd());

   const std::array types {
        MetaDataOf<Type1>()
      , MetaDataOf<Type2>()
      , MetaDataOf<Type3>()
      , MetaDataOf<Type4>()
      , MetaDataOf<Type8>()
      , MetaDataOf<TypeBig>()
      , MetaDataOf<TypeVeryBig>()
      , MetaDataOf<TypeVeryBigAligned>()
      , MetaDataOf<TypeVeryBigPacked>()
   };

   constexpr size_t budget = Langulus::Bitness >= 64 ? 500'000 : 150'000;

   {
      // Perform a million random allocations using the memory manager  
      ::std::unordered_set<Allocation*> mask;
      mask.reserve(budget);

      ::std::vector<Allocation*> entries;
      entries.reserve(budget);

      for (size_t i = 0; i < budget/3; ++i) {
         // Allocate 2 entries, and then deallocate one random          
         for (int k = 0; k < 2; ++k) {
            auto random_type = types[generator() % types.size()];
            auto random_size = pot_t(Roof2(random_type.GetSize() * (generator() % 100)));
            Allocation* entry;
            {
               CTRACK_NAME_PERSIST("Test/Accumulator/Fractalloc::Allocate");
               entry = Allocator::Allocate(random_type, random_size);
            }

            REQUIRE(entry);
            if (mask.contains(entry)) {
               Logger::Error("Entry with index ",
                  entry->GetPool()->IndexFromAllocation(entry), " shouldn't be reused");
               FAIL("");
            }

            mask.insert(entry);
            entries.push_back(entry);
         }

         auto random_deletion = generator() % entries.size();
         auto& e = entries[random_deletion];
         if (e) {
            REQUIRE(mask.contains(e));
            {
               CTRACK_NAME_PERSIST("Test/Accumulator/Fractalloc::Deallocate");
               Allocator::Deallocate(e);
            }

            mask.erase(e);
            e = nullptr;
         }
      }

      for (auto& e : entries) {
         if (e) {
            CTRACK_NAME_PERSIST("Test/Accumulator/Fractalloc::Deallocate");
            Allocator::Deallocate(e);
         }
      }
   }

   #if LANGULUS(BENCHMARK)
   {
      // Perform a million random allocations using the malloc          
      ::std::vector<void*> entries;
      entries.reserve(budget);

      for (int i = 0; i < budget/3; ++i) {
         // Allocate 2 entries, and then deallocate one random          
         for (int k = 0; k < 2; ++k) {
            auto random_type = types[generator() % types.size()];
            auto random_size = Roof2(random_type.GetSize() * (generator() % 100));
            void* entry;
            {
               CTRACK_NAME("Test/Accumulator/malloc");
               entry = malloc(random_size);
            }

            REQUIRE(entry);
            entries.push_back(entry);
         }

         auto random_deletion = generator() % entries.size();
         auto& e = entries[random_deletion];
         if (e) {
            CTRACK_NAME("Test/Accumulator/free");
            free(e);
         }
         e = nullptr;
      }

      for (auto& e : entries) {
         if (e) {
            CTRACK_NAME("Test/Accumulator/free");
            free(e);
         }
      }
   }

   {
      // Perform a million random allocations using the aligned_alloc   
      ::std::vector<void*> entries;
      entries.reserve(budget);

      for (int i = 0; i < budget/3; ++i) {
         // Allocate 2 entries, and then deallocate one random          
         for (int k = 0; k < 2; ++k) {
            auto random_type = types[generator() % types.size()];
            auto random_size = Roof2(random_type.GetSize() * (generator() % 100));
            auto random_alignment = static_cast<size_t>(random_type.GetAlignment());
            void* entry;
            {
               CTRACK_NAME("Test/Accumulator/aligned_malloc");
               #if LANGULUS_COMPILER(MSVC) or LANGULUS_COMPILER(CLANG_CL)
                  entry = _aligned_malloc(random_size, random_alignment);
               #else
                  entry = ::std::aligned_alloc(random_size, random_alignment);
               #endif
            }

            REQUIRE(entry);
            entries.push_back(entry);
         }

         auto random_deletion = generator() % entries.size();
         auto& e = entries[random_deletion];
         if (e) {
            CTRACK_NAME("Test/Accumulator/aligned_free");
            #if LANGULUS_COMPILER(MSVC) or LANGULUS_COMPILER(CLANG_CL)
               _aligned_free(e);
            #else
               ::std::free(e);
            #endif
         }
         e = nullptr;
      }

      for (auto& e : entries) {
         if (e) {
            CTRACK_NAME("Test/Accumulator/aligned_free");
            #if LANGULUS_COMPILER(MSVC) or LANGULUS_COMPILER(CLANG_CL)
               _aligned_free(e);
            #else
               ::std::free(e);
            #endif
         }
      }
   }

   auto benchmark = ctrack::result_get_detail_table();
   REQUIRE(benchmark.check_highscore());
   REQUIRE(benchmark.check_faster("Test/Accumulator/Fractalloc::Allocate", "Test/Accumulator/aligned_malloc"));
   REQUIRE(benchmark.check_faster("Test/Accumulator/Fractalloc::Deallocate", "Test/Accumulator/aligned_free"));
   #endif

   REQUIRE(memoryState.Assert());
   REQUIRE_FALSE(Allocator::CollectGarbage());
}
