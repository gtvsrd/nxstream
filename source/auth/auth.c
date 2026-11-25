// Autenticação: chamadas de rede. A interpretação fica em auth_parse.c.
#include "auth.h"
#include "../api/config.h"
#include "../core/http.h"
#include "../core/log.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_erro[256];

const char *auth_ultimo_erro(void) {
    return g_erro;
}

static void definir_erro(const char *fmt, const char *detalhe) {
    snprintf(g_erro, sizeof g_erro, fmt, detalhe ? detalhe : "");
}

// Registra no log uma resposta de erro (truncada). Só é chamada em falhas,
// e respostas de erro não contêm tokens.
static void log_resposta_erro(const char *etapa, const HttpResposta *r) {
    LOG("auth: %s respondeu HTTP %ld: %.300s", etapa, r->status, r->corpo ? r->corpo : "(vazio)");
}

static const char *const HEADERS_FORM[] = {
    "Content-Type: application/x-www-form-urlencoded",
    "Cache-Control: no-store, must-revalidate, no-cache",
    NULL,
};

static const char *const HEADERS_XBL[] = {
    "Content-Type: application/json",
    "Accept: application/json",
    "x-xbl-contract-version: 1",
    "Cache-Control: no-cache",
    "Origin: https://www.xbox.com",
    "Referer: https://www.xbox.com/",
    "ms-cv: 0",
    NULL,
};

static const char *const HEADERS_GSSV[] = {
    "Content-Type: application/json",
    "Cache-Control: no-store, must-revalidate, no-cache",
    "x-gssv-client: " GSSV_CLIENTE,
    NULL,
};

// ---------------------------------------------------------------------------
// 1. Código de login
// ---------------------------------------------------------------------------
AuthStatus auth_codigo_iniciar(AuthCodigo *codigo) {
    g_erro[0] = '\0';
    char *escopo = http_codificar(MS_ESCOPO);
    char corpo[512];
    snprintf(corpo, sizeof corpo, "client_id=%s&scope=%s", MS_CLIENT_ID, escopo ? escopo : "");
    free(escopo);

    HttpResposta r;
    if (!http_requisicao(HTTP_POST, MS_URL_DEVICECODE, HEADERS_FORM, corpo, &r)) {
        definir_erro("Sem conexao com o servidor de login (%s).", r.erro);
        return AUTH_ERRO_REDE;
    }

    AuthStatus st = AUTH_ERRO;
    if (r.status == 200 && auth_parse_codigo(r.corpo, codigo)) {
        LOG("auth: codigo de login gerado (expira em %d s, intervalo %d s)",
            codigo->expira_em_s, codigo->intervalo_s);
        st = AUTH_OK;
    } else {
        log_resposta_erro("devicecode", &r);
        snprintf(g_erro, sizeof g_erro, "Servidor de login respondeu HTTP %ld.", r.status);
    }
    http_resposta_liberar(&r);
    return st;
}

AuthStatus auth_codigo_verificar(const AuthCodigo *codigo, AuthConta *conta) {
    g_erro[0] = '\0';
    char *dispositivo = http_codificar(codigo->codigo_dispositivo);
    if (!dispositivo)
        return AUTH_ERRO;

    size_t tam = strlen(dispositivo) + 256;
    char *corpo = malloc(tam);
    snprintf(corpo, tam,
             "grant_type=urn:ietf:params:oauth:grant-type:device_code&client_id=%s&device_code=%s",
             MS_CLIENT_ID, dispositivo);
    free(dispositivo);

    HttpResposta r;
    bool ok = http_requisicao(HTTP_POST, MS_URL_TOKEN, HEADERS_FORM, corpo, &r);
    free(corpo);
    if (!ok) {
        definir_erro("Sem conexao com o servidor de login (%s).", r.erro);
        return AUTH_ERRO_REDE;
    }

    AuthStatus st = auth_parse_token(r.status, r.corpo, conta);
    switch (st) {
        case AUTH_OK:       LOG("auth: login concluido"); break;
        case AUTH_PENDENTE: break;
        case AUTH_LENTO:    LOG("auth: servidor pediu para consultar mais devagar"); break;
        case AUTH_RECUSADO: LOG("auth: login recusado pelo usuario");
                            definir_erro("O login foi recusado.", NULL); break;
        case AUTH_EXPIRADO: LOG("auth: codigo de login expirou");
                            definir_erro("O codigo expirou.", NULL); break;
        default:
            log_resposta_erro("token (device code)", &r);
            snprintf(g_erro, sizeof g_erro, "Servidor de login respondeu HTTP %ld.", r.status);
            st = AUTH_ERRO;
            break;
    }
    http_resposta_liberar(&r);
    return st;
}

