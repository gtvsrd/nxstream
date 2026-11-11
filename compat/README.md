# Camada de compatibilidade (Switch)

Cabeçalhos e funções que as dependências do WebRTC (usrsctp, libjuice,
libSRTP) esperam encontrar num sistema POSIX e que a libnx não oferece.

| Arquivo | Para quem | O que faz |
|---|---|---|
| `include/sys/uio.h` | usrsctp | Expõe `struct iovec` (a libnx define em `sys/_iovec.h`) |
| `include/ifaddrs.h` + `src/ifaddrs.c` | libjuice | `getifaddrs()` que devolve a interface Wi-Fi do Switch com o IP atual |
| `include/netinet/in.h` | libSRTP | Inclui também `arpa/inet.h` (`htons`, `ntohl`...) |
| `compat.cmake` | todas | Adiciona os cabeçalhos acima e `_DEFAULT_SOURCE` (tipos `u_int`, `u_long`) a todo projeto CMake |

Uso com CMake: `-DCMAKE_PROJECT_INCLUDE=<caminho>/compat/compat.cmake`.
O `src/ifaddrs.c` é compilado junto com o app.
