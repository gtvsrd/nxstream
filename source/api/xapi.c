// API de streaming do console: chamadas de rede.
#include "xapi.h"
#include "config.h"
#include "../core/http.h"
#include "../core/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_erro[256];

const char *xapi_ultimo_erro(void) {
    return g_erro;
}

// GET na API com os headers padrão e o gsToken
static bool xapi_get(const AuthStreaming *s, const char *caminho, HttpResposta *r) {
    size_t tam_url = strlen(s->base_uri) + strlen(caminho) + 1;
    char *url = malloc(tam_url);
    snprintf(url, tam_url, "%s%s", s->base_uri, caminho);

    size_t tam_auth = strlen(s->gs_token) + 32;
    char *autorizacao = malloc(tam_auth);
    snprintf(autorizacao, tam_auth, "Authorization: Bearer %s", s->gs_token);

    const char *headers[] = {
        "Accept: application/json",
        "Content-Type: application/json",
        "X-Gssv-Client: " GSSV_CLIENTE,
        "X-MS-Device-Info: " NX_DEVICE_INFO,
        autorizacao,
        NULL,
    };

    bool ok = http_requisicao(HTTP_GET, url, headers, NULL, r);
    free(url);
    free(autorizacao);
    return ok;
}

bool xapi_listar_consoles(const AuthStreaming *s, XConsoles *consoles) {
    g_erro[0] = '\0';
    memset(consoles, 0, sizeof *consoles);

    HttpResposta r;
    if (!xapi_get(s, API_CAMINHO_CONSOLES, &r)) {
        snprintf(g_erro, sizeof g_erro, "Sem conexao com o servico de streaming (%s).", r.erro);
        return false;
    }

    bool ok = false;
    if (r.status == 200 && xapi_parse_consoles(r.corpo, consoles)) {
        LOG("xapi: %d console(s) encontrado(s)", consoles->total);
        for (int i = 0; i < consoles->total; i++)
            LOG("xapi:   [%d] %s | %s | %s | id %.8s...", i, consoles->itens[i].nome,
                consoles->itens[i].tipo, consoles->itens[i].estado, consoles->itens[i].server_id);
        ok = true;
    } else {
        LOG("xapi: lista de consoles respondeu HTTP %ld: %.300s", r.status,
            r.corpo ? r.corpo : "(vazio)");
        snprintf(g_erro, sizeof g_erro,
                 "Servico de streaming respondeu HTTP %ld ao listar consoles.", r.status);
    }
    http_resposta_liberar(&r);
    return ok;
}
