///                                                                           
/// Langulus::Fractalloc                                                      
/// Copyright (c) 2015 Dimo Markov <team@langulus.com>                        
/// Part of the Langulus framework, see https://langulus.com                  
///                                                                           
/// SPDX-License-Identifier: MIT                                              
///                                                                           
#pragma once
#include <Langulus/Core.hpp>
#include "Allocation.hpp"

#if not LANGULUS_FEATURE(MANAGED_MEMORY) or not LANGULUS_FEATURE(MANAGED_REFLECTION)
   #error "This file shouldn't be included if MANAGED_MEMORY or MANAGED_REFLECTION are disabled"
#endif


namespace Langulus::Fractalloc
{
   ///                                                                        
   ///   A flexible packed pointer                                            
   ///                                                                        
   /// You can tune the bits used to represent pool ID, entry ID and element  
   /// offset. The IDs corresponding to these values are unique for each T,   
   /// so there's no way one type of packed pointer steal budget from the     
   /// others. The allocator will attempt to satisfy your constraints at all  
   /// cost, until unable to do so.                                           
   /// Packing pointers is most useful on 64bit builds, and is by default     
   /// reduced to 32bits, however it can also be used as a security measure   
   /// to obfuscate pointers. You can create custom pointers that are even    
   /// smaller.                                                               
   ///   @tparam T the type behind the pointer                                
   ///   @tparam POOL_BITS if you have 8bit pool id, this means that you      
   ///      can access at most 255 pools. If the first 255 pools have already 
   ///      been filled, the allocator will simply deny your request. If not, 
   ///      it will use only the first 255 slots for your allocation request. 
   ///   @tparam ENTRY_BITS if you have 8bit entry representation, then you   
   ///      will support only up to 256 entries. This means that the memory   
   ///      manager will search in the allowed pools, if it can allocate one  
   ///      of the first 256 entries in order to satisfy your request. If it  
   ///      is unable to do so, your request will be denied.                  
   ///   @tparam OFFSET_BITS the offset bits show how many elements you can   
   ///      move from the start of the entry to the right. The byte offset is 
   ///      calculated as `sizeof(T) * offset`.                               
   #pragma pack(push, 1)
   template<class T, unsigned POOL_BITS = 4, unsigned ENTRY_BITS = 16, unsigned OFFSET_BITS = 12>
   struct PackedPointer {
      using CTTI_Sparse = Yup;
      using Type = T;

      using MakeDecvq    = PackedPointer<Decvq<T>,    POOL_BITS, ENTRY_BITS, OFFSET_BITS>;
      using MakeConst    = PackedPointer<const T,     POOL_BITS, ENTRY_BITS, OFFSET_BITS>;
      using MakeDecvqAll = PackedPointer<DecvqAll<T>, POOL_BITS, ENTRY_BITS, OFFSET_BITS>;
      using MakeConstAll = PackedPointer<ConstAll<T>, POOL_BITS, ENTRY_BITS, OFFSET_BITS>;

      static constexpr PointerSpecification Specification {POOL_BITS, ENTRY_BITS, OFFSET_BITS};
      static constexpr unsigned TotalBits = POOL_BITS + ENTRY_BITS + OFFSET_BITS;
      static_assert(TotalBits == 8 or TotalBits == 16 or TotalBits == 32);

      using Inner = ::std::conditional_t<TotalBits == 8,  uint8_t,
                    ::std::conditional_t<TotalBits == 16, uint16_t, uint32_t>>; 

   protected:
      friend struct Allocator;
      friend struct Allocation;
      
      Inner mAll;

      /// Manually construct the packed pointer                               
      constexpr PackedPointer(size_t poolId, size_t entryId, size_t elementId = 0)
      assumptious {
         LglsAssumeDevAndOptimize(poolId < (1u << Specification.PoolBits),
            "Pool ID beyond limits");
         LglsAssumeDevAndOptimize(entryId < (1u << Specification.EntryBits),
            "Entry ID beyond limits");
         LglsAssumeDevAndOptimize(elementId < (1u << Specification.OffsetBits),
            "Element ID beyond limits");
         mAll  = static_cast<Inner>(poolId);
         mAll <<= Specification.EntryBits;
         mAll += static_cast<Inner>(entryId);
         mAll <<= Specification.OffsetBits;
         mAll += static_cast<Inner>(elementId);
      }

      size_t GetPoolId() const noexcept {
         return mAll >> (Specification.EntryBits + Specification.OffsetBits);
      }

      size_t GetEntryId() const noexcept {
         return (mAll >> Specification.OffsetBits) & ((1u << Specification.EntryBits) - 1u);
      }

      size_t GetElementId() const noexcept {
         return mAll & ((1u << Specification.OffsetBits) - 1u);
      }

   public:
      constexpr PackedPointer() noexcept
         : mAll(0) {}
      
      constexpr PackedPointer(nullptr_t) noexcept
         : mAll(0) {}      

      explicit constexpr PackedPointer(void const*) noexcept
         : mAll(0) {}      

      explicit constexpr operator bool () const noexcept {
         return mAll != 0;
      }

      explicit constexpr operator void* () const noexcept {
         return Unpack();
      }

      explicit constexpr operator void const* () const noexcept {
         return Unpack();
      }

      explicit constexpr operator T* () const noexcept {
         return Unpack();
      }

      explicit constexpr operator T const* () const noexcept {
         return Unpack();
      }

      constexpr auto operator <=> (const PackedPointer& a) const noexcept {
         return mAll <=> a.mAll;
      }

      constexpr bool operator == (const PackedPointer& a) const noexcept {
         return mAll == a.mAll;
      }

      /// Unpack and dereference the pointer                                  
      T& operator * () const assumptious {
         LglsAssumeDev(mAll, "Trying to dereference a null pointer");
         return *Unpack();
      }

      /// Unpack the pointer                                                  
      auto Unpack() const noexcept -> T* {
         return reinterpret_cast<T*>(Allocator::UnpackPointer(
            Specification, MetaDataOf<T>(), mAll
         ));
      }

      /// Hash the packed pointer                                             
      auto GetHash() const noexcept -> Hash {
         return HashOf(mAll);
      }
   };
   #pragma pack(pop)
}