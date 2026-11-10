add_langulus_test(LangulusFractallocTest
   SOURCES	   $<IF:$<BOOL:${LANGULUS_OPTION_MANAGED_MEMORY}>,TestManaged.cpp,TestUnmanaged.cpp>
   LIBRARIES   LangulusFractalloc
)
