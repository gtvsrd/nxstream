// NXStream - áudio do console (ver audio.h)
#include "media/audio.h"

#include <malloc.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "core/log.h"

#if defined(TEM_LIBOPUS)
#include <opus/opus.h>
#else
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#endif

#define TAXA        48000
#define CANAIS      2
#define BLOCO       960                    // 20 ms por bloco de saída
#define NUM_BLOCOS  3                      // blocos na fila do audout
#define FILA_MAX    (TAXA / 5)             // 200 ms de capacidade
#define FILA_ALVO   (TAXA * 60 / 1000)     // acima de ~60 ms + 2 blocos: corta o excesso
#define MAX_QUADRO  5760                   // 120 ms (maior quadro Opus)

static pthread_mutex_t g_trava = PTHREAD_MUTEX_INITIALIZER;
static AudioEstado g_estado;
static bool g_rodando;
static pthread_t g_thread;

// Fila circular de amostras (pares L/R int16)
static int16_t g_fila[FILA_MAX * CANAIS];
static int g_ini, g_qtd;                   // em quadros (pares)

static bool g_tem_seq;
static uint16_t g_ultima_seq;
static int16_t g_pcm[MAX_QUADRO * CANAIS];

static void *g_mem_saida;                  // blocos do audout (alinhados a 0x1000)
static AudioOutBuffer g_blocos[NUM_BLOCOS];
static bool g_audout;

#if defined(TEM_LIBOPUS)
static OpusDecoder *g_opus;
#else
static AVCodecContext *g_ctx;
static AVPacket *g_pkt;
static AVFrame *g_quadro;
#endif

static void enfileirar(const int16_t *pcm, int quadros) {
    pthread_mutex_lock(&g_trava);
    for (int i = 0; i < quadros; i++) {
        if (g_qtd == FILA_MAX) { // cheia: o mais antigo sai
            g_ini = (g_ini + 1) % FILA_MAX;
            g_qtd--;
            g_estado.descartados_ms++; // contado por quadro; convertido em ms ao ler
        }
        int pos = (g_ini + g_qtd) % FILA_MAX;
        g_fila[pos * 2] = pcm[i * 2];
        g_fila[pos * 2 + 1] = pcm[i * 2 + 1];
        g_qtd++;
    }
    pthread_mutex_unlock(&g_trava);
}

