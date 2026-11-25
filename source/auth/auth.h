// NXStream - autenticação Microsoft / Xbox Live / streaming
//
// Fluxo (docs/PROTOCOLO.md):
//   1. código de login (device code) -> usuário entra em microsoft.com/link
//   2. token da conta (access + refresh)
//   3. user token do Xbox Live -> XSTS (gssv) -> gsToken do streaming
//
// Só o refresh token é salvo no SD. O resto é obtido a cada abertura.
#pragma once
#include <stdbool.h>

typedef enum {
    AUTH_OK,
    AUTH_PENDENTE,    // usuário ainda não concluiu o login no celular/PC
    AUTH_LENTO,       // servidor pediu para consultar com menos frequência
    AUTH_RECUSADO,    // usuário recusou o login
    AUTH_EXPIRADO,    // código de login expirou
    AUTH_INVALIDO,    // refresh token não vale mais: precisa logar de novo
    AUTH_ERRO_REDE,   // sem conexão / timeout / TLS
    AUTH_ERRO,        // resposta inesperada do servidor
} AuthStatus;

typedef struct {
    char codigo_usuario[32];      // o que o usuário digita (ex.: ABCD1234)
    char endereco[128];           // ex.: https://www.microsoft.com/link
    char codigo_dispositivo[2048];
    int  expira_em_s;
    int  intervalo_s;
} AuthCodigo;

typedef struct {
    char *access_token;
    char *refresh_token;
} AuthConta;

typedef struct {
    char *gs_token;
    char  base_uri[256];          // servidor regional da API de streaming
    int   duracao_s;
    char  gamertag[64];           // pode ficar vazio
    char  oferta[16];             // xhome, xgpuweb ou xgpuwebf2p
    char  regiao[48];             // nome da região escolhida (ex.: BrazilSouth)
    char  regioes[256];           // todas, para o log: "A*, B, C" (* = padrão)
} AuthStreaming;

// --- Rede -----------------------------------------------------------------
AuthStatus auth_codigo_iniciar(AuthCodigo *codigo);
AuthStatus auth_codigo_verificar(const AuthCodigo *codigo, AuthConta *conta);
AuthStatus auth_renovar(AuthConta *conta);
AuthStatus auth_streaming(const AuthConta *conta, AuthStreaming *streaming);
// xCloud (fase X0): gsToken da oferta xgpuweb (Game Pass Ultimate); se a conta
// não tiver, tenta xgpuwebf2p (só jogos gratuitos). nuvem->oferta diz qual veio.
AuthStatus auth_nuvem(const AuthConta *conta, AuthStreaming *nuvem);
// Token de transferência que o console pede em /connect (estado ReadyToConnect).
// Não altera o refresh token salvo. *token deve ser liberado com free().
AuthStatus auth_token_console(const AuthConta *conta, char **token);

// Mensagem legível do último erro (para mostrar na tela)
const char *auth_ultimo_erro(void);

void auth_conta_liberar(AuthConta *conta);
void auth_streaming_liberar(AuthStreaming *streaming);

// --- Armazenamento (auth_store.c) ------------------------------------------
bool  auth_salvar_refresh(const char *caminho, const char *refresh_token);
char *auth_carregar_refresh(const char *caminho);   // NULL se não houver
bool  auth_apagar(const char *caminho);

// --- Interpretação das respostas (auth_parse.c, sem rede, testável no PC) ---
bool       auth_parse_codigo(const char *json, AuthCodigo *codigo);
AuthStatus auth_parse_token(long status, const char *json, AuthConta *conta);
char      *auth_parse_xbl_token(const char *json, char *gamertag, int tam_gamertag);
bool       auth_parse_streaming(const char *json, AuthStreaming *streaming);
const char *auth_descrever_xerr(long long xerr);
char      *auth_parse_token_console(long status, const char *json);
