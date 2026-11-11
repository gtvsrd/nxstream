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

// ---------------------------------------------------------------------------
// Negociação WebRTC (docs/PROTOCOLO.md, seção 3)
// ---------------------------------------------------------------------------
#include <stdlib.h>

char *xapi_corpo_sdp(const char *oferta) {
    cJSON *raiz = cJSON_CreateObject();
    cJSON_AddStringToObject(raiz, "messageType", "offer");
    cJSON_AddStringToObject(raiz, "sdp", oferta);
    cJSON_AddStringToObject(raiz, "requestId", "1");
    cJSON *cfg = cJSON_AddObjectToObject(raiz, "configuration");

    cJSON *chat_cfg = cJSON_AddObjectToObject(cfg, "chatConfiguration");
    cJSON_AddNumberToObject(chat_cfg, "bytesPerSample", 2);
    cJSON_AddNumberToObject(chat_cfg, "expectedClipDurationMs", 20);
    cJSON *formato = cJSON_AddObjectToObject(chat_cfg, "format");
    cJSON_AddStringToObject(formato, "codec", "opus");
    cJSON_AddStringToObject(formato, "container", "webm");
    cJSON_AddNumberToObject(chat_cfg, "numChannels", 1);
    cJSON_AddNumberToObject(chat_cfg, "sampleFrequencyHz", 24000);

    static const struct { const char *nome; int min, max; } versoes[] = {
        {"chat", 1, 1}, {"control", 1, 3}, {"input", 1, 9}, {"message", 1, 1},
        {"reliableinput", 9, 9}, {"unreliableinput", 9, 9},
    };
    for (size_t i = 0; i < sizeof versoes / sizeof versoes[0]; i++) {
        cJSON *v = cJSON_AddObjectToObject(cfg, versoes[i].nome);
        cJSON_AddNumberToObject(v, "minVersion", versoes[i].min);
        cJSON_AddNumberToObject(v, "maxVersion", versoes[i].max);
    }
    char *json = cJSON_PrintUnformatted(raiz);
    cJSON_Delete(raiz);
    return json;
}

// exchangeResponse é um texto JSON dentro do JSON da resposta
static cJSON *ler_exchange(const char *json, char *erro, int tam_erro) {
    if (erro && tam_erro > 0)
        erro[0] = '\0';
    cJSON *raiz = json ? cJSON_Parse(json) : NULL;
    if (!raiz) {
        if (erro) snprintf(erro, (size_t)tam_erro, "resposta nao e JSON");
        return NULL;
    }
    const cJSON *det = cJSON_GetObjectItemCaseSensitive(raiz, "errorDetails");
    const cJSON *msg = cJSON_GetObjectItemCaseSensitive(det, "message");
    const cJSON *cod = cJSON_GetObjectItemCaseSensitive(det, "code");
    if (cJSON_IsString(msg) || cJSON_IsString(cod)) {
        if (erro)
            snprintf(erro, (size_t)tam_erro, "%s%s%s", cJSON_IsString(cod) ? cod->valuestring : "",
                     cJSON_IsString(cod) && cJSON_IsString(msg) ? ": " : "",
                     cJSON_IsString(msg) ? msg->valuestring : "");
        cJSON_Delete(raiz);
        return NULL;
    }
    const cJSON *troca = cJSON_GetObjectItemCaseSensitive(raiz, "exchangeResponse");
    cJSON *interno = cJSON_IsString(troca) ? cJSON_Parse(troca->valuestring) : NULL;
    if (!interno && erro)
        snprintf(erro, (size_t)tam_erro, "sem exchangeResponse valido");
    cJSON_Delete(raiz);
    return interno;
}

int xapi_parse_resposta_sdp(const char *json, char **sdp, char *erro, int tam_erro) {
    *sdp = NULL;
    cJSON *interno = ler_exchange(json, erro, tam_erro);
    if (!interno)
        return -1;
    const cJSON *texto = cJSON_GetObjectItemCaseSensitive(interno, "sdp");
    const cJSON *status = cJSON_GetObjectItemCaseSensitive(interno, "status");
    if (cJSON_IsString(texto) && texto->valuestring[0]) {
        *sdp = strdup(texto->valuestring);
    } else if (erro) {
        snprintf(erro, (size_t)tam_erro, "resposta sem SDP (status %s)",
                 cJSON_IsString(status) ? status->valuestring : "?");
    }
    cJSON_Delete(interno);
    return *sdp ? 1 : -1;
}

