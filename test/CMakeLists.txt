file(GLOB_RECURSE
	LANGULUS_FRACTALLOC_TEST_SOURCES 
	LIST_DIRECTORIES FALSE CONFIGURE_DEPENDS
	*.cpp
)

add_executable(LangulusFractallocTest ${LANGULUS_FRACTALLOC_TEST_SOURCES})

target_link_libraries(LangulusFractallocTest
	PRIVATE		LangulusFractalloc
				Catch2
)

add_test(
	NAME		LangulusFractallocTest 
	COMMAND		LangulusFractallocTest
)