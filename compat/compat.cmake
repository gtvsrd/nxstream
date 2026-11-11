# Incluído em todo projeto CMake via -DCMAKE_PROJECT_INCLUDE (ver compat/README.md)
include_directories(BEFORE "${CMAKE_CURRENT_LIST_DIR}/include")
# Expõe os tipos BSD (u_int, u_long...) mesmo com -std=c99 -pedantic
add_compile_definitions(_DEFAULT_SOURCE)
