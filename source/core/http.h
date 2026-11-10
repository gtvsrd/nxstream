// NXStream - requisições HTTPS (camada fina sobre o libcurl)
#pragma once
#include <stdbool.h>
#include <stddef.h>

typedef enum { HTTP_GET, HTTP_POST, HTTP_DELETE } HttpMetodo;

typedef struct {
    long status;       // código HTTP (0 se a requisição nem chegou ao servidor)
    char *corpo;       // resposta, terminada em '\0' (pode ser NULL)
    size_t tamanho;
    char erro[160];    // descrição do erro de transporte, se houver
} HttpResposta;

bool http_iniciar(void);
void http_encerrar(void);

// headers: lista de "Nome: valor" terminada em NULL (pode ser NULL).
// corpo: usado apenas no POST (pode ser NULL).
// Retorna false só em erro de transporte (sem rede, DNS, TLS, timeout).
// Qualquer resposta do servidor, mesmo 4xx/5xx, retorna true.
bool http_requisicao(HttpMetodo metodo, const char *url, const char *const *headers,
                     const char *corpo, HttpResposta *resposta);

void http_resposta_liberar(HttpResposta *resposta);

// Codifica um texto para uso em formulário/URL. Liberar com free().
char *http_codificar(const char *texto);
