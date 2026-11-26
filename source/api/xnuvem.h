// NXStream - xCloud (jogos na nuvem), fase X0: lista de jogos
//
// Fluxo: auth_nuvem() dá o gsToken da nuvem; xnuvem_listar() lê os jogos
// liberados (/v2/titles), os recentes (/v2/titles/mru) e os nomes no catálogo
// do Game Pass. Referência: docs/XCLOUD.md
#pragma once
#include <stdbool.h>
#include "../auth/auth.h"

typedef struct {
    char titulo_id[64];    // titleId do xCloud (abre a sessão na fase X1)
    char produto_id[24];   // productId da loja (chave do catálogo)
    char nome[96];         // do catálogo; vazio se não veio
    char editora[64];
    char capa[160];        // URL do pôster (2:3) no catálogo; vazio se não veio
    bool tem_direito;      // hasEntitlement (Game Pass/comprado) ou gratuito na loja
    int  recente;          // posição em "jogados recentemente" (1 = o último); 0 = não
} XTitulo;

typedef struct {
    XTitulo *itens;        // só os jogos com direito, ordenados: recentes, depois por nome
    int total;
    // Diagnóstico (log e tela)
    int total_servidor;    // quantos /v2/titles trouxe, com e sem direito
    int com_nome;
    int recentes;
} XTitulos;

// Lista completa (várias requisições). false = falhou (xnuvem_ultimo_erro()).
bool xnuvem_listar(const AuthStreaming *nuvem, XTitulos *titulos);
void xnuvem_liberar(XTitulos *titulos);
const char *xnuvem_ultimo_erro(void);

// --- Interpretação das respostas (xnuvem_parse.c, sem rede, testável no PC) ---
// /v2/titles: substitui o conteúdo de *t (todos os jogos, inclusive sem direito).
bool  xnuvem_parse_titulos(const char *json, XTitulos *t);
// /v2/titles/mru: marca XTitulo.recente. Retorna quantos foram marcados (-1 = JSON inválido).
int   xnuvem_parse_recentes(const char *json, XTitulos *t);
// Jogos que ainda não têm nome (os que vão ao catálogo)
int   xnuvem_sem_nome(const XTitulos *t);
// Corpo do POST do catálogo com os productIds dos jogos SEM nome, do inicio-ésimo
// ao (inicio+n)-ésimo deles. Liberar com free().
char *xnuvem_corpo_catalogo(const XTitulos *t, int inicio, int n);
// Resposta do catálogo: preenche nome e editora. Retorna quantos receberam nome (-1 = inválido).
int   xnuvem_parse_catalogo(const char *json, XTitulos *t);
// Busca (v0.13.0): `busca` aparece em `nome`, sem diferenciar maiúsculas nem
// acentos ("pokemon" acha "Pokémon"). Busca vazia combina com tudo.
bool  xnuvem_combina(const char *nome, const char *busca);
// Cache dos nomes no SD (v0.11.1; v2 desde a v0.13.0, com a capa):
// {"versao":2,"idioma":..,"produtos":{"9N..":["Nome","Editora","URL da capa"]}}.
// montar: liberar com free(). aplicar: preenche os nomes e devolve quantos (-1 = inválido
// ou de outro idioma).
char *xnuvem_cache_montar(const XTitulos *t, const char *idioma);
int   xnuvem_cache_aplicar(const char *json, const char *idioma, XTitulos *t);
// Mantém só os jogos com direito e ordena (recentes primeiro, depois por nome).
void  xnuvem_filtrar_ordenar(XTitulos *t);
