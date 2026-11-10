// NXStream - API de streaming do console (xhome)
#pragma once
#include <stdbool.h>
#include "../auth/auth.h"

#define XAPI_MAX_CONSOLES 8

typedef struct {
    char nome[64];        // nome dado ao console (deviceName)
    char server_id[64];   // identificador usado para abrir a sessão
    char estado[32];      // powerState: On, ConnectedStandby, Off...
    char tipo[32];        // consoleType: XboxSeriesX, XboxOne...
} XConsole;

typedef struct {
    XConsole itens[XAPI_MAX_CONSOLES];
    int total;
} XConsoles;

// Sessão de streaming aberta no servidor
typedef struct {
    char caminho[256];    // sessionPath, ex.: v5/sessions/home/ABC123
    char id[256];         // última parte do caminho (para o log)
} XSessao;

typedef enum {
    XSESSAO_PREPARANDO,   // Provisioning
    XSESSAO_AGUARDA_LOGIN,// ReadyToConnect: enviar token do console em /connect
    XSESSAO_PRONTA,       // Provisioned
    XSESSAO_NA_FILA,      // WaitingForResources
    XSESSAO_FALHOU,       // Failed
    XSESSAO_DESCONHECIDO, // estado que ainda não conhecemos
    XSESSAO_ERRO,         // a consulta em si falhou (rede/HTTP)
} XSessaoEstado;

// --- Consoles ---------------------------------------------------------------
bool xapi_listar_consoles(const AuthStreaming *streaming, XConsoles *consoles);

// --- Sessão -----------------------------------------------------------------
bool xapi_sessao_iniciar(const AuthStreaming *s, const char *server_id, XSessao *sessao);
// detalhe recebe o estado bruto ou a mensagem de erro do servidor
XSessaoEstado xapi_sessao_estado(const AuthStreaming *s, const XSessao *sessao,
                                 char *detalhe, int tam_detalhe);
bool xapi_sessao_conectar(const AuthStreaming *s, const XSessao *sessao, const char *token_console);
// Retorna o código HTTP (0 = sem rede)
long xapi_sessao_keepalive(const AuthStreaming *s, const XSessao *sessao);
bool xapi_sessao_encerrar(const AuthStreaming *s, const XSessao *sessao);

const char *xapi_ultimo_erro(void);

// Textos amigáveis para a tela
const char *xapi_estado_amigavel(const char *estado);
const char *xapi_tipo_amigavel(const char *tipo);
const char *xapi_sessao_estado_texto(XSessaoEstado estado);

// --- Interpretação das respostas (xapi_parse.c, sem rede, testável no PC) ---
bool xapi_parse_consoles(const char *json, XConsoles *consoles);
bool xapi_parse_sessao(const char *json, XSessao *sessao);
XSessaoEstado xapi_parse_estado(const char *json, char *detalhe, int tam_detalhe);
char *xapi_corpo_play(const char *server_id);   // JSON do /play; liberar com free()
