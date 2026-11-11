// Compatibilidade: acrescenta CMSG_ALIGN, usado pela usrsctp
#ifndef NX_COMPAT_SYS_SOCKET_H
#define NX_COMPAT_SYS_SOCKET_H
#pragma GCC system_header
#include_next <sys/socket.h>
#ifndef CMSG_ALIGN
#define CMSG_ALIGN(tam) (((tam) + sizeof(long) - 1) & ~(sizeof(long) - 1))
#endif
#endif
