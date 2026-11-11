// API de streaming do console: chamadas de rede.
#include "xapi.h"
#include "config.h"
#include "../core/http.h"
#include "../core/log.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_erro[256];

const char *xapi_ultimo_erro(void) {
    return g_erro;
}

// Requisição na API com os headers padrão e o gsToken.
// caminho pode começar com '/' ou não (o sessionPath vem sem).
static bool xapi_requisicao(const AuthStreaming *s, HttpMetodo metodo, const char *caminho,
                            const char *corpo, HttpResposta *r) {
    const char *barra = caminho[0] == '/' ? "" : "/";
    size_t tam_url = strlen(s->base_uri) + strlen(caminho) + 2;
    char *url = malloc(tam_url);
    snprintf(url, tam_url, "%s%s%s", s->base_uri, barra, caminho);

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

    bool ok = http_requisicao(metodo, url, headers, corpo, r);
    if (!ok)
        snprintf(g_erro, sizeof g_erro, "Sem conexao com o servico de streaming (%s).", r->erro);
    free(url);
    free(autorizacao);
    return ok;
}

static void log_erro(const char *etapa, const HttpResposta *r) {
    LOG("xapi: %s respondeu HTTP %ld: %.300s", etapa, r->status, r->corpo ? r->corpo : "(vazio)");
    snprintf(g_erro, sizeof g_erro, "Servico de streaming respondeu HTTP %ld (%s).", r->status, etapa);
}

// ---------------------------------------------------------------------------
// Consoles
// ---------------------------------------------------------------------------
bool xapi_listar_consoles(const AuthStreaming *s, XConsoles *consoles) {
    g_erro[0] = '\0';
    memset(consoles, 0, sizeof *consoles);

    HttpResposta r;
    if (!xapi_requisicao(s, HTTP_GET, API_CAMINHO_CONSOLES, NULL, &r))
        return false;

    bool ok = false;
    if (r.status == 200 && xapi_parse_consoles(r.corpo, consoles)) {
        LOG("xapi: %d console(s) encontrado(s)", consoles->total);
        for (int i = 0; i < consoles->total; i++)
            LOG("xapi:   [%d] %s | %s | %s | id %.8s...", i, consoles->itens[i].nome,
                consoles->itens[i].tipo, consoles->itens[i].estado, consoles->itens[i].server_id);
        ok = true;
    } else {
        log_erro("lista de consoles", &r);
    }
    http_resposta_liberar(&r);
    return ok;
}

// ---------------------------------------------------------------------------
// Sessão
// ---------------------------------------------------------------------------
bool xapi_sessao_iniciar(const AuthStreaming *s, const char *server_id, XSessao *sessao) {
    g_erro[0] = '\0';
    char *corpo = xapi_corpo_play(server_id);
    if (!corpo)
        return false;

    HttpResposta r;
    bool ok = xapi_requisicao(s, HTTP_POST, API_CAMINHO_PLAY, corpo, &r);
    free(corpo);
    if (!ok)
        return false;

    ok = (r.status >= 200 && r.status < 300) && xapi_parse_sessao(r.corpo, sessao);
    if (ok)
        LOG("xapi: sessao criada (%s)", sessao->id);
    else
        log_erro("criar sessao", &r);
    http_resposta_liberar(&r);
    return ok;
}

static char *caminho_sessao(const XSessao *sessao, const char *sufixo) {
    size_t tam = strlen(sessao->caminho) + strlen(sufixo) + 1;
    char *c = malloc(tam);
    snprintf(c, tam, "%s%s", sessao->caminho, sufixo);
    return c;
}

XSessaoEstado xapi_sessao_estado(const AuthStreaming *s, const XSessao *sessao,
                                 char *detalhe, int tam_detalhe) {
    g_erro[0] = '\0';
    if (detalhe && tam_detalhe > 0)
        detalhe[0] = '\0';
    char *caminho = caminho_sessao(sessao, "/state");
    HttpResposta r;
    bool ok = xapi_requisicao(s, HTTP_GET, caminho, NULL, &r);
    free(caminho);
    if (!ok)
        return XSESSAO_ERRO;

    XSessaoEstado st = XSESSAO_ERRO;
    if (r.status == 200) {
        st = xapi_parse_estado(r.corpo, detalhe, tam_detalhe);
        if (st == XSESSAO_FALHOU || st == XSESSAO_DESCONHECIDO)
            LOG("xapi: estado da sessao: %.300s", r.corpo ? r.corpo : "");
    } else {
        log_erro("estado da sessao", &r);
    }
    http_resposta_liberar(&r);
    return st;
}

bool xapi_sessao_conectar(const AuthStreaming *s, const XSessao *sessao, const char *token_console) {
    g_erro[0] = '\0';
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "userToken", token_console);
    char *corpo = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!corpo)
        return false;

    char *caminho = caminho_sessao(sessao, "/connect");
    HttpResposta r;
    bool ok = xapi_requisicao(s, HTTP_POST, caminho, corpo, &r);
    free(caminho);
    free(corpo);
    if (!ok)
        return false;

    ok = r.status >= 200 && r.status < 300;
    if (ok)
        LOG("xapi: token do console enviado (HTTP %ld)", r.status);
    else
        log_erro("connect", &r);
    http_resposta_liberar(&r);
    return ok;
}

