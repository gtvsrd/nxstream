#include "http.h"
#include "log.h"
#include "../api/config.h"

#include <curl/curl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool g_tem_certificados = false;
static void (*g_ao_terminar)(void) = NULL;

// Conexões reaproveitadas (v0.9.3). Antes cada requisição abria TCP + TLS do
// zero até o servidor de streaming (EUA): ~1 s por chamada, e a conexão com o
// console faz uma dúzia delas em sequência. Um handle do curl guarda as
// conexões abertas entre requisições, então o app mantém alguns handles
// reaproveitáveis: um por thread que esteja fazendo HTTP ao mesmo tempo
// (principal, keepalive, renovação, encerramento da sessão antiga).
// Não usa CURLSH para conexões: o curl não suporta compartilhar o cache de
// conexões entre threads simultâneas.
#define POOL_TAMANHO 4
typedef struct {
    CURL *curl;
    bool  em_uso;
    int   geracao;        // handles de uma geração antiga são recriados
    char  host[128];      // último host acessado (preferência na escolha)
} Handle;
static Handle g_pool[POOL_TAMANHO];
static pthread_mutex_t g_pool_trava = PTHREAD_MUTEX_INITIALIZER;
static int g_geracao = 0;

static void host_da_url(const char *url, char *host, size_t tam) {
    const char *inicio = strstr(url, "://");
    inicio = inicio ? inicio + 3 : url;
    size_t n = strcspn(inicio, "/:?");
    if (n >= tam)
        n = tam - 1;
    memcpy(host, inicio, n);
    host[n] = '\0';
}

// Pega um handle livre, de preferência um que já tenha conexão com o host.
// Se todos estiverem em uso, cria um avulso (pool = -1).
static CURL *pegar_handle(const char *host, int *indice) {
    pthread_mutex_lock(&g_pool_trava);
    int escolhido = -1;
    for (int i = 0; i < POOL_TAMANHO && escolhido < 0; i++) // livre e já conectado ao host
        if (!g_pool[i].em_uso && g_pool[i].curl && g_pool[i].geracao == g_geracao &&
            strcmp(g_pool[i].host, host) == 0)
            escolhido = i;
    for (int i = 0; i < POOL_TAMANHO && escolhido < 0; i++) // qualquer livre
        if (!g_pool[i].em_uso)
            escolhido = i;
    CURL *curl = NULL;
    if (escolhido >= 0) {
        Handle *h = &g_pool[escolhido];
        h->em_uso = true;
        if (h->curl && h->geracao != g_geracao) { // conexões de antes do repouso: descarta
            curl_easy_cleanup(h->curl);
            h->curl = NULL;
        }
        if (!h->curl) {
            h->curl = curl_easy_init();
            h->geracao = g_geracao;
        } else {
            curl_easy_reset(h->curl); // mantém as conexões abertas
        }
        snprintf(h->host, sizeof h->host, "%s", host);
        curl = h->curl;
        if (!curl)
            h->em_uso = false;
    }
    pthread_mutex_unlock(&g_pool_trava);
    *indice = curl ? escolhido : -1;
    return curl ? curl : curl_easy_init();
}

static void soltar_handle(CURL *curl, int indice, bool falhou) {
    if (indice < 0) {
        curl_easy_cleanup(curl);
        return;
    }
    pthread_mutex_lock(&g_pool_trava);
    if (falhou) { // não confia nas conexões desse handle depois de um erro de transporte
        curl_easy_cleanup(g_pool[indice].curl);
        g_pool[indice].curl = NULL;
    }
    g_pool[indice].em_uso = false;
    pthread_mutex_unlock(&g_pool_trava);
}

void http_descartar_conexoes(void) {
    pthread_mutex_lock(&g_pool_trava);
    g_geracao++;
    pthread_mutex_unlock(&g_pool_trava);
}

void http_definir_ao_terminar(void (*funcao)(void)) {
    g_ao_terminar = funcao;
}

