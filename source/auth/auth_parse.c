// Interpretação das respostas de autenticação. Sem rede: testável no PC.
#include "auth.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *texto(const cJSON *obj, const char *chave) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, chave);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

static int numero(const cJSON *obj, const char *chave, int padrao) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, chave);
    return cJSON_IsNumber(item) ? item->valueint : padrao;
}

static void copiar(char *destino, size_t tam, const char *origem) {
    snprintf(destino, tam, "%s", origem ? origem : "");
}

bool auth_parse_codigo(const char *json, AuthCodigo *c) {
    memset(c, 0, sizeof *c);
    cJSON *raiz = cJSON_Parse(json);
    if (!raiz)
        return false;

    const char *usuario = texto(raiz, "user_code");
    const char *dispositivo = texto(raiz, "device_code");
    bool ok = usuario && dispositivo && strlen(dispositivo) < sizeof c->codigo_dispositivo;
    if (ok) {
        copiar(c->codigo_usuario, sizeof c->codigo_usuario, usuario);
        copiar(c->codigo_dispositivo, sizeof c->codigo_dispositivo, dispositivo);
        copiar(c->endereco, sizeof c->endereco, texto(raiz, "verification_uri"));
        if (!c->endereco[0])
            copiar(c->endereco, sizeof c->endereco, "https://www.microsoft.com/link");
        c->expira_em_s = numero(raiz, "expires_in", 900);
        c->intervalo_s = numero(raiz, "interval", 5);
        if (c->intervalo_s < 1)
            c->intervalo_s = 5;
    }
    cJSON_Delete(raiz);
    return ok;
}

AuthStatus auth_parse_token(long status, const char *json, AuthConta *conta) {
    cJSON *raiz = json ? cJSON_Parse(json) : NULL;
    AuthStatus resultado = AUTH_ERRO;

    if (status == 200 && raiz) {
        const char *access = texto(raiz, "access_token");
        const char *refresh = texto(raiz, "refresh_token");
        if (access && refresh) {
            free(conta->access_token);
            free(conta->refresh_token);
            conta->access_token = strdup(access);
            conta->refresh_token = strdup(refresh);
            conta->obtido_em = time(NULL);
            resultado = AUTH_OK;
        }
    } else if (raiz) {
        const char *erro = texto(raiz, "error");
        if (erro) {
            if (!strcmp(erro, "authorization_pending"))
                resultado = AUTH_PENDENTE;
            else if (!strcmp(erro, "slow_down"))
                resultado = AUTH_LENTO;
            else if (!strcmp(erro, "authorization_declined") || !strcmp(erro, "access_denied"))
                resultado = AUTH_RECUSADO;
            else if (!strcmp(erro, "expired_token") || !strcmp(erro, "code_expired"))
                resultado = AUTH_EXPIRADO;
            else if (!strcmp(erro, "invalid_grant"))
                resultado = AUTH_INVALIDO;
        }
    }

    cJSON_Delete(raiz);
    return resultado;
}

char *auth_parse_xbl_token(const char *json, char *gamertag, int tam_gamertag) {
    if (gamertag && tam_gamertag > 0)
        gamertag[0] = '\0';
    cJSON *raiz = json ? cJSON_Parse(json) : NULL;
    if (!raiz)
        return NULL;

    const char *token = texto(raiz, "Token");
    char *copia = token ? strdup(token) : NULL;

    // DisplayClaims.xui[0].gtg (gamertag), quando o servidor envia
    if (gamertag && tam_gamertag > 0) {
        const cJSON *claims = cJSON_GetObjectItemCaseSensitive(raiz, "DisplayClaims");
        const cJSON *xui = cJSON_GetObjectItemCaseSensitive(claims, "xui");
        const cJSON *primeiro = cJSON_IsArray(xui) ? cJSON_GetArrayItem(xui, 0) : NULL;
        const char *gtg = primeiro ? texto(primeiro, "gtg") : NULL;
        if (gtg)
            snprintf(gamertag, (size_t)tam_gamertag, "%s", gtg);
    }

    cJSON_Delete(raiz);
    return copia;
}

