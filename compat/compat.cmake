# Incluído em todo projeto CMake via -DCMAKE_PROJECT_INCLUDE (ver compat/README.md)
include_directories(BEFORE "${CMAKE_CURRENT_LIST_DIR}/include")
# Expõe os tipos BSD (u_int, u_long...) mesmo com -std=c99 -pedantic
add_compile_definitions(_DEFAULT_SOURCE)
# A libnx usa sockaddr no formato BSD (sa_len). Com o patch na usrsctp.h
# (patches/aplicar.py), a sockaddr_conn também fica no formato BSD, e a
# libdatachannel precisa preencher sconn_len, o que ela só faz com esta definição.
add_compile_definitions(HAVE_SCONN_LEN)
