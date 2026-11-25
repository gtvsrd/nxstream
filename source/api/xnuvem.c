// xCloud: chamadas de rede da lista de jogos (fase X0)
#include "xnuvem.h"
#include "config.h"
#include "../core/http.h"
#include "../core/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static char g_erro[256];

const char *xnuvem_ultimo_erro(void) {
    return g_erro;
}

static double agora_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

// GET na API da nuvem com o gsToken. O servidor responde 204 enquanto monta a
// lista (o Greenlight repete depois de 750 ms); aqui até 8 vezes.
static bool get_nuvem(const AuthStreaming *s, const char *caminho, const char *etapa, HttpResposta *r) {
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

    bool ok = false;
    for (int tentativa = 1; tentativa <= 8; tentativa++) {
        if (!http_requisicao(HTTP_GET, url, headers, NULL, r)) {
            snprintf(g_erro, sizeof g_erro, "Sem conexao com o Xbox Cloud Gaming (%s).", r->erro);
            break;
        }
        if (r->status == 204) {
            LOG("xnuvem: %s: servidor ainda montando (204), tentativa %d", etapa, tentativa);
            http_resposta_liberar(r);
            usleep(750 * 1000);
            continue;
        }
        if (r->status == 200) {
            ok = true;
        } else {
            LOG("xnuvem: %s respondeu HTTP %ld: %.300s", etapa, r->status, r->corpo ? r->corpo : "(vazio)");
            snprintf(g_erro, sizeof g_erro, "Xbox Cloud Gaming respondeu HTTP %ld (%s).", r->status, etapa);
        }
        break;
    }
    if (!ok && !g_erro[0])
        snprintf(g_erro, sizeof g_erro, "Xbox Cloud Gaming nao entregou a lista (%s).", etapa);
    if (!ok)
        http_resposta_liberar(r); // quem chama só libera no sucesso (liberar de novo é seguro)
    free(url);
    free(autorizacao);
    return ok;
}

static char *ler_arquivo(const char *caminho) {
    FILE *f = fopen(caminho, "rb");
    if (!f)
        return NULL;
    char *dados = NULL;
    if (fseek(f, 0, SEEK_END) == 0) {
        long tam = ftell(f);
        if (tam > 0 && tam < 4 * 1024 * 1024 && fseek(f, 0, SEEK_SET) == 0 && (dados = malloc((size_t)tam + 1))) {
            if (fread(dados, 1, (size_t)tam, f) == (size_t)tam) {
                dados[tam] = '\0';
            } else {
                free(dados);
                dados = NULL;
            }
        }
    }
    fclose(f);
    return dados;
}

// Grava num temporário e troca, para não deixar um cache pela metade
static void salvar_cache(const XTitulos *t) {
    char *json = xnuvem_cache_montar(t, CATALOGO_IDIOMA);
    if (!json)
        return;
    const char *tmp = NX_ARQ_NUVEM_NOMES ".tmp";
    FILE *f = fopen(tmp, "wb");
    bool ok = f && fwrite(json, 1, strlen(json), f) == strlen(json);
    if (f)
        ok = (fclose(f) == 0) && ok;
    if (ok) {
        remove(NX_ARQ_NUVEM_NOMES);
        ok = rename(tmp, NX_ARQ_NUVEM_NOMES) == 0;
    }
    LOG("xnuvem: cache de nomes %s (%zu KB)", ok ? "salvo" : "NAO foi salvo", strlen(json) / 1024);
    free(json);
}

