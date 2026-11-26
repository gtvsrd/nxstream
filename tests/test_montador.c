// Testes do montador de quadros RTP -> H.264 (source/net/montador.c)
#include "net/montador.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_total, g_falhas;
#define CHECAR(c)                                                                \
    do {                                                                         \
        g_total++;                                                               \
        if (!(c)) {                                                              \
            g_falhas++;                                                          \
            printf("  FALHOU (linha %d): %s\n", __LINE__, #c);                   \
        }                                                                        \
    } while (0)

// --- Saída capturada ---
static int g_quadros, g_idrs, g_rtcps, g_plis;
static uint8_t g_ultimo[4096];
static size_t g_ultimo_tam;
static uint32_t g_ultimo_ts;
static uint8_t g_rtcp[512];
static size_t g_rtcp_tam;

static void ao_quadro(const uint8_t *d, size_t n, uint32_t ts, bool idr, void *u) {
    (void)u;
    g_quadros++;
    g_idrs += idr;
    g_ultimo_tam = n < sizeof g_ultimo ? n : sizeof g_ultimo;
    memcpy(g_ultimo, d, g_ultimo_tam);
    g_ultimo_ts = ts;
}
static void ao_rtcp(const uint8_t *d, size_t n, void *u) {
    (void)u;
    g_rtcps++;
    g_rtcp_tam = n < sizeof g_rtcp ? n : sizeof g_rtcp;
    memcpy(g_rtcp, d, g_rtcp_tam);
}
static void ao_pli(void *u) {
    (void)u;
    g_plis++;
}

static Montador *novo(void) {
    g_quadros = g_idrs = g_rtcps = g_plis = 0;
    g_ultimo_tam = g_rtcp_tam = 0;
    return montador_criar((MontadorSaida){ao_quadro, ao_rtcp, ao_pli, NULL});
}

// Pacote RTP: cabeçalho de 12 bytes + carga
static size_t rtp(uint8_t *b, uint16_t seq, uint32_t ts, bool marcador, const uint8_t *carga, size_t n) {
    b[0] = 0x80;
    b[1] = (uint8_t)((marcador ? 0x80 : 0) | 102);
    b[2] = (uint8_t)(seq >> 8); b[3] = (uint8_t)seq;
    b[4] = (uint8_t)(ts >> 24); b[5] = (uint8_t)(ts >> 16); b[6] = (uint8_t)(ts >> 8); b[7] = (uint8_t)ts;
    b[8] = 0x12; b[9] = 0x34; b[10] = 0x56; b[11] = 0x78;
    memcpy(b + 12, carga, n);
    return 12 + n;
}

static void enviar(Montador *m, uint16_t seq, uint32_t ts, bool marcador, const uint8_t *c, size_t n, double t) {
    uint8_t b[1600];
    montador_pacote(m, b, rtp(b, seq, ts, marcador, c, n), t);
}

// Quadro IDR em 3 FU-A: [S] 0xAA 0xAA | [meio] 0xBB | [E] 0xCC
static const uint8_t FU1[] = {0x7C, 0x85, 0xAA, 0xAA};
static const uint8_t FU2[] = {0x7C, 0x05, 0xBB};
static const uint8_t FU3[] = {0x7C, 0x45, 0xCC};
static const uint8_t IDR_ESPERADO[] = {0, 0, 0, 1, 0x65, 0xAA, 0xAA, 0xBB, 0xCC};
static const uint8_t P[] = {0x41, 0x9A, 0x01};  // quadro P de uma NAL

static void idr(Montador *m, uint16_t seq, uint32_t ts, double t) {
    enviar(m, seq, ts, false, FU1, sizeof FU1, t);
    enviar(m, (uint16_t)(seq + 1), ts, false, FU2, sizeof FU2, t);
    enviar(m, (uint16_t)(seq + 2), ts, true, FU3, sizeof FU3, t);
}

int main(void) {
    puts("montador: em ordem");
    Montador *m = novo();
    idr(m, 100, 9000, 0);
    CHECAR(g_quadros == 1 && g_idrs == 1);
    CHECAR(g_ultimo_tam == sizeof IDR_ESPERADO && !memcmp(g_ultimo, IDR_ESPERADO, sizeof IDR_ESPERADO));
    CHECAR(g_ultimo_ts == 9000);
    enviar(m, 103, 10500, true, P, sizeof P, 0.016);
    CHECAR(g_quadros == 2 && g_ultimo_tam == 7 && g_ultimo[4] == 0x41);
    CHECAR(g_rtcps == 0 && g_plis == 0);
    montador_liberar(m);

    puts("montador: começa sem quadro-chave");
    m = novo();
    enviar(m, 7, 100, true, P, sizeof P, 0);     // P antes do primeiro IDR: descartado
    CHECAR(g_quadros == 0);
    idr(m, 8, 200, 0.01);
    CHECAR(g_quadros == 1 && g_idrs == 1);
    MontadorEstat e;
    montador_estat(m, &e);
    CHECAR(e.esperando_chave == 1);
    montador_liberar(m);

    puts("montador: fora de ordem dentro do quadro");
    m = novo();
    enviar(m, 200, 1, false, FU1, sizeof FU1, 0);
    enviar(m, 202, 1, true, FU3, sizeof FU3, 0.001);   // marcador antes do meio
    CHECAR(g_quadros == 0);                            // espera o 201
    enviar(m, 201, 1, false, FU2, sizeof FU2, 0.002);
    CHECAR(g_quadros == 1 && !memcmp(g_ultimo, IDR_ESPERADO, sizeof IDR_ESPERADO));
    CHECAR(g_rtcps == 0);                              // chegou antes do 1º NACK
    montador_estat(m, &e);
    CHECAR(e.fora_de_ordem == 1 && e.recuperados == 0 && e.quadros_perdidos == 0);
    montador_liberar(m);

    puts("montador: perdido, NACK e recuperado");
    m = novo();
    idr(m, 300, 1, 0);
    enviar(m, 303, 2, false, FU1, sizeof FU1, 0.020); // 304 e 306 vão faltar
    enviar(m, 305, 2, false, FU2, sizeof FU2, 0.020);
    enviar(m, 307, 2, true, FU3, sizeof FU3, 0.021);
    CHECAR(g_rtcps == 0);                             // < 10 ms desde os buracos
    enviar(m, 308, 3, true, P, sizeof P, 0.035);      // próximo quadro: hora do NACK
    CHECAR(g_rtcps == 1 && g_rtcp_tam == 16);         // um NACK com uma entrada
    CHECAR(g_rtcp[0] == 0x81 && g_rtcp[1] == 205 && g_rtcp[2] == 0 && g_rtcp[3] == 3);
    CHECAR(g_rtcp[8] == 0x12 && g_rtcp[9] == 0x34 && g_rtcp[10] == 0x56 && g_rtcp[11] == 0x78);
    CHECAR(g_rtcp[12] == 0x01 && g_rtcp[13] == 0x30);  // PID 304
    CHECAR(g_rtcp[14] == 0x00 && g_rtcp[15] == 0x02);  // BLP: 306 (304 + 2)
    CHECAR(g_quadros == 1);                           // o 308 espera o quadro de antes
    enviar(m, 304, 2, false, FU2, sizeof FU2, 0.060);
    enviar(m, 306, 2, false, FU2, sizeof FU2, 0.061);
    CHECAR(g_quadros == 3 && g_plis == 0);            // quadro 2 e, em seguida, o 3
    montador_estat(m, &e);
    CHECAR(e.perdidos == 2 && e.recuperados == 2 && e.nacks == 1);
    montador_liberar(m);

    puts("montador: NACK repetido e desistência");
    m = novo();
    idr(m, 400, 1, 0);
    enviar(m, 404, 2, true, P, sizeof P, 0.016);      // 403 faltando (quadro 2 quebrado)
    enviar(m, 405, 3, true, P, sizeof P, 0.033);      // 1º NACK
    enviar(m, 406, 4, true, P, sizeof P, 0.090);      // 2º NACK (50 ms depois)
    CHECAR(g_rtcps == 2 && g_quadros == 1);
    enviar(m, 407, 5, true, P, sizeof P, 0.200);      // passou do limite: desiste
    CHECAR(g_plis == 1);
    CHECAR(g_quadros == 1);                           // P sem referência não vai ao decodificador
    enviar(m, 403, 2, true, P, sizeof P, 0.210);      // chegou tarde demais
    idr(m, 408, 6, 0.250);                            // quadro-chave pedido chega
    CHECAR(g_quadros == 2 && g_idrs == 2);
    enviar(m, 411, 7, true, P, sizeof P, 0.266);
    CHECAR(g_quadros == 3);
    montador_estat(m, &e);
    CHECAR(e.quadros_perdidos == 1 && e.atrasados == 1 && e.pedidos_chave == 1);
    CHECAR(e.esperando_chave == 3);                   // 405, 406 e 407 descartados até o IDR
    montador_liberar(m);

    puts("montador: virada da sequência (65535 -> 0)");
    m = novo();
    idr(m, 65534, 1, 0);                              // 65534, 65535, 0
    CHECAR(g_quadros == 1 && !memcmp(g_ultimo, IDR_ESPERADO, sizeof IDR_ESPERADO));
    enviar(m, 2, 2, true, P, sizeof P, 0.016);        // 1 faltando
    enviar(m, 1, 2, false, P, sizeof P, 0.017);
    CHECAR(g_quadros == 2 && g_ultimo_tam == 14);     // duas NALs
    montador_liberar(m);

    puts("montador: STAP-A, duplicado e preenchimento");
    m = novo();
    static const uint8_t STAP[] = {0x78, 0x00, 0x02, 0x67, 0x42, 0x00, 0x02, 0x68, 0xCE, 0x00, 0x02, 0x65, 0x88};
    enviar(m, 10, 1, true, STAP, sizeof STAP, 0);
    CHECAR(g_quadros == 1 && g_idrs == 1 && g_ultimo_tam == 18);
    CHECAR(!memcmp(g_ultimo, "\0\0\0\1\x67\x42\0\0\0\1\x68\xCE\0\0\0\1\x65\x88", 18));
    enviar(m, 10, 1, true, STAP, sizeof STAP, 0.001); // repetido: já entregue
    uint8_t vazio[1];
    enviar(m, 11, 1, false, vazio, 0, 0.002);         // só cabeçalho (sonda de banda)
    enviar(m, 12, 2, true, P, sizeof P, 0.016);
    CHECAR(g_quadros == 2 && g_ultimo_tam == 7);
    montador_estat(m, &e);
    CHECAR(e.atrasados == 1);
    montador_liberar(m);

    puts("montador: RTCP ignorado");
    m = novo();
    uint8_t sr[28] = {0x80, 200, 0, 6};               // Sender Report
    montador_pacote(m, sr, sizeof sr, 0);
    montador_estat(m, &e);
    CHECAR(e.pacotes == 0);
    montador_liberar(m);

    printf("%d de %d verificacoes passaram\n", g_total - g_falhas, g_total);
    return g_falhas ? 1 : 0;
}