// Decodifica um pacote Opus (ou, com dados NULL, esconde uma perda). Retorna quadros.
static int decodificar(const uint8_t *dados, int tamanho) {
#if defined(TEM_LIBOPUS)
    int n = opus_decode(g_opus, dados, dados ? tamanho : 0, g_pcm, MAX_QUADRO, 0);
    return n < 0 ? -1 : n;
#else
    if (!dados) return 0; // o FFmpeg não esconde perdas por aqui
    g_pkt->data = (uint8_t *)dados;
    g_pkt->size = tamanho;
    if (avcodec_send_packet(g_ctx, g_pkt) < 0) return -1;
    int total = 0;
    while (avcodec_receive_frame(g_ctx, g_quadro) >= 0) {
        int n = g_quadro->nb_samples;
        int ch = g_quadro->ch_layout.nb_channels;
        if (total + n > MAX_QUADRO) n = MAX_QUADRO - total;
        // Saída do Opus do FFmpeg: float planar (fltp)
        const float *esq = (const float *)g_quadro->data[0];
        const float *dir = ch > 1 ? (const float *)g_quadro->data[1] : esq;
        for (int i = 0; i < n; i++) {
            float l = esq[i] * 32767.0f, r = dir[i] * 32767.0f;
            g_pcm[(total + i) * 2] = (int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
            g_pcm[(total + i) * 2 + 1] = (int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
        }
        total += n;
        av_frame_unref(g_quadro);
    }
    return total;
#endif
}

void audio_receber_rtp(const uint8_t *d, int n) {
    if (!g_rodando || n < 12 || (d[0] >> 6) != 2) return;
    int cc = d[0] & 0x0F;
    bool ext = d[0] & 0x10, preench = d[0] & 0x20;
    uint16_t seq = (uint16_t)(d[2] << 8 | d[3]);
    int ofs = 12 + 4 * cc;
    if (ext) {
        if (n < ofs + 4) return;
        ofs += 4 + 4 * (d[ofs + 2] << 8 | d[ofs + 3]);
    }
    int fim = n;
    if (preench && n > 0) fim -= d[n - 1];
    if (ofs >= fim) return;

    int perdidos = 0;
    if (g_tem_seq) {
        int16_t dif = (int16_t)(seq - g_ultima_seq);
        if (dif <= 0) return;           // repetido ou atrasado: ignora
        perdidos = dif - 1;
    }
    g_tem_seq = true;
    g_ultima_seq = seq;

    // Perdas curtas: o Opus inventa um trecho parecido (melhor que um estalo)
    for (int i = 0; i < perdidos && i < 3; i++) {
        int q = decodificar(NULL, 0);
        if (q > 0) enfileirar(g_pcm, q);
    }
    int q = decodificar(d + ofs, fim - ofs);
    pthread_mutex_lock(&g_trava);
    g_estado.pacotes++;
    g_estado.perdidos += (unsigned long)perdidos;
    if (q < 0) g_estado.erros++;
#if defined(TEM_LIBOPUS)
    if (g_estado.pacotes == 1) g_estado.canais = opus_packet_get_nb_channels(d + ofs);
#else
    if (g_estado.pacotes == 1 && g_quadro) g_estado.canais = g_ctx->ch_layout.nb_channels;
#endif
    unsigned long pacotes = g_estado.pacotes, erros = g_estado.erros;
    pthread_mutex_unlock(&g_trava);
    if (q > 0) enfileirar(g_pcm, q);
    if (pacotes == 1)
        LOG("audio: primeiro pacote (%d bytes de Opus, %d amostras, %d canal(is))", fim - ofs, q, g_estado.canais);
    if (q < 0 && erros <= 5)
        LOG("audio: erro ao decodificar um pacote (%d bytes)", fim - ofs);
}

// Preenche um bloco de saída com o que houver na fila (o resto, silêncio)
static void preencher(int16_t *saida) {
    pthread_mutex_lock(&g_trava);
    // Atraso acumulado (a rede entregou de uma vez): corta o excesso para manter ~60 ms
    if (g_qtd > FILA_ALVO + 2 * BLOCO) {
        int corte = g_qtd - FILA_ALVO;
        g_ini = (g_ini + corte) % FILA_MAX;
        g_qtd -= corte;
        g_estado.descartados_ms += (unsigned long)corte;
    }
    int n = g_qtd < BLOCO ? g_qtd : BLOCO;
    for (int i = 0; i < n; i++) {
        int pos = (g_ini + i) % FILA_MAX;
        saida[i * 2] = g_fila[pos * 2];
        saida[i * 2 + 1] = g_fila[pos * 2 + 1];
    }
    g_ini = (g_ini + n) % FILA_MAX;
    g_qtd -= n;
    if (n < BLOCO && g_estado.pacotes > 0) g_estado.faltas++;
    pthread_mutex_unlock(&g_trava);
    if (n < BLOCO) memset(saida + n * 2, 0, (size_t)(BLOCO - n) * CANAIS * sizeof(int16_t));
}

static void *thread_saida(void *arg) {
    (void)arg;
    while (g_rodando) {
        AudioOutBuffer *liberado = NULL;
        u32 qtd = 0;
        Result rc = audoutWaitPlayFinish(&liberado, &qtd, 100000000ULL); // até 100 ms
        if (R_FAILED(rc) || !liberado || qtd == 0) continue;
        preencher((int16_t *)liberado->buffer);
        liberado->data_size = BLOCO * CANAIS * sizeof(int16_t);
        audoutAppendAudioOutBuffer(liberado);
    }
    return NULL;
}

bool audio_iniciar(void) {
    audio_encerrar();
    memset(&g_estado, 0, sizeof g_estado);
    g_ini = g_qtd = 0;
    g_tem_seq = false;

#if defined(TEM_LIBOPUS)
    int err = 0;
    g_opus = opus_decoder_create(TAXA, CANAIS, &err);
    if (!g_opus) {
        LOG("audio: libopus nao iniciou (%d)", err);
        return false;
    }
    snprintf(g_estado.descricao, sizeof g_estado.descricao, "%s", opus_get_version_string());
#else
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_OPUS);
    if (!codec) {
        LOG("audio: FFmpeg sem decodificador Opus; sem som");
        return false;
    }
    g_ctx = avcodec_alloc_context3(codec);
    g_pkt = av_packet_alloc();
    g_quadro = av_frame_alloc();
    if (!g_ctx || !g_pkt || !g_quadro) {
        audio_encerrar();
        return false;
    }
    g_ctx->sample_rate = TAXA;
    av_channel_layout_default(&g_ctx->ch_layout, CANAIS);
    if (avcodec_open2(g_ctx, codec, NULL) < 0) {
        LOG("audio: falha ao abrir o Opus do FFmpeg");
        audio_encerrar();
        return false;
    }
    snprintf(g_estado.descricao, sizeof g_estado.descricao, "FFmpeg %s", codec->name);
#endif

    // Saída de áudio do Switch: 48 kHz, 2 canais, 16 bits
    Result rc = audoutInitialize();
    if (R_FAILED(rc)) {
        LOG("audio: audoutInitialize falhou (0x%x)", rc);
        audio_encerrar();
        return false;
    }
    g_audout = true;
    rc = audoutStartAudioOut();
    if (R_FAILED(rc)) {
        LOG("audio: audoutStartAudioOut falhou (0x%x)", rc);
        audio_encerrar();
        return false;
    }
    size_t tam_bloco = (BLOCO * CANAIS * sizeof(int16_t) + 0xFFF) & ~(size_t)0xFFF;
    g_mem_saida = memalign(0x1000, tam_bloco * NUM_BLOCOS);
    if (!g_mem_saida) {
        audio_encerrar();
        return false;
    }
    memset(g_mem_saida, 0, tam_bloco * NUM_BLOCOS);
    for (int i = 0; i < NUM_BLOCOS; i++) {
        g_blocos[i].next = NULL;
        g_blocos[i].buffer = (uint8_t *)g_mem_saida + i * tam_bloco;
        g_blocos[i].buffer_size = tam_bloco;
        g_blocos[i].data_size = BLOCO * CANAIS * sizeof(int16_t);
        g_blocos[i].data_offset = 0;
        audoutAppendAudioOutBuffer(&g_blocos[i]); // começa com silêncio
    }

    g_rodando = true;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 64 * 1024);
    bool ok = pthread_create(&g_thread, &at, thread_saida, NULL) == 0;
    pthread_attr_destroy(&at);
    if (!ok) {
        g_rodando = false;
        LOG("audio: falha ao criar a thread de saida");
        audio_encerrar();
        return false;
    }
    g_estado.ativo = true;
    LOG("audio: pronto (%s, saida %u Hz, %u canais, blocos de %d ms)", g_estado.descricao, audoutGetSampleRate(),
        audoutGetChannelCount(), BLOCO * 1000 / TAXA);
    return true;
}

void audio_ler(AudioEstado *c) {
    pthread_mutex_lock(&g_trava);
    *c = g_estado;
    c->fila_ms = g_qtd * 1000 / TAXA;
    c->descartados_ms = g_estado.descartados_ms * 1000 / TAXA;
    pthread_mutex_unlock(&g_trava);
}

void audio_encerrar(void) {
    bool tinha = g_rodando;
    g_rodando = false;
    if (tinha) pthread_join(g_thread, NULL);
    if (g_audout) {
        audoutStopAudioOut();
        audoutExit();
        g_audout = false;
    }
    free(g_mem_saida);
    g_mem_saida = NULL;
#if defined(TEM_LIBOPUS)
    if (g_opus) opus_decoder_destroy(g_opus);
    g_opus = NULL;
#else
    avcodec_free_context(&g_ctx);
    av_packet_free(&g_pkt);
    av_frame_free(&g_quadro);
#endif
    if (tinha || g_estado.pacotes)
        LOG("audio: encerrado (%lu pacotes, %lu perdidos, %lu erros, %lu faltas)", g_estado.pacotes,
            g_estado.perdidos, g_estado.erros, g_estado.faltas);
    g_estado.ativo = false;
}
