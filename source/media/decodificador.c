#include "decodificador.h"
#include "../core/log.h"

#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

// Fila de quadros entre a thread de rede e a de decodificação. Pequena de
// propósito: se encher, é porque a decodificação não acompanha, e acumular só
// aumentaria o atraso. Nesse caso tudo é descartado até o próximo quadro-chave.
#define FILA_TAMANHO 8

typedef struct {
    uint8_t *dados;
    size_t   tamanho;
    bool     chave;
} Item;

static pthread_mutex_t g_trava = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_tem_item = PTHREAD_COND_INITIALIZER;
static Item g_fila[FILA_TAMANHO];
static int  g_inicio = 0, g_quantos = 0;
static bool g_rodando = false;
static bool g_esperando_chave = true; // nada é decodificável antes do 1º quadro-chave
static pthread_t g_thread;
static DecodEstado g_estado;

static AVCodecContext *g_ctx = NULL;
static AVBufferRef *g_dispositivo = NULL;
static AVPacket *g_pacote = NULL;
static AVFrame *g_quadro = NULL;

// Janela para o tempo médio de decodificação
#define JANELA 60
static double g_tempos[JANELA];
static int g_pos_tempo = 0, g_num_tempos = 0;

static void esvaziar_fila_trancada(void) {
    while (g_quantos > 0) {
        free(g_fila[g_inicio].dados);
        g_inicio = (g_inicio + 1) % FILA_TAMANHO;
        g_quantos--;
    }
    g_inicio = 0;
}

void decod_enviar(const uint8_t *dados, size_t tamanho, bool quadro_chave) {
    pthread_mutex_lock(&g_trava);
    if (!g_rodando) {
        pthread_mutex_unlock(&g_trava);
        return;
    }
    if (g_esperando_chave && !quadro_chave) {
        g_estado.descartados++;
        g_estado.esperando_chave++;
        pthread_mutex_unlock(&g_trava);
        return;
    }
    if (g_quantos == FILA_TAMANHO) {
        // A decodificação não acompanhou: joga fora o que está na fila e o
        // quadro atual, e espera um quadro-chave para recomeçar sem artefatos
        g_estado.descartados += (unsigned long)g_quantos + 1;
        esvaziar_fila_trancada();
        g_esperando_chave = true;
        g_estado.precisa_chave = true;
        pthread_mutex_unlock(&g_trava);
        LOG("decod: fila cheia, quadros descartados ate o proximo quadro-chave");
        return;
    }
    // O FFmpeg exige uma margem de zeros no fim do pacote
    uint8_t *copia = malloc(tamanho + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!copia) {
        g_estado.descartados++;
        pthread_mutex_unlock(&g_trava);
        return;
    }
    memcpy(copia, dados, tamanho);
    memset(copia + tamanho, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    if (quadro_chave)
        g_esperando_chave = false;
    int pos = (g_inicio + g_quantos) % FILA_TAMANHO;
    g_fila[pos] = (Item){copia, tamanho, quadro_chave};
    g_quantos++;
    g_estado.fila = g_quantos;
    pthread_cond_signal(&g_tem_item);
    pthread_mutex_unlock(&g_trava);
}

static void registrar_tempo(double ms) {
    g_tempos[g_pos_tempo] = ms;
    g_pos_tempo = (g_pos_tempo + 1) % JANELA;
    if (g_num_tempos < JANELA)
        g_num_tempos++;
    double soma = 0, maximo = 0;
    for (int i = 0; i < g_num_tempos; i++) {
        soma += g_tempos[i];
        if (g_tempos[i] > maximo)
            maximo = g_tempos[i];
    }
    g_estado.ms_ultimo = ms;
    g_estado.ms_medio = soma / g_num_tempos;
    g_estado.ms_maximo = maximo;
}

static void decodificar(Item *item) {
    g_pacote->data = item->dados;
    g_pacote->size = (int)item->tamanho;
    g_pacote->flags = item->chave ? AV_PKT_FLAG_KEY : 0;

    u64 inicio = armGetSystemTick();
    int rc = avcodec_send_packet(g_ctx, g_pacote);
    int saidas = 0;
    if (rc >= 0) {
        while ((rc = avcodec_receive_frame(g_ctx, g_quadro)) >= 0) {
            saidas++;
            pthread_mutex_lock(&g_trava);
            g_estado.decodificados++;
            bool primeiro = g_estado.decodificados == 1;
            g_estado.largura = g_quadro->width;
            g_estado.altura = g_quadro->height;
            const char *nome = av_get_pix_fmt_name((enum AVPixelFormat)g_quadro->format);
            snprintf(g_estado.formato, sizeof g_estado.formato, "%s", nome ? nome : "?");
            pthread_mutex_unlock(&g_trava);
            if (primeiro)
                LOG("decod: primeiro quadro decodificado %dx%d (%s)", g_quadro->width, g_quadro->height,
                    nome ? nome : "?");
            av_frame_unref(g_quadro); // fase 2c.3: aqui o quadro irá para a tela
        }
    }
    double ms = armTicksToNs(armGetSystemTick() - inicio) / 1e6;
    // Sem saída não é erro (o decodificador pode segurar um quadro); EAGAIN e EOF também não
    bool erro = rc < 0 && rc != AVERROR(EAGAIN) && rc != AVERROR_EOF;

    pthread_mutex_lock(&g_trava);
    g_estado.enviados++;
    if (saidas > 0)
        registrar_tempo(ms);
    unsigned long erros = 0;
    if (erro) {
        g_estado.erros++;
        erros = g_estado.erros;
        g_estado.precisa_chave = true;
    }
    pthread_mutex_unlock(&g_trava);

    if (erro && erros <= 10) {
        char texto[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(rc, texto, sizeof texto);
        LOG("decod: erro ao decodificar (%d: %s)", rc, texto);
    }
}

static void *thread_decodificacao(void *arg) {
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&g_trava);
        while (g_rodando && g_quantos == 0)
            pthread_cond_wait(&g_tem_item, &g_trava);
        if (!g_rodando) {
            pthread_mutex_unlock(&g_trava);
            break;
        }
        Item item = g_fila[g_inicio];
        g_inicio = (g_inicio + 1) % FILA_TAMANHO;
        g_quantos--;
        g_estado.fila = g_quantos;
        pthread_mutex_unlock(&g_trava);

        decodificar(&item);
        free(item.dados);
    }
    return NULL;
}

