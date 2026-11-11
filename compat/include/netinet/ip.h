// Compatibilidade: cabeçalho IPv4 no formato BSD, usado pela usrsctp.
// Definições equivalentes às de FreeBSD netinet/ip.h (licença BSD).
#ifndef NX_COMPAT_NETINET_IP_H
#define NX_COMPAT_NETINET_IP_H
#pragma GCC system_header
#include <stdint.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>

#define IPVERSION   4

struct ip {
    // O Switch (AArch64) é little-endian: o campo de 4 bits menos significativo vem primeiro
    uint8_t  ip_hl:4,        // tamanho do cabeçalho (em palavras de 32 bits)
             ip_v:4;         // versão
    uint8_t  ip_tos;         // tipo de serviço
    uint16_t ip_len;         // tamanho total
    uint16_t ip_id;          // identificação
    uint16_t ip_off;         // fragmentação
#define IP_RF      0x8000    // reservado
#define IP_DF      0x4000    // não fragmentar
#define IP_MF      0x2000    // mais fragmentos
#define IP_OFFMASK 0x1fff
    uint8_t  ip_ttl;         // tempo de vida
    uint8_t  ip_p;           // protocolo
    uint16_t ip_sum;         // checksum
    struct in_addr ip_src, ip_dst;
} __attribute__((packed, aligned(2)));

#define IP_MAXPACKET 65535
#define MAXTTL       255
#define IPDEFTTL     64

#ifndef IP_HDRINCL
#define IP_HDRINCL   2       // opção de socket (valor do BSD)
#endif

#endif
