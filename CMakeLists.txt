cmake_minimum_required(VERSION 3.24)
project(LangulusFractalloc
    VERSION         1.0.0
    DESCRIPTION     "Langulus memory management library"
    HOMEPAGE_URL    https://langulus.com
)

# Check if this project is built as standalone, or a part of something else 
if(PROJECT_IS_TOP_LEVEL OR NOT LANGULUS)
	set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
	set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin/lib)

    include(LangulusUtilities.cmake)

    # Always enable the managed memory feature                              
    set(LANGULUS_FEATURE_MANAGED_MEMORY ON CACHE BOOL
        "Memory will be pooled and recycled when freed, and you can safely push any kind of pointer, \
        as long as it was allocated by the memory manager, or by the overridden new/delete feature"
        FORCE
    )

    # Add Langulus::Core/Logger/RTTI libraries                              
    fetch_langulus_module(Core   GIT_TAG 0097bd180a5c625f88db0db0514bbf93aff53c7a)
    fetch_langulus_module(Logger GIT_TAG b63513abb9fb3014107335949e17e41e6e8a334b)
    fetch_langulus_module(RTTI   GIT_TAG f7c610255b819b99a6adcdefa970387490093c25)
endif()

if (NOT LANGULUS_FEATURE_MANAGED_MEMORY)
    message(FATAL_ERROR "Fractalloc can be built only with enabled LANGULUS_FEATURE_MANAGED_MEMORY")
endif()

add_library(LangulusFractalloc ${LANGULUS_LIBRARY_TYPE}
    $<TARGET_OBJECTS:LangulusLogger>
    $<TARGET_OBJECTS:LangulusRTTI>
    source/Allocator.cpp
)

target_include_directories(LangulusFractalloc
    PUBLIC	    include
                $<TARGET_PROPERTY:LangulusLogger,INTERFACE_INCLUDE_DIRECTORIES>
                $<TARGET_PROPERTY:LangulusRTTI,INTERFACE_INCLUDE_DIRECTORIES>
)

target_link_libraries(LangulusFractalloc
    PUBLIC      LangulusCore
                fmt
)

target_compile_definitions(LangulusFractalloc
    PRIVATE     LANGULUS_EXPORT_ALL
)

if(LANGULUS_TESTING)
    enable_testing()
    add_subdirectory(test)
endif()