// Nomes no catálogo do Game Pass, em lotes, só para os jogos que o cache do SD
// não tem. Falha aqui não impede a lista. Retorna quantos nomes vieram.
static int buscar_nomes(XTitulos *t) {
    static const char *const headers[] = {
        "Accept: application/json",
        "Content-Type: application/json",
        "ms-cv: 0",
        "calling-app-name: Xbox Cloud Gaming Web",
        "calling-app-version: 21.0.0",
        NULL,
    };
    int faltam = xnuvem_sem_nome(t);
    if (faltam == 0) {
        LOG("xnuvem: catalogo: todos os nomes ja estavam no cache");
        return 0;
    }
    double t0 = agora_s();
    size_t bytes = 0, rede = 0;
    int lotes = 0, falhas = 0, novos = 0;
    // Os índices contam entre os jogos ainda sem nome, que diminuem a cada lote
    // respondido: avança só pelos que continuaram sem nome.
    for (int inicio = 0; inicio < xnuvem_sem_nome(t);) {
        char *corpo = xnuvem_corpo_catalogo(t, inicio, CATALOGO_LOTE);
        if (!corpo)
            break;
        int antes = xnuvem_sem_nome(t);
        HttpResposta r;
        bool ok = http_requisicao(HTTP_POST, CATALOGO_URL_PRODUTOS, headers, corpo, &r);
        free(corpo);
        lotes++;
        int n = ok && r.status == 200 ? xnuvem_parse_catalogo(r.corpo, t) : -1;
        if (n < 0) {
            falhas++;
            LOG("xnuvem: catalogo (lote %d) falhou: HTTP %ld %s %.200s", lotes, r.status, ok ? "" : r.erro,
                r.corpo ? r.corpo : "");
        } else {
            novos += n;
        }
        bytes += r.tamanho;
        rede += r.recebido ? r.recebido : r.tamanho;
        http_resposta_liberar(&r);
        if (!ok)
            break; // sem rede: não insiste nos outros lotes
        int resolvidos = antes - xnuvem_sem_nome(t);
        inicio += CATALOGO_LOTE - resolvidos; // os que o catálogo não conhece ficam para trás
    }
    LOG("xnuvem: catalogo: %d sem nome, %d lote(s), %d falha(s), %d nome(s), %zu KB (%zu KB pela rede) em %.1f s",
        faltam, lotes, falhas, novos, bytes / 1024, rede / 1024, agora_s() - t0);
    return novos;
}

bool xnuvem_listar(const AuthStreaming *s, XTitulos *t) {
    g_erro[0] = '\0';
    double t0 = agora_s();
    HttpResposta r;

    if (!get_nuvem(s, API_CAMINHO_TITULOS, "lista de jogos", &r))
        return false;
    bool ok = xnuvem_parse_titulos(r.corpo, t);
    if (!ok) {
        LOG("xnuvem: lista de jogos em formato inesperado (%zu bytes): %.300s", r.tamanho, r.corpo ? r.corpo : "");
        snprintf(g_erro, sizeof g_erro, "Resposta inesperada do Xbox Cloud Gaming (lista de jogos).");
    } else {
        // Até hoje a lista vem inteira; se um dia vier paginada, o log avisa
        bool paginada = r.corpo && strstr(r.corpo, "\"continuationToken\":\"");
        LOG("xnuvem: /v2/titles: %d jogo(s), %zu KB (%zu KB pela rede), %.1f s%s", t->total, r.tamanho / 1024,
            (r.recebido ? r.recebido : r.tamanho) / 1024, agora_s() - t0, paginada ? " (ATENCAO: lista paginada, so a 1a pagina foi lida)" : "");
    }
    http_resposta_liberar(&r);
    if (!ok)
        return false;

    // Recentes: opcional
    if (get_nuvem(s, API_CAMINHO_RECENTES, "recentes", &r)) {
        int n = xnuvem_parse_recentes(r.corpo, t);
        LOG("xnuvem: recentes: %d", n);
    } else {
        LOG("xnuvem: recentes indisponiveis (%s)", g_erro);
        g_erro[0] = '\0';
    }
    http_resposta_liberar(&r);

    xnuvem_filtrar_ordenar(t); // só os com direito vão ao catálogo
    LOG("xnuvem: com direito: %d de %d", t->total, t->total_servidor);

    char *cache = ler_arquivo(NX_ARQ_NUVEM_NOMES);
    int do_cache = cache ? xnuvem_cache_aplicar(cache, CATALOGO_IDIOMA, t) : -1;
    free(cache);
    LOG("xnuvem: cache de nomes: %s%d", do_cache < 0 ? "vazio ou invalido, " : "", do_cache < 0 ? 0 : do_cache);
    if (buscar_nomes(t) > 0)
        salvar_cache(t);
    xnuvem_filtrar_ordenar(t); // de novo, agora por nome

    LOG("xnuvem: lista pronta em %.1f s: %d jogo(s), %d com nome, %d recente(s)", agora_s() - t0, t->total,
        t->com_nome, t->recentes);
    for (int i = 0; i < t->total && i < 5; i++)
        LOG("xnuvem:   [%d] %s | %s | %s%s", i, t->itens[i].nome[0] ? t->itens[i].nome : "(sem nome)",
            t->itens[i].titulo_id, t->itens[i].produto_id, t->itens[i].recente ? " | recente" : "");
    return true;
}
