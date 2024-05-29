///                                                                           
/// Langulus::Fractalloc                                                      
/// Copyright (c) 2015 Dimo Markov <team@langulus.com>                        
/// Part of the Langulus framework, see https://langulus.com                  
///                                                                           
/// SPDX-License-Identifier: MIT                                              
///                                                                           
#pragma once
#include "../source/Allocation.hpp"
#include "../source/Pool.hpp"
#include <unordered_set>
#include <optional>


namespace Langulus::Fractalloc
{

   ///                                                                        
   ///   Memory allocator                                                     
   ///                                                                        
   /// The lowest-level memory management interface                           
   /// Basically an overcomplicated wrapper for malloc/free                   
   ///                                                                        
   struct Allocator {
      #if LANGULUS_FEATURE(MEMORY_STATISTICS)
         ///                                                                  
         /// Structure for keeping track of allocations                       
         ///                                                                  
         struct Statistics {
            // The real allocated bytes, provided by malloc in backend  
            Offset mBytesAllocatedByBackend {};
            // The bytes allocated by the frontend                      
            Offset mBytesAllocatedByFrontend {};
            // Number of registered entries                             
            Count mEntries {};
            // Number of registered pools                               
            Count mPools {};
            // Increases with each call to State::Assert, used to       
            // diff pools                                               
            Count mStep {};

            #if LANGULUS_FEATURE(MANAGED_REFLECTION)
               // Number of registered meta datas                       
               Count mDataDefinitions {};
               // Number of registered meta traits                      
               Count mTraitDefinitions {};
               // Number of registered meta verbs                       
               Count mVerbDefinitions {};
            #endif

            bool operator == (const Statistics&) const noexcept;

            void AddPool(const Pool*) noexcept;
            void DelPool(const Pool*) noexcept;
         };
      
         ///                                                                  
         /// Structure that holds a single memory manager state, used for     
         /// comparing states in order to detect leaks while testing          
         ///                                                                  
         struct State {
         private:
            // The previous state                                       
            ::std::optional<Statistics> mState;

         public:
            LANGULUS_API(FRACTALLOC) bool Assert();
         };

      private:
         // The current memory manager statistics                       
         Statistics mStatistics {};
      #else
         /// No state when MEMORY_STATISTICS feature is disabled              
         struct State {
            consteval bool Assert() const noexcept { return true; }
         };
      #endif

   private:
      // Default pool chain                                             
      Pool* mDefaultPoolChain {};
      // The last succesfull Find() result in default pool chain        
      mutable const Pool* mLastFoundPool {};

      // Pool chains for types that use PoolTactic::Size                
      static constexpr Count SizeBuckets = sizeof(Offset) * 8;
      Pool* mSizePoolChain[SizeBuckets] {};

      // A set of types, that are currently in use                      
      // Used to detect if a shared object is safe to be unloaded       
      // MUST BE BY POINTER, because there can be multiple definitions  
      ::std::unordered_set<const RTTI::MetaData*> mInstantiatedTypes;

   private:
      #if LANGULUS_FEATURE(MEMORY_STATISTICS)
         LANGULUS_API(FRACTALLOC)
         static void DumpPool(Offset, const Pool*) noexcept;
         
         NOD() LANGULUS_API(FRACTALLOC)
         bool IntegrityCheckChain(const Pool*);
      #endif

      LANGULUS_API(FRACTALLOC)
      void CollectGarbageChain(Pool*&);

      const Allocation* FindInChain(const void*, const Pool*) const IF_UNSAFE(noexcept);
      bool ContainedInChain(const void*, const Pool*) const IF_UNSAFE(noexcept);

   public:
      NOD() LANGULUS_API(FRACTALLOC)
      static Allocation* Allocate(RTTI::DMeta, Offset) IF_UNSAFE(noexcept);

      NOD() LANGULUS_API(FRACTALLOC)
      static Allocation* Reallocate(Offset, Allocation*) IF_UNSAFE(noexcept);

      LANGULUS_API(FRACTALLOC)
      static void Deallocate(Allocation*) IF_UNSAFE(noexcept);

      NOD() LANGULUS_API(FRACTALLOC)
      static const Allocation* Find(RTTI::DMeta, const void*) IF_UNSAFE(noexcept);

      NOD() LANGULUS_API(FRACTALLOC)
      static bool CheckAuthority(RTTI::DMeta, const void*) IF_UNSAFE(noexcept);

      NOD() LANGULUS_API(FRACTALLOC)
      static Pool* AllocatePool(DMeta, Offset) IF_UNSAFE(noexcept);

      LANGULUS_API(FRACTALLOC)
      static void DeallocatePool(Pool*) IF_UNSAFE(noexcept);

      LANGULUS_API(FRACTALLOC)
      static void CollectGarbage();

      #if LANGULUS_FEATURE(MANAGED_REFLECTION)
         LANGULUS_API(FRACTALLOC)
         static Count CheckBoundary(const Token&) noexcept;
      #endif

      #if LANGULUS_FEATURE(MEMORY_STATISTICS)
         NOD() LANGULUS_API(FRACTALLOC)
         static const Statistics& GetStatistics() noexcept;

         LANGULUS_API(FRACTALLOC)
         static void DumpPools() noexcept;

         LANGULUS_API(FRACTALLOC)
         static void Diff(const Statistics&) noexcept;
         
         LANGULUS_API(FRACTALLOC)
         static bool IntegrityCheck();
      #endif
   };


   ///                                                                        
   ///   The global memory manager instance                                   
   ///                                                                        
   LANGULUS_API(FRACTALLOC) extern Allocator Instance;

} // namespace Langulus::Fractalloc

#include "../source/Allocation.inl"
#include "../source/Pool.inl"
