// Compatibilidade: acrescenta ERESTART (BSD), usado pela usrsctp
#ifndef NX_COMPAT_ERRNO_H
#define NX_COMPAT_ERRNO_H
#pragma GCC system_header
#include_next <errno.h>
#ifndef ERESTART
#define ERESTART (-1)   // "reiniciar a chamada", mesmo valor do FreeBSD
#endif
#endif
