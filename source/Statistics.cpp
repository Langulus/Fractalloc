///                                                                           
/// Langulus::Fractalloc                                                      
/// Copyright (c) 2015 Dimo Markov <team@langulus.com>                        
/// Part of the Langulus framework, see https://langulus.com                  
///                                                                           
/// SPDX-License-Identifier: MIT                                              
///                                                                           
#include "Allocator.hpp"

#if 0
   #include <Langulus/Logger/EnableVerbose.hpp>
#else
   #include <Langulus/Logger/NoVerbose.hpp>
#endif


namespace Langulus::Fractalloc
{
   /// Compare two states                                                     
   bool Statistics::operator == (const Statistics& rhs) const assumptious {
      LglsAssumeDevAndOptimize(
         mBytesAllocatedByFrontend <= mBytesAllocatedByBackend,
         "Impossible amount of frontend allocation"
      );

      return mBytesAllocatedByBackend == rhs.mBytesAllocatedByBackend
         and mBytesAllocatedByFrontend == rhs.mBytesAllocatedByFrontend
         and mEntries == rhs.mEntries
         and mPools == rhs.mPools
      #if LANGULUS_FEATURE(MANAGED_REFLECTION)
         and mDataDefinitions == rhs.mDataDefinitions
         and mTraitDefinitions == rhs.mTraitDefinitions
         and mVerbDefinitions == rhs.mVerbDefinitions
      #endif
      ;
   }

   /// Account for a newly allocated pool                                     
   ///   @param pool the pool to account for                                  
   void Statistics::AddPool(const Pool* pool) IF_UNSAFE(noexcept) {
      mBytesAllocatedByBackend  += pool->GetTotalSize();
      mBytesAllocatedByFrontend += pool->GetAllocatedByFrontend();
      LglsAssumeDevAndOptimize(
         mBytesAllocatedByFrontend <= mBytesAllocatedByBackend,
         "Impossible amount of frontend allocation"
      );
      ++mPools;
      ++mEntries;
   }
   
   /// Account for a removed pool                                             
   ///   @param pool the pool to account for                                  
   void Statistics::DelPool(const Pool* pool) IF_UNSAFE(noexcept) {
      LglsAssumeDev(
         mBytesAllocatedByBackend >= pool->GetTotalSize(),
         "Impossible amount of backend allocation"
      );
      mBytesAllocatedByBackend -= pool->GetTotalSize();
      --mPools;
   }
   
   /// Check for memory leaks, by retrieving the new memory manager state     
   /// and comparing it against this one                                      
   ///   @return true if no functional difference between the states          
   bool State::Assert() {
      Allocator::CollectGarbage();

      if (not Allocator::IntegrityCheck()) {
         Logger::Error("Memory integrity check failure");
         return false;
      }

      if (mState.has_value()) {
         if (mState != Allocator::GetStatistics()) {
            // Assertion failure                                        
            Allocator::DumpPools();
            Allocator::Diff(mState.value());
            mState = Allocator::GetStatistics();
            ++Allocator::GetStatistics().mStep;
            Logger::Error("Memory state mismatch");
            return false;
         }
      }

      // All is fine                                                    
      mState = Allocator::GetStatistics();
      ++Allocator::GetStatistics().mStep;
      return true;
   }
}