// ---------------------------------------------------------------------------
// 2. Renovação com o refresh token salvo
// ---------------------------------------------------------------------------
AuthStatus auth_renovar(AuthConta *conta) {
    g_erro[0] = '\0';
    if (!conta->refresh_token)
        return AUTH_INVALIDO;

    char *refresh = http_codificar(conta->refresh_token);
    char *escopo = http_codificar(MS_ESCOPO);
    if (!refresh || !escopo) {
        free(refresh);
        free(escopo);
        return AUTH_ERRO;
    }
    size_t tam = strlen(refresh) + strlen(escopo) + 256;
    char *corpo = malloc(tam);
    snprintf(corpo, tam, "client_id=%s&grant_type=refresh_token&refresh_token=%s&scope=%s",
             MS_CLIENT_ID, refresh, escopo);
    free(refresh);
    free(escopo);

    HttpResposta r;
    bool ok = http_requisicao(HTTP_POST, MS_URL_TOKEN, HEADERS_FORM, corpo, &r);
    free(corpo);
    if (!ok) {
        definir_erro("Sem conexao com o servidor de login (%s).", r.erro);
        return AUTH_ERRO_REDE;
    }

    AuthStatus st = auth_parse_token(r.status, r.corpo, conta);
    if (st == AUTH_OK) {
        LOG("auth: sessao renovada");
    } else if (st == AUTH_INVALIDO || r.status == 400) {
        LOG("auth: refresh token nao vale mais, sera preciso logar de novo");
        definir_erro("Sua sessao expirou. Faca login de novo.", NULL);
        st = AUTH_INVALIDO;
    } else {
        log_resposta_erro("token (refresh)", &r);
        snprintf(g_erro, sizeof g_erro, "Servidor de login respondeu HTTP %ld.", r.status);
        st = AUTH_ERRO;
    }
    http_resposta_liberar(&r);
    return st;
}

// ---------------------------------------------------------------------------
// 3. Xbox Live -> XSTS -> streaming
// ---------------------------------------------------------------------------

// Trata a resposta de erro do XSTS, que traz um código "XErr" explicativo
static void erro_xsts(const HttpResposta *r) {
    cJSON *raiz = r->corpo ? cJSON_Parse(r->corpo) : NULL;
    const cJSON *xerr = cJSON_GetObjectItemCaseSensitive(raiz, "XErr");
    const char *descricao = NULL;
    if (cJSON_IsNumber(xerr)) {
        long long codigo = (long long)xerr->valuedouble;
        descricao = auth_descrever_xerr(codigo);
        LOG("auth: XSTS recusou com XErr %lld", codigo);
    }
    if (descricao)
        snprintf(g_erro, sizeof g_erro, "%s", descricao);
    else
        snprintf(g_erro, sizeof g_erro, "Xbox Live recusou o acesso (HTTP %ld).", r->status);
    cJSON_Delete(raiz);
}

static char *post_json(const char *url, const char *const *headers, cJSON *corpo,
                       const char *etapa, HttpResposta *r) {
    memset(r, 0, sizeof *r);
    char *json = cJSON_PrintUnformatted(corpo);
    bool ok = json && http_requisicao(HTTP_POST, url, headers, json, r);
    free(json);
    if (!ok) {
        definir_erro("Sem conexao com o Xbox Live (%s).", r->erro);
        return NULL;
    }
    if (r->status != 200) {
        log_resposta_erro(etapa, r);
        return NULL;
    }
    return r->corpo;
}

