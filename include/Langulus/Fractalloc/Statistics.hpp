///                                                                           
/// Langulus::Fractalloc                                                      
/// Copyright (c) 2015 Dimo Markov <team@langulus.com>                        
/// Part of the Langulus framework, see https://langulus.com                  
///                                                                           
/// SPDX-License-Identifier: MIT                                              
///                                                                           
#pragma once
#include <Langulus/Core.hpp>
#include <optional>

#if not LANGULUS_FEATURE(MEMORY_STATISTICS)
   #error "This file shouldn't be included if MEMORY_STATISTICS is disabled"
#endif


namespace Langulus::Fractalloc
{
   struct Pool;
      
   ///                                                                        
   /// Structure for keeping track of allocations                             
   ///                                                                        
   struct Statistics {
      // The real allocated bytes, provided by malloc in backend        
      size_t mBytesAllocatedByBackend {};
      // The bytes allocated by the frontend                            
      size_t mBytesAllocatedByFrontend {};
      // Number of registered entries                                   
      size_t mEntries {};
      // Number of registered pools                                     
      size_t mPools {};
      // Increases with each call to State::Assert, used to diff pools  
      mutable size_t mStep {};

      #if LANGULUS_FEATURE(MANAGED_REFLECTION)
         // Number of registered meta datas                             
         size_t mDataDefinitions {};
         // Number of registered meta traits                            
         size_t mTraitDefinitions {};
         // Number of registered meta verbs                             
         size_t mVerbDefinitions {};
      #endif

      bool operator == (const Statistics&) const assumptious;

      void AddPool(const Pool*) assumptious;
      void DelPool(const Pool*) assumptious;
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
}