char *xapi_corpo_ice(const XCandidatos *locais, const char *ufrag) {
    cJSON *raiz = cJSON_CreateObject();
    cJSON *lista = cJSON_AddArrayToObject(raiz, "candidates");
    for (int i = 0; i < locais->total; i++) {
        // O navegador envia "candidate:..." sem o "a="
        const char *c = locais->cand[i];
        if (!strncmp(c, "a=", 2))
            c += 2;
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "candidate", c);
        cJSON_AddStringToObject(item, "sdpMid", locais->mid[i][0] ? locais->mid[i] : "0");
        cJSON_AddNumberToObject(item, "sdpMLineIndex", atoi(locais->mid[i]));
        cJSON_AddStringToObject(item, "usernameFragment", ufrag ? ufrag : "");
        char *texto = cJSON_PrintUnformatted(item);
        cJSON_Delete(item);
        cJSON_AddItemToArray(lista, cJSON_CreateString(texto ? texto : "{}"));
        free(texto);
    }
    char *json = cJSON_PrintUnformatted(raiz);
    cJSON_Delete(raiz);
    return json;
}

// Lê um endereço IPv6 em texto (com ou sem "::") para 16 bytes.
// Implementação própria: não depende do inet_pton da plataforma (a libnx pode
// não suportar IPv6 nele) e roda igual no PC e no Switch.
static bool ler_ipv6(const char *texto, unsigned char b[16]) {
    unsigned grupos[8];
    int antes = 0, depois = 0, pos_compressao = -1;
    unsigned grupos_depois[8];
    const char *p = texto;

    if (p[0] == ':' && p[1] == ':') {
        pos_compressao = 0;
        p += 2;
    }
    while (*p) {
        char *fim;
        unsigned long v = strtoul(p, &fim, 16);
        if (fim == p || fim - p > 4 || v > 0xFFFF)
            return false;
        if (pos_compressao >= 0) {
            if (depois >= 8) return false;
            grupos_depois[depois++] = (unsigned)v;
        } else {
            if (antes >= 8) return false;
            grupos[antes++] = (unsigned)v;
        }
        p = fim;
        if (*p == ':') {
            if (p[1] == ':') {
                if (pos_compressao >= 0) return false; // "::" só uma vez
                pos_compressao = antes;
                p += 2;
            } else {
                p++;
                if (!*p) return false;
            }
        } else if (*p) {
            return false;
        }
    }
    int total = antes + depois;
    if (pos_compressao < 0 ? total != 8 : total > 7)
        return false;
    int zeros = 8 - total;
    for (int i = 0; i < zeros; i++)
        grupos[antes + i] = 0;
    for (int i = 0; i < depois; i++)
        grupos[antes + zeros + i] = grupos_depois[i];
    for (int i = 0; i < 8; i++) {
        b[2 * i] = (unsigned char)(grupos[i] >> 8);
        b[2 * i + 1] = (unsigned char)(grupos[i] & 0xFF);
    }
    return true;
}

bool xapi_teredo(const char *ipv6, char *ipv4, int tam_ipv4, int *porta) {
    unsigned char b[16];
    if (strncmp(ipv6, "2001:", 5) != 0 || !ler_ipv6(ipv6, b))
        return false;
    if (b[0] != 0x20 || b[1] != 0x01 || b[2] != 0x00 || b[3] != 0x00)
        return false; // Teredo é 2001:0000::/32
    // Porta e IPv4 do cliente vêm invertidos bit a bit (RFC 4380)
    *porta = (~((b[10] << 8) | b[11])) & 0xFFFF;
    snprintf(ipv4, (size_t)tam_ipv4, "%u.%u.%u.%u", (unsigned)(~b[12] & 0xFF),
             (unsigned)(~b[13] & 0xFF), (unsigned)(~b[14] & 0xFF), (unsigned)(~b[15] & 0xFF));
    return true;
}

