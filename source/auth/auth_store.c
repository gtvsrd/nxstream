// Armazenamento do refresh token no SD (JSON simples). Testável no PC.
#include "auth.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool auth_salvar_refresh(const char *caminho, const char *refresh_token) {
    cJSON *raiz = cJSON_CreateObject();
    cJSON_AddNumberToObject(raiz, "versao", 1);
    cJSON_AddStringToObject(raiz, "refresh_token", refresh_token);
    char *json = cJSON_PrintUnformatted(raiz);
    cJSON_Delete(raiz);
    if (!json)
        return false;

    // Escreve num arquivo temporário e troca no final,
    // para nunca deixar um token pela metade se o app fechar no meio.
    char temporario[512];
    snprintf(temporario, sizeof temporario, "%s.tmp", caminho);

    bool ok = false;
    FILE *f = fopen(temporario, "wb");
    if (f) {
        size_t n = strlen(json);
        ok = fwrite(json, 1, n, f) == n;
        ok = (fclose(f) == 0) && ok;
    }
    free(json);

    if (ok) {
        remove(caminho);
        ok = rename(temporario, caminho) == 0;
    }
    if (!ok)
        remove(temporario);
    return ok;
}

char *auth_carregar_refresh(const char *caminho) {
    FILE *f = fopen(caminho, "rb");
    if (!f)
        return NULL;

    fseek(f, 0, SEEK_END);
    long tam = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (tam <= 0 || tam > 64 * 1024) {
        fclose(f);
        return NULL;
    }

    char *dados = malloc((size_t)tam + 1);
    size_t lido = dados ? fread(dados, 1, (size_t)tam, f) : 0;
    fclose(f);
    if (!dados)
        return NULL;
    dados[lido] = '\0';

    char *token = NULL;
    cJSON *raiz = cJSON_Parse(dados);
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(raiz, "refresh_token");
    if (cJSON_IsString(item) && item->valuestring[0])
        token = strdup(item->valuestring);
    cJSON_Delete(raiz);
    free(dados);
    return token;
}

bool auth_apagar(const char *caminho) {
    return remove(caminho) == 0;
}
