// getnameinfo() com NI_NUMERICHOST respeitado.
//
// A libnx ignora NI_NUMERICHOST e faz consulta reversa de DNS: para
// 203.0.113.64 devolveu "ip-cgnat-203-0-113-64.provedor.example", que a
// libdatachannel não aceita como endereço de candidato (log da v0.4.3), além
// de bloquear a thread esperando o DNS.
//
// Ligado com -Wl,--wrap=getnameinfo (Makefile): as chamadas a getnameinfo()
// vêm para __wrap_getnameinfo(), e __real_getnameinfo() é a original da libnx.
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

int __real_getnameinfo(const struct sockaddr *sa, socklen_t salen, char *host, socklen_t hostlen,
                       char *serv, socklen_t servlen, int flags);

int __wrap_getnameinfo(const struct sockaddr *sa, socklen_t salen, char *host, socklen_t hostlen,
                       char *serv, socklen_t servlen, int flags) {
    if (!(flags & NI_NUMERICHOST) || !sa || sa->sa_family != AF_INET)
        return __real_getnameinfo(sa, salen, host, hostlen, serv, servlen, flags);
    if (salen < (socklen_t)sizeof(struct sockaddr_in))
        return EAI_FAMILY;

    const struct sockaddr_in *in = (const struct sockaddr_in *)sa;
    if (host && hostlen > 0) {
        const unsigned char *b = (const unsigned char *)&in->sin_addr.s_addr;
        int n = snprintf(host, hostlen, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
        if (n < 0 || (socklen_t)n >= hostlen)
            return EAI_OVERFLOW;
    }
    if (serv && servlen > 0) {
        if (flags & NI_NUMERICSERV) {
            int n = snprintf(serv, servlen, "%u", (unsigned)ntohs(in->sin_port));
            if (n < 0 || (socklen_t)n >= servlen)
                return EAI_OVERFLOW;
        } else {
            // Nome do serviço: deixa com a original, só para a porta
            return __real_getnameinfo(sa, salen, NULL, 0, serv, servlen, flags & ~NI_NUMERICHOST);
        }
    }
    return 0;
}
