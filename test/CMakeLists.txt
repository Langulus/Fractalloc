file(GLOB_RECURSE
	LANGULUS_FRACTALLOC_TEST_SOURCES 
	LIST_DIRECTORIES FALSE CONFIGURE_DEPENDS
	*.cpp
)

add_langulus_test(LangulusFractallocTest
	SOURCES		${LANGULUS_FRACTALLOC_TEST_SOURCES}
	LIBRARIES	LangulusFractalloc
)