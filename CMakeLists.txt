cmake_minimum_required(VERSION 3.30)
project(LangulusFractalloc
   VERSION         1.0.0
   DESCRIPTION     "Langulus memory management library"
   HOMEPAGE_URL    https://langulus.com
)

# Check if this project is built as standalone, or a part of something else 
if (PROJECT_IS_TOP_LEVEL OR NOT LANGULUS)
   set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
   set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
   set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
   include(LangulusUtilities.cmake)
   fetch_langulus_module(RTTI GIT_TAG wip)
endif()

add_langulus_library(LangulusFractalloc
   SOURCES  $<TARGET_OBJECTS:LangulusLogger>
            $<TARGET_OBJECTS:LangulusRTTI>
            $<$<BOOL:${LANGULUS_OPTION_PROFILING}>:$<TARGET_OBJECTS:LangulusPercist>>
            $<$<BOOL:${LANGULUS_OPTION_MANAGED_MEMORY}>:source/Allocator.cpp>
            $<$<BOOL:${LANGULUS_OPTION_MANAGED_MEMORY}>:source/Pool.cpp>
            $<$<BOOL:${LANGULUS_OPTION_MEMORY_STATISTICS}>:source/Statistics.cpp>
)

target_include_directories(LangulusFractalloc
   PUBLIC   $<TARGET_PROPERTY:LangulusLogger,INTERFACE_INCLUDE_DIRECTORIES>
            $<TARGET_PROPERTY:LangulusRTTI,INTERFACE_INCLUDE_DIRECTORIES>
            $<$<BOOL:${LANGULUS_OPTION_PROFILING}>:$<TARGET_PROPERTY:LangulusPercist,INTERFACE_INCLUDE_DIRECTORIES>>
            include
)

target_link_libraries(LangulusFractalloc
   PUBLIC   LangulusCore
            $<$<BOOL:${LANGULUS_OPTION_LOGGING}>:fmt>
)

target_compile_definitions(LangulusFractalloc
   PRIVATE  LANGULUS_EXPORT_ALL
)

if(LANGULUS_OPTION_TESTING)
   enable_testing()
   add_subdirectory(test)
endif()