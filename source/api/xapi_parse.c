// Interpretação das respostas da API de streaming. Sem rede: testável no PC.
#include "xapi.h"
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
