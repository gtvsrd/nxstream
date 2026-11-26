// Capas dos jogos da nuvem: veja capas.h
#include "capas.h"
#include "tela.h"
#include "../api/config.h"
#include "../core/http.h"
#include "../core/log.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define SLOTS     64    // capas na memória (~115 KB cada)
#define THREADS   2
#define VALIDADE  30    // quadros sem ser pedida: deixa de buscar
#define ARQ_MAX   (512 * 1024)

typedef enum { VAZIO, PEDIDO, CARREGANDO, PRONTO, FALHOU } Estado;

typedef struct {
    Estado estado;
    char id[24];
    char url[160];
    uint32_t *px;
    unsigned long usado; // último quadro em que foi pedida
} Slot;

static Slot g_slots[SLOTS];
static pthread_mutex_t g_trava = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_sinal = PTHREAD_COND_INITIALIZER;
static pthread_t g_threads[THREADS];
static int g_n_threads;
static bool g_parar;
static unsigned long g_quadro = 1000;
static struct { unsigned long sd, rede, falhas; } g_est;

static char *ler(const char *caminho, size_t *tam) {
    FILE *f = fopen(caminho, "rb");
    if (!f)
        return NULL;
    char *dados = malloc(ARQ_MAX);
    size_t n = dados ? fread(dados, 1, ARQ_MAX, f) : 0;
    fclose(f);
    if (!n) {
        free(dados);
        return NULL;
    }
    *tam = n;
    return dados;
}

static void gravar(const char *caminho, const char *dados, size_t tam) {
    char tmp[160];
    snprintf(tmp, sizeof tmp, "%s.tmp", caminho);
    FILE *f = fopen(tmp, "wb");
    if (!f)
        return;
    bool ok = fwrite(dados, 1, tam, f) == tam;
    ok = (fclose(f) == 0) && ok;
    if (ok)
        rename(tmp, caminho);
    else
        remove(tmp);
}

// SD ou rede -> RGBA. NULL = não deu.
static uint32_t *carregar(const char *id, const char *url) {
    char caminho[128];
    snprintf(caminho, sizeof caminho, "%s/%s.jpg", NX_PASTA_CAPAS, id);
    size_t tam = 0;
    char *dados = ler(caminho, &tam);
    bool do_sd = dados != NULL;
    if (!dados && url[0]) {
        // O servidor de imagens da loja redimensiona: pede já no tamanho da tela
        char u[256];
        snprintf(u, sizeof u, "%s?w=%d&h=%d&format=jpg&q=85", url, UI_CAPA_L, UI_CAPA_A);
        HttpResposta r;
        if (http_requisicao(HTTP_GET, u, NULL, NULL, &r) && r.status == 200 && r.corpo && r.tamanho) {
            dados = malloc(r.tamanho);
            if (dados) {
                memcpy(dados, r.corpo, r.tamanho);
                tam = r.tamanho;
            }
        } else if (r.status != 0 && r.status != 200) {
            LOG("capas: %s respondeu HTTP %ld", id, r.status);
        }
        http_resposta_liberar(&r);
    }
    uint32_t *px = dados ? ui_decodificar_capa((const uint8_t *)dados, tam, UI_CAPA_L, UI_CAPA_A) : NULL;
    if (px && !do_sd)
        gravar(caminho, dados, tam); // só guarda o que decodificou
    free(dados);
    pthread_mutex_lock(&g_trava);
    if (!px)
        g_est.falhas++;
    else if (do_sd)
        g_est.sd++;
    else
        g_est.rede++;
    pthread_mutex_unlock(&g_trava);
    return px;
}

