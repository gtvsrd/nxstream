// xCloud: interpretação das respostas (sem rede; testado no PC em tests/)
#include "xnuvem.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *texto(const cJSON *obj, const char *chave) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, chave);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

static void copiar(char *destino, size_t tam, const char *origem) {
    snprintf(destino, tam, "%s", origem ? origem : "");
}

bool xnuvem_parse_titulos(const char *json, XTitulos *t) {
    cJSON *raiz = json ? cJSON_Parse(json) : NULL;
    const cJSON *lista = cJSON_GetObjectItemCaseSensitive(raiz, "results");
    if (!cJSON_IsArray(lista)) {
        cJSON_Delete(raiz);
        return false;
    }
    int n = cJSON_GetArraySize(lista);
    XTitulo *itens = calloc(n > 0 ? (size_t)n : 1, sizeof *itens);
    if (!itens) {
        cJSON_Delete(raiz);
        return false;
    }
    int total = 0;
    const cJSON *el;
    cJSON_ArrayForEach(el, lista) {
        const char *id = texto(el, "titleId");
        if (!id || !id[0])
            continue;
        const cJSON *det = cJSON_GetObjectItemCaseSensitive(el, "details");
        XTitulo *x = &itens[total++];
        copiar(x->titulo_id, sizeof x->titulo_id, id);
        copiar(x->produto_id, sizeof x->produto_id, texto(det, "productId"));
        x->tem_direito = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(det, "hasEntitlement")) ||
                         cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(det, "isFreeInStore"));
    }
    cJSON_Delete(raiz);

    free(t->itens);
    memset(t, 0, sizeof *t);
    t->itens = itens;
    t->total = total;
    t->total_servidor = total;
    return true;
}

int xnuvem_parse_recentes(const char *json, XTitulos *t) {
    cJSON *raiz = json ? cJSON_Parse(json) : NULL;
    const cJSON *lista = cJSON_GetObjectItemCaseSensitive(raiz, "results");
    if (!cJSON_IsArray(lista)) {
        cJSON_Delete(raiz);
        return -1;
    }
    int posicao = 0, marcados = 0;
    const cJSON *el;
    cJSON_ArrayForEach(el, lista) {
        const char *id = texto(el, "titleId");
        if (!id)
            continue;
        posicao++;
        for (int i = 0; i < t->total; i++) {
            if (!strcasecmp(t->itens[i].titulo_id, id) && !t->itens[i].recente) {
                t->itens[i].recente = posicao;
                marcados++;
                break;
            }
        }
    }
    cJSON_Delete(raiz);
    t->recentes = marcados;
    return marcados;
}

int xnuvem_sem_nome(const XTitulos *t) {
    int n = 0;
    for (int i = 0; i < t->total; i++)
        n += !t->itens[i].nome[0] && t->itens[i].produto_id[0];
    return n;
}

char *xnuvem_corpo_catalogo(const XTitulos *t, int inicio, int n) {
    cJSON *corpo = cJSON_CreateObject();
    cJSON *lista = cJSON_AddArrayToObject(corpo, "Products");
    int k = 0; // índice entre os jogos sem nome
    for (int i = 0; i < t->total && k < inicio + n; i++) {
        if (t->itens[i].nome[0] || !t->itens[i].produto_id[0])
            continue;
        if (k++ >= inicio)
            cJSON_AddItemToArray(lista, cJSON_CreateString(t->itens[i].produto_id));
    }
    char *json = cJSON_PrintUnformatted(corpo);
    cJSON_Delete(corpo);
    return json;
}

char *xnuvem_cache_montar(const XTitulos *t, const char *idioma) {
    cJSON *raiz = cJSON_CreateObject();
    cJSON_AddNumberToObject(raiz, "versao", 2);
    cJSON_AddStringToObject(raiz, "idioma", idioma);
    cJSON *produtos = cJSON_AddObjectToObject(raiz, "produtos");
    for (int i = 0; i < t->total; i++) {
        const XTitulo *x = &t->itens[i];
        if (!x->nome[0] || !x->produto_id[0] || cJSON_GetObjectItemCaseSensitive(produtos, x->produto_id))
            continue;
        cJSON *par = cJSON_CreateArray();
        cJSON_AddItemToArray(par, cJSON_CreateString(x->nome));
        cJSON_AddItemToArray(par, cJSON_CreateString(x->editora));
        cJSON_AddItemToArray(par, cJSON_CreateString(x->capa));
        cJSON_AddItemToObject(produtos, x->produto_id, par);
    }
    char *json = cJSON_PrintUnformatted(raiz);
    cJSON_Delete(raiz);
    return json;
}

