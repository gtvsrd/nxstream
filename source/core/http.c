#include "http.h"
#include "log.h"
#include "../api/config.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool g_tem_certificados = false;

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

    CURL *curl = curl_easy_init();
    if (!curl) {
        snprintf(r->erro, sizeof r->erro, "curl_easy_init falhou");
        return false;
    }

    struct curl_slist *lista = NULL;
    for (const char *const *h = headers; h && *h; h++)
        lista = curl_slist_append(lista, *h);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, lista);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, NX_USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, ao_receber);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, r);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (long)HTTP_TEMPO_CONEXAO);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)HTTP_TEMPO_TOTAL);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
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
        LOG("http: falha ao acessar %.*s: %s (curl %d)", tam, inicio, r->erro, (int)rc);
    }

    curl_slist_free_all(lista);
    curl_easy_cleanup(curl);
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
