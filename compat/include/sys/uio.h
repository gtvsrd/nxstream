// Compatibilidade: a libnx define struct iovec em sys/_iovec.h, mas não tem sys/uio.h
#ifndef NX_COMPAT_SYS_UIO_H
#define NX_COMPAT_SYS_UIO_H
#pragma GCC system_header
#include <sys/types.h>
#include <sys/_iovec.h>

#ifndef UIO_MAXIOV
#define UIO_MAXIOV 1024   // máximo de iovecs por chamada (valor do BSD/Linux)
#endif
#endif
