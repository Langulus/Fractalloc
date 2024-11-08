cmake_minimum_required(VERSION 3.28)
project(LangulusFractalloc
    VERSION         1.0.0
    DESCRIPTION     "Langulus memory management library"
    HOMEPAGE_URL    https://langulus.com
)

# Check if this project is built as standalone, or a part of something else 
if(PROJECT_IS_TOP_LEVEL OR NOT LANGULUS)
	set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
	set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)

    include(LangulusUtilities.cmake)

    # Always enable the managed memory feature                              
    set(LANGULUS_FEATURE_MANAGED_MEMORY ON CACHE BOOL
        "Memory will be pooled and recycled when freed, and you can safely push any kind of pointer, \
        as long as it was allocated by the memory manager, or by the overridden new/delete feature"
        FORCE
    )

    # Add Langulus::Core/Logger/RTTI libraries                              
    fetch_langulus_module(Core   GIT_TAG 35756f11d2f9c475f27b094b8d4c82cd453969fc)
    fetch_langulus_module(Logger GIT_TAG dafbeb825071ec60d8403254143f75606151a7e6)
    fetch_langulus_module(RTTI   GIT_TAG fc49750884ac943dff4261ac5b8dfb2c148423d7)
endif()

if (NOT LANGULUS_FEATURE_MANAGED_MEMORY)
    message(FATAL_ERROR "Fractalloc can be built only with enabled LANGULUS_FEATURE_MANAGED_MEMORY")
endif()

add_langulus_library(LangulusFractalloc
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