// 3a + 3b: user token do Xbox Live e XSTS do serviço de streaming (gssv).
// Valem para as duas ofertas (console e nuvem). gamertag pode ser NULL.
static AuthStatus obter_xsts_gssv(const AuthConta *conta, char **user_token_saida, char **xsts_saida,
                                  char *gamertag, int tam_gamertag) {
    HttpResposta r;
    char *user_token = NULL, *xsts_token = NULL;
    *user_token_saida = *xsts_saida = NULL;

    // 3a. User token
    {
        size_t tam = strlen(conta->access_token) + 3;
        char *ticket = malloc(tam);
        snprintf(ticket, tam, "d=%s", conta->access_token);

        cJSON *corpo = cJSON_CreateObject();
        cJSON *props = cJSON_AddObjectToObject(corpo, "Properties");
        cJSON_AddStringToObject(props, "AuthMethod", "RPS");
        cJSON_AddStringToObject(props, "SiteName", "user.auth.xboxlive.com");
        cJSON_AddStringToObject(props, "RpsTicket", ticket);
        cJSON_AddStringToObject(corpo, "RelyingParty", XBL_RP_USER);
        cJSON_AddStringToObject(corpo, "TokenType", "JWT");
        free(ticket);

        char *resp = post_json(XBL_URL_USER_AUTH, HEADERS_XBL, corpo, "user/authenticate", &r);
        cJSON_Delete(corpo);
        if (resp)
            user_token = auth_parse_xbl_token(resp, NULL, 0);
        bool ok_transporte = r.status != 0;
        if (!user_token && ok_transporte && !g_erro[0])
            snprintf(g_erro, sizeof g_erro, "Xbox Live recusou o login (HTTP %ld).", r.status);
        http_resposta_liberar(&r);
        if (!user_token)
            return ok_transporte ? AUTH_ERRO : AUTH_ERRO_REDE;
    }

    // 3b. XSTS para o serviço de streaming
    {
        cJSON *corpo = cJSON_CreateObject();
        cJSON *props = cJSON_AddObjectToObject(corpo, "Properties");
        cJSON_AddStringToObject(props, "SandboxId", "RETAIL");
        cJSON *tokens = cJSON_AddArrayToObject(props, "UserTokens");
        cJSON_AddItemToArray(tokens, cJSON_CreateString(user_token));
        cJSON_AddStringToObject(corpo, "RelyingParty", XBL_RP_GSSV);
        cJSON_AddStringToObject(corpo, "TokenType", "JWT");

        char *resp = post_json(XBL_URL_XSTS, HEADERS_XBL, corpo, "xsts/authorize", &r);
        cJSON_Delete(corpo);
        if (resp)
            xsts_token = auth_parse_xbl_token(resp, gamertag, tam_gamertag);
        if (!xsts_token && r.status != 0 && r.status != 200)
            erro_xsts(&r);
        AuthStatus st = xsts_token ? AUTH_OK : r.status ? AUTH_ERRO : AUTH_ERRO_REDE;
        http_resposta_liberar(&r);
        if (!xsts_token) {
            free(user_token);
            return st;
        }
    }

    *user_token_saida = user_token;
    *xsts_saida = xsts_token;
    return AUTH_OK;
}

// 3c. gsToken de uma oferta (xhome, xgpuweb, xgpuwebf2p). *status_http recebe o
// código da resposta (0 = sem rede), para decidir se vale tentar outra oferta.
static AuthStatus login_gssv(const char *url, const char *oferta, const char *xsts_token,
                             AuthStreaming *s, long *status_http) {
    HttpResposta r;
    AuthStatus st;
    cJSON *corpo = cJSON_CreateObject();
    cJSON_AddStringToObject(corpo, "token", xsts_token);
    cJSON_AddStringToObject(corpo, "offeringId", oferta);

    char etapa[48];
    snprintf(etapa, sizeof etapa, "gssv login %s", oferta);
    char *resp = post_json(url, HEADERS_GSSV, corpo, etapa, &r);
    cJSON_Delete(corpo);
    if (resp && auth_parse_streaming(resp, s)) {
        snprintf(s->oferta, sizeof s->oferta, "%s", oferta);
        LOG("auth: %s liberado (regiao %s, valido por %d s)", oferta, s->base_uri, s->duracao_s);
        st = AUTH_OK;
    } else {
        if (r.status != 0 && !g_erro[0])
            snprintf(g_erro, sizeof g_erro,
                     "Servico de streaming recusou o acesso (HTTP %ld).", r.status);
        st = r.status ? AUTH_ERRO : AUTH_ERRO_REDE;
    }
    *status_http = r.status;
    http_resposta_liberar(&r);
    return st;
}

AuthStatus auth_streaming(const AuthConta *conta, AuthStreaming *s) {
    g_erro[0] = '\0';
    AuthStatus st;
    char *user_token = NULL, *xsts_token = NULL;
    // Quem chama pode já trazer a gamertag (renovações): não é pedida de novo
    char gamertag_antes[sizeof s->gamertag];
    snprintf(gamertag_antes, sizeof gamertag_antes, "%s", s->gamertag);

    st = obter_xsts_gssv(conta, &user_token, &xsts_token, s->gamertag, sizeof s->gamertag);
    if (st != AUTH_OK)
        goto fim;

    // 3c. Token do streaming do console (xhome)
    long status;
    st = login_gssv(GSSV_URL_LOGIN, GSSV_OFERTA, xsts_token, s, &status);
    if (st == AUTH_OK && !s->gamertag[0])
        snprintf(s->gamertag, sizeof s->gamertag, "%s", gamertag_antes);

    // 3d. Gamertag (v0.10.3). O XSTS do streaming não traz o nome da conta; o do
    // Xbox Live comum traz (DisplayClaims.xui[0].gtg). Opcional: se falhar, o app
    // segue sem mostrar a gamertag.
    if (st == AUTH_OK && !s->gamertag[0]) {
        cJSON *corpo = cJSON_CreateObject();
        cJSON *props = cJSON_AddObjectToObject(corpo, "Properties");
        cJSON_AddStringToObject(props, "SandboxId", "RETAIL");
        cJSON *tokens = cJSON_AddArrayToObject(props, "UserTokens");
        cJSON_AddItemToArray(tokens, cJSON_CreateString(user_token));
        cJSON_AddStringToObject(corpo, "RelyingParty", XBL_RP_XBOXLIVE);
        cJSON_AddStringToObject(corpo, "TokenType", "JWT");

        char erro_antes[sizeof g_erro];
        snprintf(erro_antes, sizeof erro_antes, "%s", g_erro);
        HttpResposta rg;
        char *resp = post_json(XBL_URL_XSTS, HEADERS_XBL, corpo, "xsts (gamertag)", &rg);
        cJSON_Delete(corpo);
        char *token = resp ? auth_parse_xbl_token(resp, s->gamertag, sizeof s->gamertag) : NULL;
        free(token); // só a gamertag interessa
        LOG("auth: gamertag %s (HTTP %ld)", s->gamertag[0] ? "obtida" : "nao veio", rg.status);
        http_resposta_liberar(&rg);
        snprintf(g_erro, sizeof g_erro, "%s", erro_antes); // falha aqui não é erro do login
    }

fim:
    free(user_token);
    free(xsts_token);
    return st;
}

