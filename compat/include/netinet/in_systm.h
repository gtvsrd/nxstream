// Compatibilidade: tipos de rede do BSD usados pela usrsctp
#ifndef NX_COMPAT_NETINET_IN_SYSTM_H
#define NX_COMPAT_NETINET_IN_SYSTM_H
#pragma GCC system_header
#include <stdint.h>
typedef uint16_t n_short;   // short em ordem de rede
typedef uint32_t n_long;    // long em ordem de rede
typedef uint32_t n_time;    // ms desde 00:00 UTC, ordem de rede
#endif
