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

bool xapi_listar_consoles(const AuthStreaming *streaming, XConsoles *consoles);
const char *xapi_ultimo_erro(void);

// Textos amigáveis para a tela
const char *xapi_estado_amigavel(const char *estado);
const char *xapi_tipo_amigavel(const char *tipo);

// Interpretação da resposta (xapi_parse.c, sem rede, testável no PC)
bool xapi_parse_consoles(const char *json, XConsoles *consoles);
