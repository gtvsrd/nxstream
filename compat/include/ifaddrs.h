// Compatibilidade: getifaddrs() para o Switch (implementado em compat/src/ifaddrs.c)
#ifndef NX_COMPAT_IFADDRS_H
#define NX_COMPAT_IFADDRS_H
#pragma GCC system_header
#include <sys/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ifaddrs {
    struct ifaddrs  *ifa_next;
    char            *ifa_name;
    unsigned int     ifa_flags;
    struct sockaddr *ifa_addr;
    struct sockaddr *ifa_netmask;
    struct sockaddr *ifa_dstaddr;
    void            *ifa_data;
};

int  getifaddrs(struct ifaddrs **ifap);
void freeifaddrs(struct ifaddrs *ifa);

#ifdef __cplusplus
}
#endif
#endif
