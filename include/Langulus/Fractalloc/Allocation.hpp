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
#include "Pool.hpp"

#if not LANGULUS_FEATURE(MANAGED_MEMORY)
   #error "This file shouldn't be included if MANAGED_MEMORY is disabled"
#endif


namespace Langulus::Fractalloc
{   
   ///                                                                        
   /// A single, continuous memory allocation. Produced and managed by Pool.  
   struct Allocation {
   private:      
      // The number of references to this memory.                       
      // Most often used, so first for immediate access.                
      int32_t mReferences = 1;

      // This has two states depending on mReferences:                  
      // If mReferences > 0, the struct is used;                        
      // If mReferences == 0, mNextFreeEntryFinder is used.             
      union {
         struct {
            #if LANGULUS_FEATURE(MEMORY_STATISTICS)
               // Acts like a timestamp of when the allocation happened 
               uint64_t mStep;
            #endif
            
            // Used to find the pool pointer by rounding 'this'.        
            // Represented as a bit number.                             
            uint8_t mPoolAlignment;
            // Allocated bytes usable by client.                        
            // Represented as a bit number.                             
            uint8_t mSize;
         };
         // Used when mReferences == 0 to chain free entries            
         int32_t mNextFreeEntryFinder;
      };

   public:
      Allocation() = delete;
      Allocation(const Allocation&) = delete;
      Allocation(Allocation&&) = delete;

      /// Initialize an allocation                                            
      ///   @param size the number of allocated bytes                         
      ///   @param pool_alignment the pool alignment                          
      Allocation(pot_t size, pot_t pool_alignment) noexcept {
         mPoolAlignment = pool_alignment.bit;
         mSize = size.bit;
      }
      
      /// Get the number of references                                        
      auto GetUses() const noexcept -> int32_t {
         return mReferences;
      }
      
      /// Reference the entry 'c' times. Use negative 'c' to dereference.     
      ///   @param c the number of references to add                          
      void AddRef(int32_t c) assumptious {
         LglsAssumeDev(c > 0 or mReferences >= -c,
            "Removing too many references");
         LglsAssumeDev(c < 0 or mReferences > 0,
            "Resurrecting a disused allocation");
         mReferences += c;
      }
      
      /// Get the user bytes                                                  
      ///   @return the byte size of the usable client memory region          
      auto GetSize() const assumptious -> pot_t {
         LglsAssumeDev(mReferences != 0,
            "Can't get size if entry isn't in use");
         pot_t result; result.bit = mSize;
         return result;
      }
      
      /// Return the aligned start of usable block memory                     
      auto GetBlockStart() const assumptious -> uint8_t* {
         LglsAssumeDev(mReferences != 0,
            "Can't get block start if entry isn't in use");

         // Return a conventional pointer                               
         const auto pool = GetPool();
         const size_t offset = this - pool->GetAllocationData();
         return pool->GetClientData() + pool->GetMinAllocation() * offset;
      }

      /// Return the aligned start of usable block memory, packed to some     
      /// specification.                                                      
      ///   @attention use this only if the allocation was produced using     
      ///      Allocator::AllocatePacked or Allocator::ReallocatePacked!      
      ///      Otherwise IDs might go beyond the limits.                      
      auto GetBlockStartPacked(PointerSpecification const& spec) const
      assumptious -> uintptr_t {
         if (not spec.IsPacked())
            return reinterpret_cast<uintptr_t>(GetBlockStart());
         
         LglsAssumeDev(mReferences != 0,
            "Can't get block start if entry isn't in use");

         // Return a packed pointer                                     
         auto pool = GetPool();
         uintptr_t result = pool->GetID();
         result <<= spec.EntryBits;
         result += pool->IndexFromAllocation(this);
         result <<= spec.OffsetBits;
         return result;
      }
      
      /// Return the aligned start of usable block memory, packed to some     
      /// specification.                                                      
      ///   @attention use this only if the allocation was produced using     
      ///      Allocator::AllocatePacked or Allocator::ReallocatePacked!      
      ///      Otherwise IDs might go beyond the limits.                      
      template<CT::CustomPointer T>
      auto GetBlockStartPackedAs() const assumptious -> T {
         LglsAssumeDev(mReferences != 0,
            "Can't get block start if entry isn't in use");

         // Return a packed pointer                                     
         auto pool = GetPool();
         return T {
            pool->GetID(),
            pool->IndexFromAllocation(this)
         };
      }

      /// Check if memory address is inside this entry                        
      ///   @param address address to check if inside this entry              
      ///   @return true if address is inside                                 
      auto Contains(const void* address) const assumptious -> bool {
         LglsAssumeDev(mReferences != 0,
            "Can't check if entry contains memory if entry isn't in use");
         const auto a = reinterpret_cast<uintptr_t>(address);
         const auto blockStart = reinterpret_cast<uintptr_t>(GetBlockStart());
         return a >= blockStart and a < blockStart + static_cast<uintptr_t>(GetSize());
      }

   protected: IF_LANGULUS_TESTING(public:)
      friend struct Pool;
      friend struct Allocator;
      
      /// Get the next entry in the free entry chain                          
      ///   @attention assumes allocation has been freed                      
      auto GetNextFreeEntry() const assumptious -> Allocation* {
         LglsAssumeDev(mReferences == 0,
            "Can't get next free entry from entry in use");
         return mNextFreeEntryFinder
            ? const_cast<Allocation*>(this - mNextFreeEntryFinder)
            : nullptr;
      }
      
      /// Set the next entry in the free entry chain                          
      ///   @attention assumes allocation has been freed                      
      void SetNextFreeEntry(Allocation const* a) assumptious {
         LglsAssumeDevAndOptimize(a,
            "If next entry is nullptr, use ResetNextFreeEntry instead");
         LglsAssumeDevAndOptimize(mReferences == 0,
            "Can't set next free entry if this entry is in use");
         LglsAssumeDevAndOptimize(a->mReferences == 0,
            "Can't set next free entry if next entry is in use");
         LglsAssumeDev(this - a >= ::std::numeric_limits<int32_t>::min()
                   and this - a <= ::std::numeric_limits<int32_t>::max(),
            "Entry difference is too big to fit in 32bit integer");
      
         mNextFreeEntryFinder = static_cast<int32_t>(this - a);
      
         LglsAssumeDev(GetNextFreeEntry() == a,
            "Next free entry isn't properly calculated from relative offset");
      }
      
      /// Reset the next entry in the free entry chain                        
      ///   @attention assumes allocation has been freed                      
      void ResetNextFreeEntry() assumptious {
         LglsAssumeDev(mReferences == 0,
            "Can't reset next free entry if this entry is in use");
         mNextFreeEntryFinder = 0;
      }
      
      /// Get the pool this allocation belongs to.                            
      /// Pools are always aligned, so all we have to do is mask out 'this'.  
      auto GetPool() const assumptious -> Pool const* {
         LglsAssumeDev(mReferences != 0, "Can't get pool if entry isn't in use");
         return reinterpret_cast<Pool const*>(
            reinterpret_cast<uintptr_t>(this) & ~((uintptr_t{1} << mPoolAlignment) - uintptr_t{1})
         );
      }
   };
}