int xnuvem_cache_aplicar(const char *json, const char *idioma, XTitulos *t) {
    cJSON *raiz = json ? cJSON_Parse(json) : NULL;
    const char *id = texto(raiz, "idioma");
    const cJSON *produtos = cJSON_GetObjectItemCaseSensitive(raiz, "produtos");
    const cJSON *versao = cJSON_GetObjectItemCaseSensitive(raiz, "versao");
    // v1 (sem capas) é descartado: o catálogo é consultado de novo uma vez
    if (!id || strcmp(id, idioma) || !cJSON_IsObject(produtos) || !cJSON_IsNumber(versao) ||
        versao->valuedouble < 2) {
        cJSON_Delete(raiz);
        return -1;
    }
    int aplicados = 0;
    for (int i = 0; i < t->total; i++) {
        XTitulo *x = &t->itens[i];
        if (!x->produto_id[0])
            continue;
        const cJSON *par = cJSON_GetObjectItemCaseSensitive(produtos, x->produto_id);
        const cJSON *nome = cJSON_GetArrayItem(par, 0), *editora = cJSON_GetArrayItem(par, 1),
                    *capa = cJSON_GetArrayItem(par, 2);
        if (!cJSON_IsString(nome) || !nome->valuestring[0])
            continue;
        copiar(x->nome, sizeof x->nome, nome->valuestring);
        copiar(x->editora, sizeof x->editora, cJSON_IsString(editora) ? editora->valuestring : "");
        copiar(x->capa, sizeof x->capa, cJSON_IsString(capa) ? capa->valuestring : "");
        aplicados++;
    }
    cJSON_Delete(raiz);
    return aplicados;
}

// Resposta: {"Products":{"9NXXXX":{"ProductTitle":..,"PublisherName":..,"StoreId":..,"XCloudTitleId":..}}}
int xnuvem_parse_catalogo(const char *json, XTitulos *t) {
    cJSON *raiz = json ? cJSON_Parse(json) : NULL;
    const cJSON *produtos = cJSON_GetObjectItemCaseSensitive(raiz, "Products");
    if (!cJSON_IsObject(produtos)) {
        cJSON_Delete(raiz);
        return -1;
    }
    int com_nome = 0;
    const cJSON *p;
    cJSON_ArrayForEach(p, produtos) {
        const char *nome = texto(p, "ProductTitle");
        if (!nome || !nome[0])
            continue;
        const char *loja = texto(p, "StoreId");
        const char *nuvem = texto(p, "XCloudTitleId");
        for (int i = 0; i < t->total; i++) {
            XTitulo *x = &t->itens[i];
            bool mesmo = (x->produto_id[0] && ((p->string && !strcasecmp(x->produto_id, p->string)) ||
                                               (loja && !strcasecmp(x->produto_id, loja)))) ||
                         (nuvem && !strcasecmp(x->titulo_id, nuvem));
            if (!mesmo)
                continue;
            if (!x->nome[0])
                com_nome++;
            copiar(x->nome, sizeof x->nome, nome);
            copiar(x->editora, sizeof x->editora, texto(p, "PublisherName"));
            // Pôster (2:3); se não houver, o quadrado. O catálogo manda "//host/..."
            const char *url = texto(cJSON_GetObjectItemCaseSensitive(p, "Image_Poster"), "URL");
            if (!url || !url[0])
                url = texto(cJSON_GetObjectItemCaseSensitive(p, "Image_Tile"), "URL");
            if (url && url[0])
                snprintf(x->capa, sizeof x->capa, "%s%s", strncmp(url, "//", 2) ? "" : "https:", url);
        }
    }
    cJSON_Delete(raiz);
    return com_nome;
}

static int comparar(const void *a, const void *b) {
    const XTitulo *x = a, *y = b;
    if (x->recente || y->recente) {
        if (!x->recente) return 1;
        if (!y->recente) return -1;
        return x->recente - y->recente;
    }
    if (!x->nome[0] || !y->nome[0]) { // sem nome vão para o fim
        if (x->nome[0]) return -1;
        if (y->nome[0]) return 1;
        return strcasecmp(x->titulo_id, y->titulo_id);
    }
    int c = strcasecmp(x->nome, y->nome);
    return c ? c : strcasecmp(x->titulo_id, y->titulo_id);
}

