# Camada de compatibilidade (Switch)

Cabeçalhos e funções que as dependências do WebRTC (usrsctp, libjuice,
libSRTP) esperam encontrar num sistema POSIX e que a libnx não oferece.

| Arquivo | Para quem | O que faz |
|---|---|---|
| `include/sys/uio.h` | usrsctp | Expõe `struct iovec` (a libnx define em `sys/_iovec.h`) e `UIO_MAXIOV` |
| `include/ifaddrs.h` + `src/ifaddrs.c` | libjuice | `getifaddrs()` que devolve a interface Wi-Fi do Switch com o IP atual |
| `include/netinet/in.h` | libSRTP | Inclui também `arpa/inet.h` (`htons`, `ntohl`...) |
| `include/netinet/in_systm.h` | usrsctp | Tipos BSD `n_short`, `n_long`, `n_time` |
| `include/netinet/ip.h` | usrsctp | `struct ip` e constantes IPv4 no formato BSD |
| `src/getnameinfo.c` | libdatachannel | `getnameinfo()` que respeita `NI_NUMERICHOST` (a libnx faz DNS reverso mesmo assim); ligado com `-Wl,--wrap=getnameinfo` no Makefile |
| `src/pipe.c` | libjuice | `pipe()` com um par de sockets UDP locais (a libnx não tem; mesma solução da libjuice no Windows) |
| `include/errno.h` | usrsctp | Acrescenta `ERESTART` |
| `include/sys/socket.h` | usrsctp | Acrescenta `CMSG_ALIGN` |
| `patches/aplicar.py` | usrsctp, libjuice | Gerador de números aleatórios seguro do Switch (`randomGet`) em `user_environment.c` (usrsctp) e `random.c` (libjuice, que sem isso usaria `rand()`) |
| `patches/aplicar.py` | libjuice | Buffers do socket UDP menores no Switch (512 KB recepção, 128 KB envio, em vez de 1 MB + 1 MB), para não esgotar o poço de memória de sockets da libnx |
| `patches/aplicar.py` + `compat.cmake` | usrsctp, libdatachannel | `sockaddr_conn` no formato BSD no Switch (`usrsctp.h`) e `HAVE_SCONN_LEN`: sem isso o SCTP falha com `EAFNOSUPPORT` (errno 106) |
| `patches/aplicar.py` | libdatachannel | Thread da limpeza final mantida viva em vez de `detach()` (que falha na libnx) ou `join()` (que trava o fechamento). Coberto pelo teste automático `tests/limpeza` |
| `compat.cmake` | todas | Adiciona os cabeçalhos acima, `_DEFAULT_SOURCE` (tipos `u_int`, `u_long`) e `HAVE_SCONN_LEN` a todo projeto CMake |

Uso com CMake: `-DCMAKE_PROJECT_INCLUDE=<caminho>/compat/compat.cmake`.
O `src/ifaddrs.c` é compilado junto com o app.

Todos os cabeçalhos usam `#pragma GCC system_header`: algumas dependências compilam com `-pedantic -Werror`, e o `#include_next` (extensão do GCC) seria tratado como erro.

Alterações no código das dependências ficam em `patches/aplicar.py`, que para com erro se o trecho original mudar.
