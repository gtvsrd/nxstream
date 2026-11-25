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
    cJSON_AddNumberToObject(raiz, "versao", 1);
    cJSON_AddStringToObject(raiz, "idioma", idioma);
    cJSON *produtos = cJSON_AddObjectToObject(raiz, "produtos");
    for (int i = 0; i < t->total; i++) {
        const XTitulo *x = &t->itens[i];
        if (!x->nome[0] || !x->produto_id[0] || cJSON_GetObjectItemCaseSensitive(produtos, x->produto_id))
            continue;
        cJSON *par = cJSON_CreateArray();
        cJSON_AddItemToArray(par, cJSON_CreateString(x->nome));
        cJSON_AddItemToArray(par, cJSON_CreateString(x->editora));
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
    if (!id || strcmp(id, idioma) || !cJSON_IsObject(produtos)) {
        cJSON_Delete(raiz);
        return -1;
    }
    int aplicados = 0;
    for (int i = 0; i < t->total; i++) {
        XTitulo *x = &t->itens[i];
        if (!x->produto_id[0])
            continue;
        const cJSON *par = cJSON_GetObjectItemCaseSensitive(produtos, x->produto_id);
        const cJSON *nome = cJSON_GetArrayItem(par, 0), *editora = cJSON_GetArrayItem(par, 1);
        if (!cJSON_IsString(nome) || !nome->valuestring[0])
            continue;
        copiar(x->nome, sizeof x->nome, nome->valuestring);
        copiar(x->editora, sizeof x->editora, cJSON_IsString(editora) ? editora->valuestring : "");
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