static enum AVPixelFormat escolher_formato(AVCodecContext *ctx, const enum AVPixelFormat *formatos) {
    (void)ctx;
    for (const enum AVPixelFormat *f = formatos; *f != AV_PIX_FMT_NONE; f++)
        if (*f == AV_PIX_FMT_NVTEGRA)
            return *f;
    LOG("decod: o decodificador nao ofereceu o formato NVTEGRA; usando %s",
        av_get_pix_fmt_name(formatos[0]) ? av_get_pix_fmt_name(formatos[0]) : "?");
    return formatos[0];
}

bool decod_iniciar(void) {
    decod_encerrar();
    pthread_mutex_lock(&g_trava);
    memset(&g_estado, 0, sizeof g_estado);
    pthread_mutex_unlock(&g_trava);
    g_num_tempos = g_pos_tempo = 0;

    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        LOG("decod: FFmpeg sem decodificador H.264");
        return false;
    }
    g_ctx = avcodec_alloc_context3(codec);
    g_pacote = av_packet_alloc();
    g_quadro = av_frame_alloc();
    if (!g_ctx || !g_pacote || !g_quadro) {
        LOG("decod: sem memoria para o decodificador");
        decod_encerrar();
        return false;
    }

    int rc = av_hwdevice_ctx_create(&g_dispositivo, AV_HWDEVICE_TYPE_NVTEGRA, NULL, NULL, 0);
    if (rc < 0) {
        char texto[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(rc, texto, sizeof texto);
        LOG("decod: NVDEC indisponivel (%s); decodificando pela CPU", texto);
    } else {
        g_ctx->hw_device_ctx = av_buffer_ref(g_dispositivo);
        g_ctx->get_format = escolher_formato;
        g_estado.hardware = true;
    }

    // Streaming: entregar cada quadro assim que possível, sem acumular
    g_ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    g_ctx->flags2 |= AV_CODEC_FLAG2_FAST;
    g_ctx->thread_count = 1;

    rc = avcodec_open2(g_ctx, codec, NULL);
    if (rc < 0) {
        char texto[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(rc, texto, sizeof texto);
        LOG("decod: falha ao abrir o decodificador (%s)", texto);
        decod_encerrar();
        return false;
    }
    snprintf(g_estado.descricao, sizeof g_estado.descricao, "%s%s (FFmpeg %s)", codec->name,
             g_estado.hardware ? " + nvtegra" : " (CPU)", av_version_info());

    pthread_mutex_lock(&g_trava);
    g_inicio = g_quantos = 0;
    g_esperando_chave = true;
    g_rodando = true;
    pthread_mutex_unlock(&g_trava);
    if (pthread_create(&g_thread, NULL, thread_decodificacao, NULL) != 0) {
        pthread_mutex_lock(&g_trava);
        g_rodando = false;
        pthread_mutex_unlock(&g_trava);
        LOG("decod: falha ao criar a thread de decodificacao");
        decod_encerrar();
        return false;
    }
    g_estado.ativo = true;
    LOG("decod: pronto: %s", g_estado.descricao);
    return true;
}

void decod_ler(DecodEstado *copia) {
    pthread_mutex_lock(&g_trava);
    *copia = g_estado;
    pthread_mutex_unlock(&g_trava);
}

void decod_limpar_pedido_chave(void) {
    pthread_mutex_lock(&g_trava);
    g_estado.precisa_chave = false;
    pthread_mutex_unlock(&g_trava);
}

void decod_encerrar(void) {
    pthread_mutex_lock(&g_trava);
    bool tinha_thread = g_rodando;
    g_rodando = false;
    pthread_cond_broadcast(&g_tem_item);
    pthread_mutex_unlock(&g_trava);
    if (tinha_thread)
        pthread_join(g_thread, NULL); // a thread termina o quadro atual e sai

    pthread_mutex_lock(&g_trava);
    esvaziar_fila_trancada();
    g_estado.ativo = false;
    pthread_mutex_unlock(&g_trava);

    if (g_ctx || g_dispositivo)
        LOG("decod: encerrado (%lu decodificados, %lu erros, %lu descartados)", g_estado.decodificados,
            g_estado.erros, g_estado.descartados);
    avcodec_free_context(&g_ctx);
    av_buffer_unref(&g_dispositivo);
    av_packet_free(&g_pacote);
    av_frame_free(&g_quadro);
}
