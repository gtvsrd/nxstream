// pipe() para o Switch: a libnx não implementa. A libjuice usa um pipe só para
// acordar a própria thread de rede (escreve 1 byte para interromper o poll()).
// Aqui o "pipe" é um par de sockets UDP conectados no endereço local, a mesma
// solução que a libjuice usa no Windows, que também não tem pipe().
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int socket_local(struct sockaddr_in *endereco) {
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0)
        return -1;
    memset(endereco, 0, sizeof *endereco);
    endereco->sin_family = AF_INET;
#ifdef __SWITCH__
    endereco->sin_len = sizeof *endereco;
#endif
    endereco->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    endereco->sin_port = 0; // porta escolhida pelo sistema
    socklen_t tam = sizeof *endereco;
    if (bind(s, (struct sockaddr *)endereco, sizeof *endereco) < 0 ||
        getsockname(s, (struct sockaddr *)endereco, &tam) < 0) {
        int erro = errno;
        close(s);
        errno = erro;
        return -1;
    }
    return s;
}

int pipe(int fds[2]) {
    struct sockaddr_in end_leitura, end_escrita;
    int leitura = socket_local(&end_leitura);
    if (leitura < 0)
        return -1;
    int escrita = socket_local(&end_escrita);
    if (escrita < 0) {
        int erro = errno;
        close(leitura);
        errno = erro;
        return -1;
    }
    // Cada ponta só aceita datagramas da outra
    if (connect(escrita, (struct sockaddr *)&end_leitura, sizeof end_leitura) < 0 ||
        connect(leitura, (struct sockaddr *)&end_escrita, sizeof end_escrita) < 0) {
        int erro = errno;
        close(leitura);
        close(escrita);
        errno = erro;
        return -1;
    }
    fds[0] = leitura; // ponta de leitura
    fds[1] = escrita; // ponta de escrita
    return 0;
}