static void *trabalhar(void *arg) {
    (void)arg;
    pthread_mutex_lock(&g_trava);
    while (!g_parar) {
        // O pedido mais recente primeiro (o que está na tela agora)
        Slot *melhor = NULL;
        for (int i = 0; i < SLOTS; i++) {
            Slot *s = &g_slots[i];
            if (s->estado == PEDIDO && s->usado + VALIDADE >= g_quadro && (!melhor || s->usado > melhor->usado))
                melhor = s;
        }
        if (!melhor) {
            pthread_cond_wait(&g_sinal, &g_trava);
            continue;
        }
        melhor->estado = CARREGANDO;
        char id[24], url[160];
        snprintf(id, sizeof id, "%s", melhor->id);
        snprintf(url, sizeof url, "%s", melhor->url);
        pthread_mutex_unlock(&g_trava);

        uint32_t *px = carregar(id, url);

        pthread_mutex_lock(&g_trava);
        if (melhor->estado == CARREGANDO && !strcmp(melhor->id, id)) {
            melhor->px = px;
            melhor->estado = px ? PRONTO : FALHOU;
        } else {
            free(px); // o lugar foi reaproveitado enquanto carregava
        }
    }
    pthread_mutex_unlock(&g_trava);
    return NULL;
}

void capas_iniciar(void) {
    if (g_n_threads)
        return;
    mkdir(NX_PASTA_CAPAS, 0777);
    g_parar = false;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 256 * 1024); // curl + TLS + stb_image
    for (int i = 0; i < THREADS; i++)
        if (pthread_create(&g_threads[g_n_threads], &at, trabalhar, NULL) == 0)
            g_n_threads++;
    pthread_attr_destroy(&at);
    LOG("capas: %d thread(s) prontas", g_n_threads);
}

void capas_encerrar(void) {
    if (!g_n_threads)
        return;
    pthread_mutex_lock(&g_trava);
    g_parar = true;
    pthread_cond_broadcast(&g_sinal);
    pthread_mutex_unlock(&g_trava);
    for (int i = 0; i < g_n_threads; i++)
        pthread_join(g_threads[i], NULL);
    g_n_threads = 0;
    LOG("capas: encerradas (%lu do SD, %lu baixadas, %lu falhas)", g_est.sd, g_est.rede, g_est.falhas);
    for (int i = 0; i < SLOTS; i++) {
        free(g_slots[i].px);
        memset(&g_slots[i], 0, sizeof g_slots[i]);
    }
}

void capas_novo_quadro(void) {
    pthread_mutex_lock(&g_trava);
    g_quadro++;
    pthread_mutex_unlock(&g_trava);
}

const uint32_t *capas_pegar(const char *produto_id, const char *url) {
    if (!produto_id || !produto_id[0] || !g_n_threads)
        return NULL;
    const uint32_t *px = NULL;
    pthread_mutex_lock(&g_trava);
    Slot *livre = NULL;
    for (int i = 0; i < SLOTS; i++) {
        Slot *s = &g_slots[i];
        if (s->estado != VAZIO && !strcmp(s->id, produto_id)) {
            bool reviver = s->estado == PEDIDO && s->usado + VALIDADE < g_quadro;
            s->usado = g_quadro;
            if (s->estado == PRONTO)
                px = s->px;
            if (reviver)
                pthread_cond_signal(&g_sinal);
            pthread_mutex_unlock(&g_trava);
            return px;
        }
        // Lugar para um pedido novo: vazio; senão o usado há mais tempo (nunca
        // um que está carregando nem um pedido neste quadro ou no anterior,
        // que pode estar na tela)
        if (s->estado == CARREGANDO || s->usado + 1 >= g_quadro)
            continue;
        if (!livre || livre->estado != VAZIO) {
            if (s->estado == VAZIO || !livre || s->usado < livre->usado)
                livre = s;
        }
    }
    if (livre) {
        free(livre->px);
        livre->px = NULL;
        livre->estado = PEDIDO;
        snprintf(livre->id, sizeof livre->id, "%s", produto_id);
        snprintf(livre->url, sizeof livre->url, "%s", url ? url : "");
        livre->usado = g_quadro;
        pthread_cond_signal(&g_sinal);
    }
    pthread_mutex_unlock(&g_trava);
    return NULL;
}
