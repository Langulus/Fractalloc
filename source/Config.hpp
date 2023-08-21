///                                                                           
/// Langulus::Fractalloc                                                      
/// Copyright(C) 2015 Dimo Markov <langulusteam@gmail.com>                    
///                                                                           
/// Distributed under GNU General Public License v3+                          
/// See LICENSE file, or https://www.gnu.org/licenses                         
///                                                                           
#pragma once
#include <Core/Config.hpp>

#if defined(LANGULUS_EXPORT_ALL) || defined(LANGULUS_EXPORT_FRACTALLOC)
   #define LANGULUS_API_FRACTALLOC() LANGULUS_EXPORT()
#else
   #define LANGULUS_API_FRACTALLOC() LANGULUS_IMPORT()
#endif

/// Memory manager shall keep track of statistics                             
/// Some overhead upon allocation/deallocation/reallocation                   
/// Some methods, like string null-termination will pick more memory-         
/// consitent, but less performant approaches (see Text::Terminate())         
#ifdef LANGULUS_ENABLE_FEATURE_MEMORY_STATISTICS
   #define LANGULUS_FEATURE_MEMORY_STATISTICS() 1
   #define IF_LANGULUS_MEMORY_STATISTICS(a) a
   #define IF_NOT_LANGULUS_MEMORY_STATISTICS(a)
#else
   #define LANGULUS_FEATURE_MEMORY_STATISTICS() 0
   #define IF_LANGULUS_MEMORY_STATISTICS(a)
   #define IF_NOT_LANGULUS_MEMORY_STATISTICS(a) a
#endif

/// Replace the default new-delete operators with custom ones                 
/// No overhead, no dependencies                                              
#ifdef LANGULUS_ENABLE_FEATURE_NEWDELETE
   #define LANGULUS_FEATURE_NEWDELETE() 1
   #define IF_LANGULUS_NEWDELETE(a) a
   #define IF_NOT_LANGULUS_NEWDELETE(a)
#else
   #define LANGULUS_FEATURE_NEWDELETE() 0
   #define IF_LANGULUS_NEWDELETE(a) 
   #define IF_NOT_LANGULUS_NEWDELETE(a) a
#endif

/// Make the rest of the code aware, that Langulus::Fractalloc is included    
#define LANGULUS_LIBRARY_FRACTALLOC() 1