// ---------------------------------------------------------------------------
// 3e. xCloud (fase X0): mesmo XSTS, outra oferta no gssv
// ---------------------------------------------------------------------------
AuthStatus auth_nuvem(const AuthConta *conta, AuthStreaming *s) {
    g_erro[0] = '\0';
    char *user_token = NULL, *xsts_token = NULL;
    AuthStatus st = obter_xsts_gssv(conta, &user_token, &xsts_token, NULL, 0);
    if (st != AUTH_OK)
        goto fim;

    long status = 0;
    st = login_gssv(GSSV_URL_LOGIN_NUVEM, GSSV_OFERTA_NUVEM, xsts_token, s, &status);
    if (st == AUTH_ERRO) {
        // Sem Game Pass Ultimate o gssv recusa a oferta completa; a gratuita
        // (Fortnite e outros free-to-play) vale para qualquer conta.
        long status_f2p = 0;
        LOG("auth: oferta %s recusada (HTTP %ld); tentando %s", GSSV_OFERTA_NUVEM, status, GSSV_OFERTA_NUVEM_F2P);
        char erro_antes[sizeof g_erro];
        snprintf(erro_antes, sizeof erro_antes, "%s", g_erro);
        g_erro[0] = '\0';
        st = login_gssv(GSSV_URL_LOGIN_NUVEM_F2P, GSSV_OFERTA_NUVEM_F2P, xsts_token, s, &status_f2p);
        if (st == AUTH_ERRO)
            snprintf(g_erro, sizeof g_erro,
                     "A conta nao tem acesso ao Xbox Cloud Gaming (HTTP %ld e %ld).", status, status_f2p);
        else if (st != AUTH_OK)
            snprintf(g_erro, sizeof g_erro, "%s", erro_antes);
    }
    if (st == AUTH_OK)
        LOG("auth: nuvem: oferta %s, regiao %s (de: %s)", s->oferta, s->regiao[0] ? s->regiao : "?", s->regioes);

fim:
    free(user_token);
    free(xsts_token);
    return st;
}

// ---------------------------------------------------------------------------
// 4. Token de transferência para o console (/connect)
// ---------------------------------------------------------------------------
AuthStatus auth_token_console(const AuthConta *conta, char **token) {
    g_erro[0] = '\0';
    *token = NULL;
    if (!conta->refresh_token)
        return AUTH_INVALIDO;

    char *refresh = http_codificar(conta->refresh_token);
    char *escopo = http_codificar(MS_ESCOPO_CONSOLE);
    if (!refresh || !escopo) {
        free(refresh);
        free(escopo);
        return AUTH_ERRO;
    }
    size_t tam = strlen(refresh) + strlen(escopo) + 256;
    char *corpo = malloc(tam);
    snprintf(corpo, tam, "client_id=%s&scope=%s&grant_type=refresh_token&refresh_token=%s",
             MS_CLIENT_ID, escopo, refresh);
    free(refresh);
    free(escopo);

    HttpResposta r;
    bool ok = http_requisicao(HTTP_POST, MS_URL_LIVE_TOKEN, HEADERS_FORM, corpo, &r);
    free(corpo);
    if (!ok) {
        definir_erro("Sem conexao com o servidor de login (%s).", r.erro);
        return AUTH_ERRO_REDE;
    }

    *token = auth_parse_token_console(r.status, r.corpo);
    AuthStatus st = AUTH_OK;
    if (*token) {
        LOG("auth: token de transferencia do console obtido");
    } else {
        log_resposta_erro("token do console", &r);
        snprintf(g_erro, sizeof g_erro, "Login nao liberou acesso ao console (HTTP %ld).", r.status);
        st = AUTH_ERRO;
    }
    http_resposta_liberar(&r);
    return st;
}
