///                                                                           
/// Langulus::Anyness                                                         
/// Copyright (c) 2012 Dimo Markov <team@langulus.com>                        
/// Part of the Langulus framework, see https://langulus.com                  
///                                                                           
/// SPDX-License-Identifier: GPL-3.0-or-later                                 
///                                                                           
#pragma once
#include <Langulus/Core.hpp>
#include <Langulus/Utils/Pot.hpp>

#if LANGULUS_FEATURE(MANAGED_MEMORY)
   #error "This file shouldn't be included if MANAGED_MEMORY is enabled"
#endif


namespace Langulus::Unmanaged
{
   struct Allocator;

   ///                                                                        
   ///   Memory allocation                                                    
   struct Allocation {
   protected:
      friend struct Allocator;
      
      // The number of references to this memory.                       
      // Most often used, so first for immediate access.                
      int32_t mReferences = 1;

      #if LANGULUS_FEATURE(MEMORY_STATISTICS)
         // Acts like a timestamp of when the allocation happened       
         uint64_t mStep;
      #endif

      // Allocated bytes usable by client                               
      pot_t mSize;
      // The alignment of the contained data                            
      pot_t mAlignment;

   public:
      Allocation() = delete;
      Allocation(const Allocation&) = delete;
      Allocation(Allocation&&) = delete;

      /// Initialize an allocation                                            
      ///   @param alignment data alignment                                   
      ///   @param size the number of allocated bytes                         
      Allocation(pot_t alignment, pot_t size) noexcept
         : mSize      {size}
         , mAlignment {alignment} {}

      /// Get the number of references                                        
      auto GetUses() const noexcept {
         return mReferences;
      }
      
      /// Get the user bytes                                                  
      ///   @return the byte size of usable memory region                     
      auto GetSize() const noexcept -> pot_t {
         return mSize;
      }
      
      /// Return the aligned start of usable block memory                     
      ///   @return aligned pointer to the entry's memory                     
      auto GetBlockStart() const noexcept -> uint8_t* {
         const auto entryStart = reinterpret_cast<const uint8_t*>(this);
         return const_cast<uint8_t*>(Align(entryStart + sizeof(Allocation), mAlignment));
      }
      
      /// Check if memory address is inside this entry                        
      ///   @param address address to check if inside this entry              
      ///   @return true if address is inside                                 
      bool Contains(const void* address) const noexcept {
         const auto a = static_cast<const uint8_t*>(address);
         const auto blockStart = GetBlockStart();
         return a >= blockStart and a < blockStart + static_cast<size_t>(mSize);
      }
      
      /// Reference the entry 'c' times                                       
      ///   @param c the number of references to add                          
      void AddRef(int32_t c) noexcept {
         mReferences += c;
      }
   };
}
