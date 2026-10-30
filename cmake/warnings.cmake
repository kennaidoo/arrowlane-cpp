add_library(al_warnings INTERFACE)
target_compile_options(al_warnings INTERFACE
  $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:MSVC>>:
    /W4 /WX /permissive- /Zc:__cplusplus /utf-8 /GR- /EHsc /std:c++latest>
  $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<NOT:$<CXX_COMPILER_ID:MSVC>>>:
    -Wall -Wextra -Wpedantic -Werror>)
