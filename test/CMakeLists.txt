add_executable(LangulusFractallocTest 
	Main.cpp
	TestAllocator.cpp
	TestNewDelete.cpp
)

target_link_libraries(LangulusFractallocTest
	PRIVATE		LangulusFractalloc
				Catch2
)

add_test(
	NAME		LangulusFractallocTest 
	COMMAND		LangulusFractallocTest
)

langulus_copy_dlls(LangulusFractallocTest ON LangulusFractallocTest)