bool http_iniciar(void) {
    CURLcode rc = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (rc != CURLE_OK) {
        LOG("http: curl_global_init falhou: %s", curl_easy_strerror(rc));
        return false;
    }

    FILE *f = fopen(NX_ARQ_CERTIFICADOS, "rb");
    if (f) {
        fclose(f);
        g_tem_certificados = true;
        LOG("http: usando certificados de %s", NX_ARQ_CERTIFICADOS);
    }

    curl_version_info_data *v = curl_version_info(CURLVERSION_NOW);
    LOG("http: libcurl %s, TLS %s", v->version, v->ssl_version ? v->ssl_version : "?");
    return true;
}

void http_encerrar(void) {
    pthread_mutex_lock(&g_pool_trava);
    for (int i = 0; i < POOL_TAMANHO; i++) {
        if (g_pool[i].curl && !g_pool[i].em_uso) {
            curl_easy_cleanup(g_pool[i].curl);
            g_pool[i].curl = NULL;
        }
    }
    pthread_mutex_unlock(&g_pool_trava);
    curl_global_cleanup();
}

static size_t ao_receber(char *dados, size_t tam, size_t n, void *usuario) {
    HttpResposta *r = usuario;
    size_t total = tam * n;
    char *novo = realloc(r->corpo, r->tamanho + total + 1);
    if (!novo)
        return 0; // aborta a transferência
    memcpy(novo + r->tamanho, dados, total);
    r->tamanho += total;
    novo[r->tamanho] = '\0';
    r->corpo = novo;
    return total;
}

bool http_requisicao(HttpMetodo metodo, const char *url, const char *const *headers,
                     const char *corpo, HttpResposta *r) {
    memset(r, 0, sizeof *r);

    char host[128];
    host_da_url(url, host, sizeof host);
    int indice = -1;
    CURL *curl = pegar_handle(host, &indice);
    if (!curl) {
        snprintf(r->erro, sizeof r->erro, "curl_easy_init falhou");
        return false;
    }

    struct curl_slist *lista = NULL;
    for (const char *const *h = headers; h && *h; h++)
        lista = curl_slist_append(lista, *h);

    char detalhe[CURL_ERROR_SIZE] = "";
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, detalhe);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, lista);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, NX_USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, ao_receber);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, r);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (long)HTTP_TEMPO_CONEXAO);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)HTTP_TEMPO_TOTAL);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    // Conexão parada por mais de 50 s não é reaproveitada (servidores costumam
    // fechar as ociosas por volta de 1 min; o keepalive a cada 30 s a mantém viva)
    curl_easy_setopt(curl, CURLOPT_MAXAGE_CONN, 50L);
    if (g_tem_certificados)
        curl_easy_setopt(curl, CURLOPT_CAINFO, NX_ARQ_CERTIFICADOS);

    if (metodo == HTTP_POST) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, corpo ? corpo : "");
    } else if (metodo == HTTP_DELETE) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    }

    CURLcode rc = curl_easy_perform(curl);
    bool ok = (rc == CURLE_OK);
    if (ok) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &r->status);
    } else {
        snprintf(r->erro, sizeof r->erro, "%s", curl_easy_strerror(rc));
        // Só o host, para não expor parâmetros no log
        const char *inicio = strstr(url, "://");
        inicio = inicio ? inicio + 3 : url;
        const char *fim = strchr(inicio, '/');
        int tam = fim ? (int)(fim - inicio) : (int)strlen(inicio);
        // detalhe traz a causa do sistema (ex.: "No buffer space available")
        LOG("http: falha ao acessar %.*s: %s (curl %d) [%s]", tam, inicio, r->erro, (int)rc,
            detalhe[0] ? detalhe : "sem detalhe");
    }

    // O handle volta para o pool: nada pode apontar para memória desta função
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, NULL);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, NULL);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, NULL);
    curl_slist_free_all(lista);
    soltar_handle(curl, indice, !ok);
    if (g_ao_terminar)
        g_ao_terminar();
    return ok;
}

void http_resposta_liberar(HttpResposta *r) {
    free(r->corpo);
    r->corpo = NULL;
    r->tamanho = 0;
}

char *http_codificar(const char *texto) {
    // Versões antigas do libcurl (o devkitPro usa 7.69) pedem um handle válido
    CURL *curl = curl_easy_init();
    if (!curl)
        return NULL;
    char *tmp = curl_easy_escape(curl, texto, 0);
    char *copia = tmp ? strdup(tmp) : NULL;
    curl_free(tmp);
    curl_easy_cleanup(curl);
    return copia;
}
