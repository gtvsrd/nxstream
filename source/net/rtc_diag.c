#include "rtc_diag.h"
#include "../core/log.h"
#include "rtc_comum.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <rtc/rtc.h>
#include <switch.h>

// Os callbacks da libdatachannel rodam em threads dela: todo acesso ao estado
// passa pelo mutex, e a tela lê uma cópia com rtc_diag_ler().
static pthread_mutex_t g_trava = PTHREAD_MUTEX_INITIALIZER;
static RtcDiagEstado g_estado;
static u64 g_inicio;
static int g_pc = -1;

static double agora_s(void) {
    return armTicksToNs(armGetSystemTick() - g_inicio) / 1e9;
}

static void ao_descricao(int pc, const char *sdp, const char *tipo, void *ptr) {
    (void)pc; (void)ptr;
    pthread_mutex_lock(&g_trava);
    g_estado.oferta_pronta = true;
    g_estado.t_oferta = agora_s();
    g_estado.tamanho_oferta = (int)strlen(sdp);
    g_estado.tem_fingerprint = strstr(sdp, "a=fingerprint:") != NULL;
    g_estado.tem_video = strstr(sdp, "m=video") != NULL;
    g_estado.tem_audio = strstr(sdp, "m=audio") != NULL;
    g_estado.tem_dados = strstr(sdp, "m=application") != NULL;
    double t = g_estado.t_oferta;
    int tam = g_estado.tamanho_oferta;
    pthread_mutex_unlock(&g_trava);
    LOG("rtc_diag: descricao local (%s) pronta em %.2f s, %d bytes", tipo, t, tam);
    LOG("rtc_diag: SDP:\n%s", sdp); // SDP local não contém segredos de conta
}

// Extrai "IP porta tipo" de "candidate:1 1 UDP 2122317823 192.168.0.10 50000 typ host ..."
static void resumir_candidato(const char *cand, char *saida, size_t tam) {
    char fundacao[32], transporte[8], ip[64], tipo[16];
    unsigned componente, porta;
    unsigned long prioridade;
    const char *c = strncmp(cand, "a=", 2) == 0 ? cand + 2 : cand;
    if (sscanf(c, "candidate:%31s %u %7s %lu %63s %u typ %15s",
               fundacao, &componente, transporte, &prioridade, ip, &porta, tipo) == 7)
        snprintf(saida, tam, "%s porta %u (%s, %s)", ip, porta, tipo, transporte);
    else
        snprintf(saida, tam, "%.60s", c);
}

static void ao_candidato(int pc, const char *cand, const char *mid, void *ptr) {
    (void)pc; (void)ptr;
    LOG("rtc_diag: candidato local [%s]: %s", mid ? mid : "-", cand);
    pthread_mutex_lock(&g_trava);
    if (g_estado.num_candidatos < RTC_DIAG_MAX_CANDIDATOS) {
        resumir_candidato(cand, g_estado.candidatos[g_estado.num_candidatos],
                          sizeof g_estado.candidatos[0]);
        g_estado.num_candidatos++;
    }
    pthread_mutex_unlock(&g_trava);
}

static void ao_estado_coleta(int pc, rtcGatheringState estado, void *ptr) {
    (void)pc; (void)ptr;
    if (estado != RTC_GATHERING_COMPLETE)
        return;
    pthread_mutex_lock(&g_trava);
    g_estado.coleta_completa = true;
    g_estado.t_coleta = agora_s();
    double t = g_estado.t_coleta;
    int n = g_estado.num_candidatos;
    pthread_mutex_unlock(&g_trava);
    LOG("rtc_diag: coleta de candidatos completa em %.2f s (%d candidatos)", t, n);
}

static void definir_erro(const char *texto, int codigo) {
    pthread_mutex_lock(&g_trava);
    snprintf(g_estado.erro, sizeof g_estado.erro, "%s (codigo %d)", texto, codigo);
    pthread_mutex_unlock(&g_trava);
    LOG("rtc_diag: %s (codigo %d)", texto, codigo);
}

// Faixa de payload types e ids usados só para o diagnóstico (o Xbox define os reais)
static bool adicionar_trilha(rtcDirection direcao, rtcCodec codec, int payload,
                             uint32_t ssrc, const char *mid) {
    rtcTrackInit t;
    memset(&t, 0, sizeof t);
    t.direction = direcao;
    t.codec = codec;
    t.payloadType = payload;
    t.ssrc = ssrc;
    t.mid = mid;
    int tr = rtcAddTrackEx(g_pc, &t);
    if (tr < 0) {
        definir_erro(mid[0] == 'v' ? "Falha ao adicionar a trilha de video"
                                   : "Falha ao adicionar a trilha de audio", tr);
        return false;
    }
    return true;
}

bool rtc_diag_iniciar(void) {
    rtc_diag_encerrar();
    pthread_mutex_lock(&g_trava);
    memset(&g_estado, 0, sizeof g_estado);
    pthread_mutex_unlock(&g_trava);
    g_inicio = armGetSystemTick();

    rtc_comum_iniciar();
    LOG("rtc_diag: iniciando");

    rtcConfiguration cfg;
    memset(&cfg, 0, sizeof cfg);
    // Sem servidores STUN/TURN: na rede local bastam os candidatos "host".
    // Negociação manual: a oferta só é gerada depois de todas as trilhas existirem.
    cfg.disableAutoNegotiation = true;

    g_pc = rtcCreatePeerConnection(&cfg);
    if (g_pc < 0) {
        definir_erro("Falha ao criar a conexao WebRTC", g_pc);
        return false;
    }
    pthread_mutex_lock(&g_trava);
    g_estado.iniciado = true;
    pthread_mutex_unlock(&g_trava);

    rtcSetLocalDescriptionCallback(g_pc, ao_descricao);
    rtcSetLocalCandidateCallback(g_pc, ao_candidato);
    rtcSetGatheringStateChangeCallback(g_pc, ao_estado_coleta);

    if (!adicionar_trilha(RTC_DIRECTION_RECVONLY, RTC_CODEC_H264, 102, 1, "video") ||
        !adicionar_trilha(RTC_DIRECTION_RECVONLY, RTC_CODEC_OPUS, 111, 2, "audio"))
        return true; // a conexão existe; o erro aparece na tela

    int dc = rtcCreateDataChannel(g_pc, "input");
    if (dc < 0) {
        definir_erro("Falha ao criar o canal de dados", dc);
        return true;
    }

    int rc = rtcSetLocalDescription(g_pc, "offer");
    if (rc < 0)
        definir_erro("Falha ao gerar a oferta SDP", rc);
    return true;
}

void rtc_diag_ler(RtcDiagEstado *copia) {
    pthread_mutex_lock(&g_trava);
    *copia = g_estado;
    pthread_mutex_unlock(&g_trava);
    copia->segundos = agora_s();
}

void rtc_diag_encerrar(void) {
    if (g_pc >= 0) {
        rtcDeletePeerConnection(g_pc);
        LOG("rtc_diag: conexao encerrada");
        g_pc = -1;
    }
}

void rtc_diag_finalizar_biblioteca(void) {
    rtc_diag_encerrar();
}