bool auth_parse_streaming(const char *json, AuthStreaming *s) {
    cJSON *raiz = json ? cJSON_Parse(json) : NULL;
    if (!raiz)
        return false;

    const char *gs = texto(raiz, "gsToken");
    const cJSON *ofertas = cJSON_GetObjectItemCaseSensitive(raiz, "offeringSettings");
    const cJSON *regioes = cJSON_GetObjectItemCaseSensitive(ofertas, "regions");

    // Usa a região marcada como padrão; se nenhuma estiver, a primeira.
    // Todas vão para s->regioes (diagnóstico: o xCloud costuma trazer várias).
    const char *base = NULL, *nome = NULL;
    bool achou_padrao = false;
    char lista[sizeof s->regioes] = "";
    const cJSON *regiao;
    cJSON_ArrayForEach(regiao, regioes) {
        const char *uri = texto(regiao, "baseUri");
        if (!uri)
            continue;
        const char *n = texto(regiao, "name");
        bool padrao = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(regiao, "isDefault"));
        size_t usado = strlen(lista);
        snprintf(lista + usado, sizeof lista - usado, "%s%s%s", usado ? ", " : "", n ? n : "?",
                 padrao ? "*" : "");
        if (!base || (padrao && !achou_padrao)) {
            base = uri;
            nome = n;
            achou_padrao = padrao;
        }
    }

    bool ok = gs && base && strlen(base) < sizeof s->base_uri;
    if (ok) {
        free(s->gs_token);
        s->gs_token = strdup(gs);
        snprintf(s->base_uri, sizeof s->base_uri, "%s", base);
        copiar(s->regiao, sizeof s->regiao, nome);
        copiar(s->regioes, sizeof s->regioes, lista);
        s->obtido_em = time(NULL);
        size_t n = strlen(s->base_uri);
        if (n > 0 && s->base_uri[n - 1] == '/')
            s->base_uri[n - 1] = '\0';
        s->duracao_s = numero(raiz, "durationInSeconds", 0);
    }
    cJSON_Delete(raiz);
    return ok;
}

const char *auth_descrever_xerr(long long xerr) {
    switch (xerr) {
        case 2148916227LL: return "Conta banida do Xbox Live.";
        case 2148916229LL: return "Conta de crianca sem permissao para jogar online.";
        case 2148916233LL: return "Esta conta Microsoft nao tem perfil Xbox. Crie um em xbox.com.";
        case 2148916234LL: return "Aceite os termos de uso em xbox.com antes de continuar.";
        case 2148916235LL: return "Xbox Live nao esta disponivel no seu pais.";
        case 2148916236LL:
        case 2148916237LL: return "A conta precisa de verificacao de idade em xbox.com.";
        case 2148916238LL: return "Conta de crianca: precisa ser adicionada a uma familia Microsoft.";
        default: return NULL;
    }
}

char *auth_parse_token_console(long status, const char *json) {
    if (status != 200 || !json)
        return NULL;
    cJSON *raiz = cJSON_Parse(json);
    const char *token = raiz ? texto(raiz, "access_token") : NULL;
    char *copia = token ? strdup(token) : NULL;
    cJSON_Delete(raiz);
    return copia;
}

void auth_conta_liberar(AuthConta *conta) {
    free(conta->access_token);
    free(conta->refresh_token);
    conta->access_token = conta->refresh_token = NULL;
    conta->obtido_em = 0;
}

void auth_streaming_liberar(AuthStreaming *s) {
    free(s->gs_token);
    s->gs_token = NULL;
}

// DisplayClaims.xui[0].uhs (o "user hash" do cabeçalho XBL3.0)
bool auth_parse_xbl_uhs(const char *json, char *uhs, int tam) {
    uhs[0] = '\0';
    cJSON *raiz = json ? cJSON_Parse(json) : NULL;
    const cJSON *claims = cJSON_GetObjectItemCaseSensitive(raiz, "DisplayClaims");
    const cJSON *xui = cJSON_GetObjectItemCaseSensitive(claims, "xui");
    const cJSON *primeiro = cJSON_IsArray(xui) ? cJSON_GetArrayItem(xui, 0) : NULL;
    const char *v = primeiro ? texto(primeiro, "uhs") : NULL;
    if (v)
        snprintf(uhs, (size_t)tam, "%s", v);
    cJSON_Delete(raiz);
    return uhs[0] != '\0';
}

// profileUsers[0].settings[id = GameDisplayPicRaw].value
bool auth_parse_foto(const char *json, char *url, int tam) {
    url[0] = '\0';
    cJSON *raiz = json ? cJSON_Parse(json) : NULL;
    const cJSON *usuarios = cJSON_GetObjectItemCaseSensitive(raiz, "profileUsers");
    const cJSON *u = cJSON_IsArray(usuarios) ? cJSON_GetArrayItem(usuarios, 0) : NULL;
    const cJSON *ajustes = cJSON_GetObjectItemCaseSensitive(u, "settings");
    const cJSON *a;
    cJSON_ArrayForEach(a, ajustes) {
        const char *id = texto(a, "id"), *v = texto(a, "value");
        if (id && v && !strcmp(id, "GameDisplayPicRaw") && !strncmp(v, "https://", 8)) {
            snprintf(url, (size_t)tam, "%s", v);
            break;
        }
    }
    cJSON_Delete(raiz);
    return url[0] != '\0';
}