long xapi_sessao_keepalive(const AuthStreaming *s, const XSessao *sessao) {
    g_erro[0] = '\0';
    char *caminho = caminho_sessao(sessao, "/keepalive");
    HttpResposta r;
    bool ok = xapi_requisicao(s, HTTP_POST, caminho, "{}", &r);
    free(caminho);
    if (!ok)
        return 0;

    long status = r.status;
    if (status >= 200 && status < 300)
        LOG("xapi: keepalive ok (HTTP %ld) %.200s", status, r.corpo ? r.corpo : "");
    else
        log_erro("keepalive", &r);
    http_resposta_liberar(&r);
    return status;
}

bool xapi_sessao_encerrar(const AuthStreaming *s, const XSessao *sessao) {
    g_erro[0] = '\0';
    HttpResposta r;
    if (!xapi_requisicao(s, HTTP_DELETE, sessao->caminho, NULL, &r))
        return false;
    bool ok = r.status >= 200 && r.status < 300;
    if (ok)
        LOG("xapi: sessao encerrada (HTTP %ld)", r.status);
    else
        log_erro("encerrar sessao", &r);
    http_resposta_liberar(&r);
    return ok;
}

// ---------------------------------------------------------------------------
// Negociação WebRTC (fase 2b)
// ---------------------------------------------------------------------------
static bool post_sessao(const AuthStreaming *s, const XSessao *sessao, const char *sufixo,
                        const char *corpo, const char *etapa) {
    char *caminho = caminho_sessao(sessao, sufixo);
    HttpResposta r;
    bool ok = xapi_requisicao(s, HTTP_POST, caminho, corpo, &r);
    free(caminho);
    if (!ok)
        return false;
    ok = r.status >= 200 && r.status < 300;
    if (ok)
        LOG("xapi: %s enviado (HTTP %ld)", etapa, r.status);
    else
        log_erro(etapa, &r);
    http_resposta_liberar(&r);
    return ok;
}

// GET em /sdp ou /ice: HTTP 204 = o console ainda não respondeu
static int get_troca(const AuthStreaming *s, const XSessao *sessao, const char *sufixo,
                     const char *etapa, HttpResposta *r) {
    char *caminho = caminho_sessao(sessao, sufixo);
    bool ok = xapi_requisicao(s, HTTP_GET, caminho, NULL, r);
    free(caminho);
    if (!ok)
        return -1;
    if (r->status == 204 || (r->status == 200 && (!r->corpo || !r->corpo[0]))) {
        http_resposta_liberar(r);
        return 0;
    }
    if (r->status != 200) {
        log_erro(etapa, r);
        http_resposta_liberar(r);
        return -1;
    }
    return 1;
}

bool xapi_sessao_enviar_sdp(const AuthStreaming *s, const XSessao *sessao, const char *oferta) {
    g_erro[0] = '\0';
    char *corpo = xapi_corpo_sdp(oferta);
    if (!corpo)
        return false;
    bool ok = post_sessao(s, sessao, "/sdp", corpo, "oferta SDP");
    free(corpo);
    return ok;
}

int xapi_sessao_receber_sdp(const AuthStreaming *s, const XSessao *sessao, char **resposta) {
    g_erro[0] = '\0';
    *resposta = NULL;
    HttpResposta r;
    int st = get_troca(s, sessao, "/sdp", "resposta SDP", &r);
    if (st != 1)
        return st;
    char erro[160];
    st = xapi_parse_resposta_sdp(r.corpo, resposta, erro, sizeof erro);
    if (st == 1) {
        LOG("xapi: resposta SDP do console recebida (%d bytes)", (int)strlen(*resposta));
        LOG("xapi: SDP do console:\n%s", *resposta);
    } else {
        LOG("xapi: resposta SDP invalida (%s): %.400s", erro, r.corpo);
        snprintf(g_erro, sizeof g_erro, "O console recusou a oferta (%s).", erro);
    }
    http_resposta_liberar(&r);
    return st;
}

bool xapi_sessao_enviar_ice(const AuthStreaming *s, const XSessao *sessao,
                            const XCandidatos *locais, const char *ufrag) {
    g_erro[0] = '\0';
    char *corpo = xapi_corpo_ice(locais, ufrag);
    if (!corpo)
        return false;
    LOG("xapi: enviando %d candidato(s) local(is)", locais->total);
    bool ok = post_sessao(s, sessao, "/ice", corpo, "candidatos ICE");
    free(corpo);
    return ok;
}

int xapi_sessao_receber_ice(const AuthStreaming *s, const XSessao *sessao, XCandidatos *remotos) {
    g_erro[0] = '\0';
    HttpResposta r;
    int st = get_troca(s, sessao, "/ice", "candidatos do console", &r);
    if (st != 1)
        return st;
    int n = xapi_parse_resposta_ice(r.corpo, remotos);
    if (n >= 0) {
        LOG("xapi: %d candidato(s) do console (ja com os derivados de Teredo)", n);
        for (int i = 0; i < n; i++)
            LOG("xapi:   [%s] %s", remotos->mid[i], remotos->cand[i]);
        st = 1;
    } else {
        LOG("xapi: candidatos do console invalidos: %.400s", r.corpo);
        snprintf(g_erro, sizeof g_erro, "Resposta invalida do console aos candidatos de rede.");
        st = -1;
    }
    http_resposta_liberar(&r);
    return st;
}
