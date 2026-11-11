// Compatibilidade: a libnx define struct iovec em sys/_iovec.h, mas não tem sys/uio.h
#ifndef NX_COMPAT_SYS_UIO_H
#define NX_COMPAT_SYS_UIO_H
#include <sys/types.h>
#include <sys/_iovec.h>
#endif
