///                                                                           
/// Langulus::Fractalloc                                                      
/// Copyright(C) 2015 Dimo Markov <langulusteam@gmail.com>                    
///                                                                           
/// Distributed under GNU General Public License v3+                          
/// See LICENSE file, or https://www.gnu.org/licenses                         
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
            Size mBytesAllocatedByBackend {};
            // The bytes allocated by the frontend                      
            Size mBytesAllocatedByFrontend {};
            // Number of registered entries                             
            Count mEntries {};
            // Number of registered pools                               
            Count mPools {};

            #if LANGULUS_FEATURE(MANAGED_REFLECTION)
               // Number of registered meta datas                       
               Count mDataDefinitions {};
               // Number of registered meta traits                      
               Count mTraitDefinitions {};
               // Number of registered meta verbs                       
               Count mVerbDefinitions {};
            #endif

            bool operator == (const Statistics&) const noexcept = default;

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
            constexpr bool Assert() const noexcept { return true; }
         };
      #endif

   private:
      // Default pool chain                                             
      Pool* mDefaultPoolChain {};
      // The last succesfull Find() result in default pool chain        
      mutable Pool* mLastFoundPool {};

      // Pool chains for types that use PoolTactic::Size                
      static constexpr Count SizeBuckets = sizeof(Size) * 8;
      Pool* mSizePoolChain[SizeBuckets] {};

      // A set of types, that are currently in use                      
      // Used to detect if a shared object is safe to be unloaded       
      ::std::unordered_set<DMeta> mInstantiatedTypes;

   private:
      #if LANGULUS_FEATURE(MEMORY_STATISTICS)
         LANGULUS_API(FRACTALLOC)
         static void DumpPool(Offset, const Pool*) noexcept;
      #endif

      LANGULUS_API(FRACTALLOC)
      void CollectGarbageChain(Pool*&);

      Allocation* FindInChain(const void*, Pool*) const SAFETY_NOEXCEPT();
      bool ContainedInChain(const void*, Pool*) const SAFETY_NOEXCEPT();

   public:
      NOD() LANGULUS_API(FRACTALLOC)
      static Allocation* Allocate(RTTI::DMeta, const Size&) SAFETY_NOEXCEPT();

      NOD() LANGULUS_API(FRACTALLOC)
      static Allocation* Reallocate(const Size&, Allocation*) SAFETY_NOEXCEPT();

      LANGULUS_API(FRACTALLOC)
      static void Deallocate(Allocation*) SAFETY_NOEXCEPT();

      NOD() LANGULUS_API(FRACTALLOC)
      static Allocation* Find(RTTI::DMeta, const void*) SAFETY_NOEXCEPT();

      NOD() LANGULUS_API(FRACTALLOC)
      static bool CheckAuthority(RTTI::DMeta, const void*) SAFETY_NOEXCEPT();

      NOD() LANGULUS_API(FRACTALLOC)
      static Pool* AllocatePool(DMeta, const Size&) SAFETY_NOEXCEPT();

      LANGULUS_API(FRACTALLOC)
      static void DeallocatePool(Pool*) SAFETY_NOEXCEPT();

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
      #endif
   };


   ///                                                                        
   ///   The global memory manager instance                                   
   ///                                                                        
   LANGULUS_API(FRACTALLOC) extern Allocator Instance;

} // namespace Langulus::Fractalloc