///                                                                           
/// Langulus::Fractalloc                                                      
/// Copyright (c) 2015 Dimo Markov <team@langulus.com>                        
/// Part of the Langulus framework, see https://langulus.com                  
///                                                                           
/// Distributed under GNU General Public License v3+                          
/// See LICENSE file, or https://www.gnu.org/licenses                         
///                                                                           
#pragma once
#include "Config.hpp"
#include <RTTI/Meta.hpp>


namespace Langulus::Fractalloc
{

   class Pool;
   
   template<class T>
   concept AllocationPrimitive = requires(T a) { 
      {T::GetNewAllocationSize(Size {})} -> CT::Same<Size>;
   };


   ///                                                                        
   ///   Memory allocation                                                    
   ///                                                                        
   /// This is a single allocation record                                     
   ///                                                                        
   struct Allocation final {
   friend class Pool;
   friend struct Allocator;
   protected:
      // Allocated bytes for this chunk                                 
      Size mAllocatedBytes;
      // The number of references to this memory                        
      Count mReferences;
      union {
         // This pointer has two uses, depending on mReferences         
         // If mReferences > 0, it refers to the pool that owns the     
         //    allocation, or	handle for std::free() if MANAGED_MEMORY  
         //    feature is not enabled                                   
         // If mReferences == 0, it refers to the next free entry to be 
         //    reused                                                   
         Pool* mPool;
         Allocation* mNextFreeEntry;
      };

   public:
      Allocation() = delete;
      Allocation(const Allocation&) = delete;
      Allocation(Allocation&&) = delete;
      ~Allocation() = delete;

      constexpr Allocation(Size, Pool*) noexcept;

      NOD() static constexpr Size GetSize() noexcept;
      NOD() static constexpr Size GetNewAllocationSize(Size) noexcept;
      NOD() static constexpr Size GetMinAllocation() noexcept;

      NOD() constexpr const Count& GetUses() const noexcept;
      NOD() Byte* GetBlockStart() const noexcept;
      NOD() const Byte* GetBlockEnd() const noexcept;
      NOD() constexpr Size GetTotalSize() const noexcept;
      NOD() constexpr const Size& GetAllocatedSize() const noexcept;
      NOD() bool Contains(const void*) const noexcept;
      NOD() bool CollisionFree(const Allocation&) const noexcept;

      template<class T>
      NOD() T* As() const noexcept;

      constexpr void Keep() noexcept;
      constexpr void Keep(Count) noexcept;
      constexpr void Free() noexcept;
      constexpr void Free(Count) noexcept;
   };

} // namespace Langulus::Anyness

#include "Allocation.inl"
