// NXStream - montador de quadros H.264 a partir dos pacotes RTP (v0.12.1)
//
// Substitui o depacketizer da libdatachannel, que entregava o quadro assim que
// chegava o pacote com o marcador de fim, sem esperar pacotes atrasados e sem
// pedir os perdidos. Na rede de casa isso não aparecia; pela internet (xCloud,
// log de 25/11) virou 3751 erros de decodificação em 4 min.
//
// O que faz:
// - guarda os pacotes por número de sequência (janela de 1024) e só entrega um
//   quadro quando todos os pacotes dele chegaram, na ordem;
// - pacote faltando: pede de novo (RTCP NACK, RFC 4585) até 3 vezes;
// - se não vier a tempo, descarta o quadro, pede um quadro-chave e não entrega
//   nada até ele chegar (o decodificador não recebe quadro quebrado).
//
// Sem rede e sem threads: testável no PC (tests/test_montador.c). Chamado só
// pela thread que recebe o vídeo.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    // Quadro completo (Annex B, start codes de 4 bytes). idr = tem IDR ou SPS.
    void (*quadro)(const uint8_t *dados, size_t tam, uint32_t timestamp, bool idr, void *u);
    // Pacote RTCP pronto para enviar (NACK)
    void (*rtcp)(const uint8_t *dados, size_t tam, void *u);
    // Pedir um quadro-chave ao servidor (PLI/FIR)
    void (*pedir_chave)(void *u);
    void *u;
} MontadorSaida;

typedef struct {
    unsigned long pacotes;          // pacotes RTP recebidos
    unsigned long perdidos;         // números de sequência que faltaram (ao detectar)
    unsigned long recuperados;      // chegaram depois de um NACK
    unsigned long fora_de_ordem;    // chegaram depois de um maior (sem precisar de NACK)
    unsigned long atrasados;        // chegaram depois do quadro já entregue/descartado
    unsigned long duplicados;
    unsigned long nacks;            // pacotes RTCP NACK enviados
    unsigned long quadros;          // quadros entregues
    unsigned long quadros_perdidos; // quadros descartados por falta de pacote
    unsigned long esperando_chave;  // quadros descartados esperando um quadro-chave
    unsigned long pedidos_chave;
} MontadorEstat;

typedef struct Montador Montador;

Montador *montador_criar(MontadorSaida saida);
void montador_liberar(Montador *m);
// Um pacote RTP já decifrado. agora = segundos (relógio monotônico).
void montador_pacote(Montador *m, const uint8_t *pacote, size_t tam, double agora);
void montador_estat(const Montador *m, MontadorEstat *e);

// Tempos (segundos), expostos para os testes
#define MONTADOR_NACK_PRIMEIRO 0.010 // espera um pouco: pode ser só fora de ordem
#define MONTADOR_NACK_REPETIR  0.050
#define MONTADOR_NACK_MAX      3
#define MONTADOR_DESISTIR      0.160 // sem o pacote até aqui: quadro perdido
#define MONTADOR_PLI_INTERVALO 0.250
