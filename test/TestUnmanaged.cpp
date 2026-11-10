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

#if LANGULUS_FEATURE(MANAGED_MEMORY)
   #error "This file shouldn't be included if MANAGED_MEMORY is enabled"
#endif


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
   IF_SAFE(REQUIRE_THROWS(Allocator::Allocate(pot_t(alignof(TestType)), 511_pot)));
   constexpr size_t testAlignment = ::std::max(alignof(TestType), Alignment);
   
   GIVEN("A small allocation") {
      pot_t s = 1_pot;
      SUBCASE("") { s =   2_pot; }
      SUBCASE("") { s = 512_pot; }
      CAPTURE(s);

      Allocation* entry = Allocator::Allocate(pot_t(alignof(TestType)), s);
      REQUIRE(entry);

      WHEN("Memory is allocated on the heap") {
         REQUIRE(entry->GetBlockStart() == Align(reinterpret_cast<uint8_t*>(entry) + sizeof(Allocation), testAlignment));
         REQUIRE(IsAligned(entry, alignof(Allocation)));
         REQUIRE(IsAligned(entry->GetBlockStart(), testAlignment));
         REQUIRE(entry->GetSize() == pot_t(Align(static_cast<size_t>(s), testAlignment)));
         REQUIRE(entry->GetUses() == 1);

         size_t matches = 0;
         size_t mismatches = 0;
         for (size_t i = 0; i < s; ++i) {
            if (entry->Contains(entry->GetBlockStart() + i))
               ++matches;
            if (not entry->Contains(entry->GetBlockStart() - (i+1)))
               ++mismatches;
         }
         REQUIRE(matches == s);
         REQUIRE(mismatches == s);

         REQUIRE_FALSE(entry->Contains(entry->GetBlockStart() + Align(static_cast<size_t>(s), testAlignment)));

         Allocator::Deallocate(entry);
      }

      WHEN("Referenced once") {
         entry->AddRef(1);

         REQUIRE(entry->GetUses() == 2);
         IF_SAFE(REQUIRE_THROWS(Allocator::Deallocate(entry)));
         IF_SAFE(REQUIRE(entry->GetUses() == 2));
         
         entry->AddRef(-1);
         Allocator::Deallocate(entry);
      }

      WHEN("Referenced multiple times") {
         entry->AddRef(5);

         REQUIRE(entry->GetUses() == 6);

         IF_SAFE(REQUIRE_THROWS(Allocator::Deallocate(entry)));
         IF_SAFE(REQUIRE(entry->GetUses() == 6));

         entry->AddRef(-5);
         Allocator::Deallocate(entry);
      }

      WHEN("Dereferenced once without deletion") {
         entry->AddRef(1);
         entry->AddRef(-1);

         REQUIRE(entry->GetUses() == 1);

         Allocator::Deallocate(entry);
      }

      WHEN("Dereferenced multiple times without deletion") {
         entry->AddRef(5);
         entry->AddRef(-4);

         REQUIRE(entry->GetUses() == 2);

         IF_SAFE(REQUIRE_THROWS(Allocator::Deallocate(entry)));
         IF_SAFE(REQUIRE(entry->GetUses() == 2));

         entry->AddRef(-1);
         Allocator::Deallocate(entry);
      }

      WHEN("Dereferenced once with deletion") {
         Allocator::Deallocate(entry);
      }

      WHEN("Dereferenced multiple times with deletion") {
         entry->AddRef(5);

         IF_SAFE(REQUIRE_THROWS(Allocator::Deallocate(entry)));
         IF_SAFE(REQUIRE(entry->GetUses() == 6));

         entry->AddRef(-5);
         Allocator::Deallocate(entry);
      }
   }
   
   GIVEN("A large allocation") {
      auto s = 4096_pot*1024_pot;
      Allocation* entry = Allocator::Allocate(pot_t(alignof(TestType)), s);
      REQUIRE(entry);

      WHEN("Memory is allocated on the heap") {
         REQUIRE(entry->GetBlockStart() == Align(reinterpret_cast<uint8_t*>(entry) + sizeof(Allocation), testAlignment));
         REQUIRE(IsAligned(entry, alignof(Allocation)));
         REQUIRE(IsAligned(entry->GetBlockStart(), alignof(TestType)));
         REQUIRE(entry->GetSize() == pot_t(Align(static_cast<size_t>(s), testAlignment)));
         REQUIRE(entry->GetUses() == 1);
      }

      Allocator::Deallocate(entry);
   }
}

TEST_CASE("Stress test and benchmarking") {
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
   for (int i = 0; i < 1'000'000; ++i) {
      auto random_type = types[generator() % types.size()];
      auto random_size = pot_t(Roof2(random_type.GetSize() * (generator() % 1000)));
      auto random_alignment = random_type.GetAlignment();
      Allocation* entry;
      {
         CTRACK_NAME_PERSIST("Test/Unmanaged::Allocate");
         entry = Allocator::Allocate(random_alignment, random_size);
      }

      REQUIRE(entry);

      {
         CTRACK_NAME_PERSIST("Test/Unmanaged::Deallocate");
         Allocator::Deallocate(entry);
      }
   }

   // Perform a million random allocations using malloc, for comparison 
   for (int i = 0; i < 1'000'000; ++i) {
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
   for (int i = 0; i < 1'000'000; ++i) {
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

   #if LANGULUS(BENCHMARK)
      auto benchmark = ctrack::result_get_detail_table();
      REQUIRE(benchmark.check_highscore());
      // Unfortunately, there's always overhead due to prepending an Allocation structure for managing references.
      // Hopefully, the capability to reference memory blocks of containers, instead of always allocating new ones will be faster overall.
      // In other words: allocations are slower, but less frequently used by Anyness containers.
      //REQUIRE(benchmark.check_same("Test/Unmanaged::Allocate", "Test/aligned_malloc"));
      //REQUIRE(benchmark.check_same("Test/Unmanaged::Deallocate", "Test/aligned_free"));
   #endif
}