static void adicionar(XCandidatos *r, const char *cand, const char *mid) {
    if (r->total >= XAPI_MAX_CAND)
        return;
    snprintf(r->cand[r->total], sizeof r->cand[0], "%s", cand);
    snprintf(r->mid[r->total], sizeof r->mid[0], "%s", mid ? mid : "0");
    r->total++;
}

int xapi_parse_resposta_ice(const char *json, XCandidatos *r) {
    memset(r, 0, sizeof *r);
    cJSON *interno = ler_exchange(json, NULL, 0);
    if (!cJSON_IsArray(interno)) {
        cJSON_Delete(interno);
        return -1;
    }
    const cJSON *item;
    cJSON_ArrayForEach(item, interno) {
        const cJSON *c = cJSON_GetObjectItemCaseSensitive(item, "candidate");
        const cJSON *m = cJSON_GetObjectItemCaseSensitive(item, "sdpMid");
        if (!cJSON_IsString(c) || strstr(c->valuestring, "end-of-candidates"))
            continue;
        const char *mid = cJSON_IsString(m) ? m->valuestring : "0";

        // Campo 5 (índice 4) do candidato é o endereço
        char copia[256], *campos[8] = {0};
        snprintf(copia, sizeof copia, "%s", c->valuestring);
        int n = 0;
        for (char *t = strtok(copia, " "); t && n < 8; t = strtok(NULL, " "))
            campos[n++] = t;

        char ipv4[16];
        int porta;
        if (n > 4 && xapi_teredo(campos[4], ipv4, sizeof ipv4, &porta)) {
            char extra[256];
            snprintf(extra, sizeof extra, "a=candidate:10 1 UDP 1 %s 9002 typ host", ipv4);
            adicionar(r, extra, mid);
            snprintf(extra, sizeof extra, "a=candidate:11 1 UDP 1 %s %d typ host", ipv4, porta);
            adicionar(r, extra, mid);
        }
        adicionar(r, c->valuestring, mid);
    }
    cJSON_Delete(interno);
    return r->total;
}

// Extrai o endereço (5º campo) de "[a=]candidate:<fund> <comp> <proto> <prio> <endereço> <porta> typ ..."
static bool campo_endereco(const char *cand, char *endereco, size_t tam, int *porta) {
    const char *c = strncmp(cand, "a=", 2) == 0 ? cand + 2 : cand;
    char fundacao[64], proto[16], end[128];
    unsigned componente;
    unsigned long prioridade;
    int p;
    if (sscanf(c, "candidate:%63s %u %15s %lu %127s %d", fundacao, &componente, proto, &prioridade, end, &p) != 6)
        return false;
    snprintf(endereco, tam, "%s", end);
    *porta = p;
    return true;
}

static bool ipv4_rede_local(const char *texto) {
    unsigned a, b, c, d;
    char sobra;
    if (sscanf(texto, "%u.%u.%u.%u%c", &a, &b, &c, &d, &sobra) != 4 || a > 255 || b > 255 || c > 255 || d > 255)
        return false; // não é IPv4 numérico (IPv6, nome, lixo)
    return a == 10 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168) || (a == 169 && b == 254);
}

int xapi_filtrar_rede_local(XCandidatos *r) {
    XCandidatos filtrado;
    memset(&filtrado, 0, sizeof filtrado);
    char vistos[XAPI_MAX_CAND][160];
    for (int i = 0; i < r->total; i++) {
        char endereco[128];
        int porta;
        if (!campo_endereco(r->cand[i], endereco, sizeof endereco, &porta) || !ipv4_rede_local(endereco))
            continue;
        char chave[160]; // endereço (até 127) + ":" + porta
        snprintf(chave, sizeof chave, "%s:%d", endereco, porta);
        bool repetido = false;
        for (int j = 0; j < filtrado.total; j++)
            if (!strcmp(vistos[j], chave))
                repetido = true;
        if (repetido)
            continue;
        snprintf(vistos[filtrado.total], sizeof vistos[0], "%s", chave);
        snprintf(filtrado.cand[filtrado.total], sizeof filtrado.cand[0], "%s", r->cand[i]);
        snprintf(filtrado.mid[filtrado.total], sizeof filtrado.mid[0], "%s", r->mid[i]);
        filtrado.total++;
    }
    *r = filtrado;
    return r->total;
}
