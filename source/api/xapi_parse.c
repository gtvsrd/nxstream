// Interpretação das respostas da API de streaming. Sem rede: testável no PC.
#include "xapi.h"
#include "config.h"
#include "cJSON.h"

#include <stdio.h>
#include <string.h>

static void copiar(char *destino, size_t tam, const cJSON *obj, const char *chave) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, chave);
    snprintf(destino, tam, "%s", cJSON_IsString(item) ? item->valuestring : "");
}

bool xapi_parse_consoles(const char *json, XConsoles *c) {
    memset(c, 0, sizeof *c);
    cJSON *raiz = json ? cJSON_Parse(json) : NULL;
    if (!raiz)
        return false;

    const cJSON *resultados = cJSON_GetObjectItemCaseSensitive(raiz, "results");
    bool ok = cJSON_IsArray(resultados);
    const cJSON *item;
    cJSON_ArrayForEach(item, resultados) {
        if (c->total >= XAPI_MAX_CONSOLES)
            break;
        XConsole *x = &c->itens[c->total];
        copiar(x->nome, sizeof x->nome, item, "deviceName");
        copiar(x->server_id, sizeof x->server_id, item, "serverId");
        copiar(x->estado, sizeof x->estado, item, "powerState");
        copiar(x->tipo, sizeof x->tipo, item, "consoleType");
        if (x->server_id[0])
            c->total++;
    }
    cJSON_Delete(raiz);
    return ok;
}

const char *xapi_estado_amigavel(const char *estado) {
    if (!strcmp(estado, "On"))               return "Ligado";
    if (!strcmp(estado, "ConnectedStandby")) return "Em repouso";
    if (!strcmp(estado, "Off"))              return "Desligado";
    if (!strcmp(estado, "Unknown") || !estado[0]) return "Desconhecido";
    return estado;
}

const char *xapi_tipo_amigavel(const char *tipo) {
    if (!strcmp(tipo, "XboxSeriesX")) return "Xbox Series X";
    if (!strcmp(tipo, "XboxSeriesS")) return "Xbox Series S";
    if (!strcmp(tipo, "XboxOneX"))    return "Xbox One X";
    if (!strcmp(tipo, "XboxOneS"))    return "Xbox One S";
    if (!strcmp(tipo, "XboxOne"))     return "Xbox One";
    return tipo[0] ? tipo : "Xbox";
}

// ---------------------------------------------------------------------------
// Sessão
// ---------------------------------------------------------------------------
bool xapi_parse_sessao(const char *json, XSessao *s) {
    memset(s, 0, sizeof *s);
    cJSON *raiz = json ? cJSON_Parse(json) : NULL;
    if (!raiz)
        return false;

    const cJSON *caminho = cJSON_GetObjectItemCaseSensitive(raiz, "sessionPath");
    bool ok = cJSON_IsString(caminho) && caminho->valuestring[0] &&
              strlen(caminho->valuestring) < sizeof s->caminho;
    if (ok) {
        const char *c = caminho->valuestring;
        while (*c == '/')
            c++; // guarda sem a barra inicial
        snprintf(s->caminho, sizeof s->caminho, "%s", c);
        const char *ultima = strrchr(s->caminho, '/');
        snprintf(s->id, sizeof s->id, "%s", ultima ? ultima + 1 : s->caminho);
    }
    cJSON_Delete(raiz);
    return ok;
}

XSessaoEstado xapi_parse_estado(const char *json, char *detalhe, int tam) {
    if (detalhe && tam > 0)
        detalhe[0] = '\0';
    cJSON *raiz = json ? cJSON_Parse(json) : NULL;
    if (!raiz)
        return XSESSAO_ERRO;

    XSessaoEstado st = XSESSAO_DESCONHECIDO;
    const cJSON *estado = cJSON_GetObjectItemCaseSensitive(raiz, "state");
    const char *e = cJSON_IsString(estado) ? estado->valuestring : "";

    if (!strcmp(e, "Provisioning"))             st = XSESSAO_PREPARANDO;
    else if (!strcmp(e, "ReadyToConnect"))      st = XSESSAO_AGUARDA_LOGIN;
    else if (!strcmp(e, "Provisioned"))         st = XSESSAO_PRONTA;
    else if (!strcmp(e, "WaitingForResources")) st = XSESSAO_NA_FILA;
    else if (!strcmp(e, "Failed"))              st = XSESSAO_FALHOU;

    if (detalhe && tam > 0) {
        const cJSON *erro = cJSON_GetObjectItemCaseSensitive(raiz, "errorDetails");
        const cJSON *codigo = cJSON_GetObjectItemCaseSensitive(erro, "code");
        const cJSON *msg = cJSON_GetObjectItemCaseSensitive(erro, "message");
        if (st == XSESSAO_FALHOU && (cJSON_IsString(msg) || cJSON_IsString(codigo)))
            snprintf(detalhe, (size_t)tam, "%s%s%s",
                     cJSON_IsString(codigo) ? codigo->valuestring : "",
                     cJSON_IsString(codigo) && cJSON_IsString(msg) ? ": " : "",
                     cJSON_IsString(msg) ? msg->valuestring : "");
        else
            snprintf(detalhe, (size_t)tam, "%s", e[0] ? e : "(sem estado)");
    }
    cJSON_Delete(raiz);
    return st;
}

char *xapi_corpo_play(const char *server_id) {
    cJSON *raiz = cJSON_CreateObject();
    cJSON_AddStringToObject(raiz, "clientSessionId", "");
    cJSON_AddStringToObject(raiz, "titleId", "");
    cJSON_AddStringToObject(raiz, "systemUpdateGroup", "");
    cJSON *cfg = cJSON_AddObjectToObject(raiz, "settings");
    cJSON_AddStringToObject(cfg, "nanoVersion", "V3;WebrtcTransport.dll");
    cJSON_AddFalseToObject(cfg, "enableOptionalDataCollection");
    cJSON_AddFalseToObject(cfg, "enableTextToSpeech");
    cJSON_AddNumberToObject(cfg, "highContrast", 0);
    cJSON_AddStringToObject(cfg, "locale", NX_IDIOMA);
    cJSON_AddFalseToObject(cfg, "useIceConnection");
    cJSON_AddNumberToObject(cfg, "timezoneOffsetMinutes", NX_FUSO_MINUTOS);
    cJSON_AddStringToObject(cfg, "sdkType", "web");
    cJSON_AddStringToObject(cfg, "osName", "windows");
    cJSON_AddStringToObject(raiz, "serverId", server_id);
    cJSON_AddArrayToObject(raiz, "fallbackRegionNames");
    char *json = cJSON_PrintUnformatted(raiz);
    cJSON_Delete(raiz);
    return json;
}

const char *xapi_sessao_estado_texto(XSessaoEstado e) {
    switch (e) {
        case XSESSAO_PREPARANDO:    return "Preparando o console";
        case XSESSAO_AGUARDA_LOGIN: return "Autorizando acesso ao console";
        case XSESSAO_PRONTA:        return "Sessao pronta";
        case XSESSAO_NA_FILA:       return "Na fila";
        case XSESSAO_FALHOU:        return "Falhou";
        case XSESSAO_DESCONHECIDO:  return "Estado desconhecido";
        default:                    return "Sem resposta";
    }
}