void xnuvem_filtrar_ordenar(XTitulos *t) {
    int n = 0, recentes = 0, com_nome = 0;
    for (int i = 0; i < t->total; i++) {
        if (!t->itens[i].tem_direito)
            continue;
        if (n != i)
            t->itens[n] = t->itens[i];
        recentes += t->itens[n].recente > 0;
        com_nome += t->itens[n].nome[0] != '\0';
        n++;
    }
    t->total = n;
    t->recentes = recentes;
    t->com_nome = com_nome;
    if (n > 1)
        qsort(t->itens, (size_t)n, sizeof *t->itens, comparar);
}

void xnuvem_liberar(XTitulos *t) {
    free(t->itens);
    memset(t, 0, sizeof *t);
}

// Próximo caractere em minúsculas e sem acento (UTF-8; Latin-1 suplementar).
// Outros caracteres de vários bytes passam como estão, byte a byte.
static int dobrar(const char **s) {
    const unsigned char *p = (const unsigned char *)*s;
    if (!*p)
        return 0;
    if (p[0] == 0xC3 && p[1] >= 0x80 && p[1] <= 0xBF) {
        static const char mapa[] = "aaaaaaaceeeeiiiidnooooo/ouuuuyps" // À..ß (0x80..0x9F)
                                   "aaaaaaaceeeeiiiidnooooo/ouuuuypy"; // à..ÿ (0xA0..0xBF)
        *s += 2;
        return (unsigned char)mapa[p[1] - 0x80];
    }
    *s += 1;
    return (p[0] >= 'A' && p[0] <= 'Z') ? p[0] + 32 : p[0];
}

bool xnuvem_combina(const char *nome, const char *busca) {
    if (!busca || !busca[0])
        return true;
    for (const char *ini = nome; *ini;) {
        const char *a = ini, *b = busca;
        for (;;) {
            if (!*b)
                return true;
            int ca = dobrar(&a), cb = dobrar(&b);
            if (!ca || ca != cb)
                break;
        }
        dobrar(&ini); // avança um caractere inteiro
    }
    return false;
}

char *xnuvem_lista_montar(const XTitulos *t) {
    cJSON *raiz = cJSON_CreateObject();
    cJSON_AddNumberToObject(raiz, "versao", 1);
    cJSON *jogos = cJSON_AddArrayToObject(raiz, "jogos");
    for (int i = 0; i < t->total; i++) {
        const XTitulo *x = &t->itens[i];
        if (!x->tem_direito)
            continue;
        cJSON *j = cJSON_CreateArray();
        cJSON_AddItemToArray(j, cJSON_CreateString(x->titulo_id));
        cJSON_AddItemToArray(j, cJSON_CreateString(x->produto_id));
        cJSON_AddItemToArray(j, cJSON_CreateNumber(x->recente));
        cJSON_AddItemToArray(jogos, j);
    }
    char *json = cJSON_PrintUnformatted(raiz);
    cJSON_Delete(raiz);
    return json;
}

bool xnuvem_lista_aplicar(const char *json, XTitulos *t) {
    cJSON *raiz = json ? cJSON_Parse(json) : NULL;
    const cJSON *versao = cJSON_GetObjectItemCaseSensitive(raiz, "versao");
    const cJSON *jogos = cJSON_GetObjectItemCaseSensitive(raiz, "jogos");
    int n = cJSON_IsArray(jogos) ? cJSON_GetArraySize(jogos) : 0;
    if (!cJSON_IsNumber(versao) || versao->valuedouble != 1 || n <= 0) {
        cJSON_Delete(raiz);
        return false;
    }
    XTitulo *itens = calloc((size_t)n, sizeof *itens);
    if (!itens) {
        cJSON_Delete(raiz);
        return false;
    }
    int total = 0;
    const cJSON *j;
    cJSON_ArrayForEach(j, jogos) {
        const cJSON *id = cJSON_GetArrayItem(j, 0), *prod = cJSON_GetArrayItem(j, 1), *rec = cJSON_GetArrayItem(j, 2);
        if (!cJSON_IsString(id) || !id->valuestring[0])
            continue;
        XTitulo *x = &itens[total++];
        copiar(x->titulo_id, sizeof x->titulo_id, id->valuestring);
        copiar(x->produto_id, sizeof x->produto_id, cJSON_IsString(prod) ? prod->valuestring : "");
        x->recente = cJSON_IsNumber(rec) ? (int)rec->valuedouble : 0;
        x->tem_direito = true;
    }
    cJSON_Delete(raiz);
    free(t->itens);
    memset(t, 0, sizeof *t);
    t->itens = itens;
    t->total = t->total_servidor = total;
    return total > 0;
}
