// Compatibilidade: código que só inclui netinet/in.h também espera htons/ntohl,
// que na libnx ficam em arpa/inet.h.
#ifndef NX_COMPAT_NETINET_IN_H
#define NX_COMPAT_NETINET_IN_H
#pragma GCC system_header
#include_next <netinet/in.h>
#include <arpa/inet.h>
#endif
