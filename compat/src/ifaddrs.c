// getifaddrs() para o Switch: devolve uma única interface (o Wi-Fi/rede atual)
// com o IP obtido por gethostid(). É o que a libjuice usa para montar os
// candidatos ICE locais.
#include <ifaddrs.h>
#include <errno.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    struct ifaddrs     ifa;
    struct sockaddr_in endereco;
    struct sockaddr_in mascara;
    char               nome[8];
} InterfaceSwitch;

int getifaddrs(struct ifaddrs **ifap) {
    if (!ifap) {
        errno = EINVAL;
        return -1;
    }
    *ifap = NULL;

    long ip = gethostid(); // ordem de rede; 0 ou 127.0.0.1 sem conexão
    if (ip == 0 || (unsigned long)ip == htonl(INADDR_LOOPBACK)) {
        errno = ENETDOWN;
        return -1;
    }

    InterfaceSwitch *i = calloc(1, sizeof *i);
    if (!i) {
        errno = ENOMEM;
        return -1;
    }
    strcpy(i->nome, "wlan0");
    i->endereco.sin_family = AF_INET;
#ifdef __SWITCH__ // campo BSD: existe na libnx, não no Linux
    i->endereco.sin_len = sizeof i->endereco;
#endif
    i->endereco.sin_addr.s_addr = (in_addr_t)ip;
    // A libnx não informa a máscara; a libjuice não a usa. /24 é o caso comum em casa.
    i->mascara.sin_family = AF_INET;
#ifdef __SWITCH__
    i->mascara.sin_len = sizeof i->mascara;
#endif
    i->mascara.sin_addr.s_addr = htonl(0xFFFFFF00u);

    i->ifa.ifa_name = i->nome;
    i->ifa.ifa_flags = IFF_UP | IFF_RUNNING;
    i->ifa.ifa_addr = (struct sockaddr *)&i->endereco;
    i->ifa.ifa_netmask = (struct sockaddr *)&i->mascara;

    *ifap = &i->ifa;
    return 0;
}

void freeifaddrs(struct ifaddrs *ifa) {
    // Uma única alocação: struct ifaddrs é o primeiro membro de InterfaceSwitch
    free(ifa);
}
