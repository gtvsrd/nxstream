// NXStream - login, lista de consoles, conexão com o console e vídeo na tela
//
// Interface gráfica simples (ui/tela, v0.10.0) inspirada no app do Xbox; o
// console de texto do libnx fica para o diagnóstico. Com o vídeo na tela, o
// texto (estatísticas, menu) vai para uma camada por cima dele. Fluxo:
//   INICIO -> (sem token salvo) LOGIN -> STREAMING -> LISTA
//   INICIO -> (com token salvo) STREAMING -> LISTA
//   LISTA -> (A no console) SESSAO -> LISTA
//   LISTA -> (R) NUVEM -> LISTA        (xCloud, fase X0: só a lista de jogos)
// Nenhum erro fecha o app: sempre há "tentar de novo" ou "sair da conta".

#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <sys/stat.h>
#include <switch.h>
#include <rtc/rtc.h> // estados da conexão (RTC_CONNECTED...) para a reconexão

#include "api/config.h"
#include "api/xapi.h"
#include "api/xnuvem.h"
#include "auth/auth.h"
#include "core/http.h"
#include "core/log.h"
#include "core/terminar.h"
#include "net/conexao.h"
#include "media/decodificador.h"
#include "media/video_tela.h"
#include "media/audio.h"
#include "input/controle.h"
#include "ui/sobreposicao.h"
#include "ui/tela.h"
#include "ui/capas.h"
#include "cJSON.h"
#include "net/rtc_comum.h"
#include "net/rtc_diag.h"

#define COR_TITULO  "\x1b[36;1m"
#define COR_DESTAQ  "\x1b[33;1m"
#define COR_OK      "\x1b[32;1m"
#define COR_ERRO    "\x1b[31;1m"
#define COR_FRACA   "\x1b[37m"
#define COR_FIM     "\x1b[0m"

typedef enum { PASSO_INICIO, PASSO_LOGIN, PASSO_STREAMING, PASSO_LISTA, PASSO_SESSAO, PASSO_DIAG, PASSO_SAIR,
               PASSO_RECONECTAR, PASSO_NUVEM } Passo; // PASSO_RECONECTAR: só entre passo_conectar e passo_sessao

static PadState g_controle;
static u64 g_tick_anterior;
static double g_ultimo_congelamento = 0; // segundos suspenso no último sleep/HOME (0 = nada novo)
static bool g_retorno_pendente = false;   // sistema avisou que o app voltou a rodar
static bool g_em_foco = true;             // último estado de foco conhecido
static XConsole g_console;               // console escolhido na lista
// xCloud (fase X1): sessão de um jogo na nuvem. g_console vira só o rótulo das
// telas (nome do jogo, tipo "Xbox Cloud Gaming"); o resto do fluxo é o mesmo.
static bool g_na_nuvem = false;
static XTitulo g_jogo;
static bool g_auto_conexao_feita = false; // já passou pela lista uma vez (conexão automática só na 1ª)
static bool g_reconectando = false;      // fase 4: falhas na conexão viram nova tentativa, não tela de erro
static bool g_chegou_a_conectar = false; // esta tentativa chegou a conectar (zera a contagem de tentativas)
static bool g_forcar_renovacao = false;  // o servidor respondeu 401: renovar o gsToken antes da próxima chamada
static u64 g_tick_queda = 0;            // quando a conexão caiu (para medir a reconexão inteira)
static char g_gamertag[64] = "";        // nome da conta, para o cabeçalho e para não pedir de novo
static AuthConta *g_conta = NULL;        // conta, para renovar o acesso durante o streaming
#define LIMITE_SUSPENSAO_S 1.5               // quadro parado por mais que isso = suspensão

// ---------------------------------------------------------------------------
// Sistema: eventos do applet e detector de congelamento
// ---------------------------------------------------------------------------
static void ao_evento_do_sistema(AppletHookType tipo, void *param) {
    (void)param;
    switch (tipo) {
        case AppletHookType_OnResume:
            LOG("sistema: app retomado (sleep)");
            g_retorno_pendente = true;
            break;
        case AppletHookType_OnFocusState: {
            bool em_foco = appletGetFocusState() == AppletFocusState_InFocus;
            LOG("sistema: foco = %s", em_foco ? "primeiro plano" : "segundo plano");
            if (em_foco && !g_em_foco)
                g_retorno_pendente = true; // voltou do HOME (ou do sleep)
            g_em_foco = em_foco;
            break;
        }
        case AppletHookType_OnOperationMode:
            LOG("sistema: modo = %s", appletGetOperationMode() == AppletOperationMode_Console
                                          ? "dock" : "portatil");
            break;
        default:
            break;
    }
}

// Ao fim de cada chamada HTTP: o tempo esperando a rede não é suspensão
static void ao_terminar_http(void) {
    g_tick_anterior = armGetSystemTick();
}

static double segundos_desde(u64 tick) {
    return armTicksToNs(armGetSystemTick() - tick) / 1e9;
}

// ---------------------------------------------------------------------------
// Texto: console do libnx ou camada por cima do vídeo
// ---------------------------------------------------------------------------
static bool g_capturando = false; // true: o texto vai para a camada sobre o vídeo

// Como printf, mas respeita para onde o texto está indo agora
static void tp(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void tp(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (g_capturando)
        sobre_printf("%s", buf);
    else
        fputs(buf, stdout);
}

// A janela do app é usada por um de três de cada vez: o console de texto
// (diagnóstico), a interface gráfica (ui/tela) ou a GPU com o vídeo.
static bool g_console_ativo = false;

static void soltar_janela(void) {
    if (video_tela_ativa())
        video_tela_encerrar(); // solta os quadros do decodificador
    if (ui_ativa())
        ui_encerrar();
    if (g_console_ativo) {
        consoleExit(NULL);
        g_console_ativo = false;
    }
}

// Console de texto (diagnóstico e telas de emergência)
static void modo_texto(void) {
    if (g_console_ativo)
        return;
    soltar_janela();
    consoleInit(NULL);
    g_console_ativo = true;
}

// Interface gráfica. Também tira o vídeo da frente: precisa vir antes de fechar
// a conexão, porque os quadros na tela são do decodificador.
static void modo_ui(void) {
    if (ui_ativa())
        return;
    soltar_janela();
    if (!ui_iniciar()) {
        LOG("main: interface grafica indisponivel; usando o console de texto");
        modo_texto();
    }
}

// Mostra a tela atual já (antes de uma operação que trava por alguns segundos)
static void mostrar_agora(void) {
    if (ui_ativa())
        ui_desenhar();
    else if (g_console_ativo)
        consoleUpdate(NULL);
}

// Um quadro: processa o sistema e o controle. Retorna os botões apertados.
// *fechar fica true quando o sistema pede para encerrar o app.
static u64 quadro(bool *fechar) {
    // Com o vídeo, quem mostra a tela é video_tela_desenhar(). A interface é
    // redesenhada a cada quadro (animação) e espera o vsync; fora de foco (HOME)
    // não desenha, só ritma o laço.
    if (ui_ativa()) {
        if (g_em_foco)
            ui_desenhar();
        else
            svcSleepThread(16000000ULL);
    } else if (g_console_ativo) {
        consoleUpdate(NULL);
    }
    if (!appletMainLoop()) {
        *fechar = true;
        return 0;
    }
    // Suspensão (sleep/HOME) é detectada por dois sinais:
    //  1. aviso do sistema (hook), quando o Switch envia;
    //  2. tempo parado entre quadros. O tempo das chamadas HTTP não conta:
    //     ao fim de cada requisição, ao_terminar_http() reinicia a contagem.
    double parado = segundos_desde(g_tick_anterior);
    g_tick_anterior = armGetSystemTick();
    bool por_aviso = g_retorno_pendente;
    bool por_tempo = parado > LIMITE_SUSPENSAO_S;
    g_retorno_pendente = false;
    if (por_aviso || por_tempo) {
        g_ultimo_congelamento = parado > 0.1 ? parado : 0.1;
        if (parado > LIMITE_SUSPENSAO_S)
            http_descartar_conexoes(); // as conexões HTTPS abertas morreram no repouso
        LOG("sistema: app voltou depois de %.1f s suspenso (detectado por %s)", parado,
            por_aviso && por_tempo ? "aviso e tempo" : por_aviso ? "aviso" : "tempo");
    }

    padUpdate(&g_controle);
    return padGetButtonsDown(&g_controle);
}

// Espera um dos botões da máscara. Retorna o botão, ou 0 se o app vai fechar.
static u64 esperar_botao(u64 mascara) {
    bool fechar = false;
    while (!fechar) {
        u64 b = quadro(&fechar) & mascara;
        if (b)
            return b;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Telas
// ---------------------------------------------------------------------------
static void cabecalho(void) {
    if (!g_capturando) {
        modo_texto(); // qualquer tela de texto tira o vídeo da frente
        consoleClear();
    }
    tp(COR_TITULO "NXStream" COR_FIM " v%s - Xbox Remote Play\n", APP_VERSION);
    tp("================================================================\n\n");
}

// Tela de espera (símbolo pulsando) mostrada na hora: vem antes de chamadas que travam
static void mostrar_aguarde(const char *mensagem) {
    modo_ui();
    if (ui_ativa()) {
        ui_status(mensagem, NULL, NULL, NULL);
    } else {
        cabecalho();
        printf("%s\n", mensagem);
    }
    mostrar_agora();
}

// Tela de mensagem (erro, aviso, pergunta); no console de texto se a interface faltar
static void mostrar_mensagem(UiTipo tipo, const char *titulo, const char *detalhe, const char *rodape) {
    modo_ui();
    if (ui_ativa()) {
        ui_mensagem(tipo, titulo, detalhe, rodape);
        return;
    }
    cabecalho();
    printf("%s%s" COR_FIM "\n\n", tipo == UI_ERRO ? COR_ERRO : COR_DESTAQ, titulo);
    if (detalhe && detalhe[0])
        printf("%s\n\n", detalhe);
    // "A=Tentar de novo|+=Fechar" -> "A = Tentar de novo   + = Fechar"
    for (const char *r = rodape; r && *r; r++) {
        if (*r == '=') printf(" = ");
        else if (*r == '|') printf("   ");
        else putchar(*r);
    }
    printf("\n");
}

// Tela de erro com opções. Retorna o botão escolhido (0 = app fechando).
static u64 mostrar_erro(const char *titulo, const char *detalhe, bool permite_sair_conta) {
    mostrar_mensagem(UI_ERRO, titulo, detalhe,
                     permite_sair_conta ? "A=Tentar de novo|X=Sair da conta|+=Fechar" : "A=Tentar de novo|+=Fechar");
    u64 mascara = HidNpadButton_A | HidNpadButton_Plus;
    if (permite_sair_conta)
        mascara |= HidNpadButton_X;
    return esperar_botao(mascara);
}

// Remove "https://" e "www." para o endereço ficar curto na tela
static const char *endereco_curto(const char *url) {
    if (!strncmp(url, "https://", 8)) url += 8;
    if (!strncmp(url, "www.", 4)) url += 4;
    return url;
}

// ---------------------------------------------------------------------------
// Chamadas de rede sem travar a animação (v0.10.6). Antes, cada consulta HTTP
// (login, lista, estado da sessão a cada 1 s, SDP, candidatos) parava o laço
// principal por 0,2 a 3 s, e a tela de inicialização dava trancos. Agora a
// chamada roda numa thread e o laço segue desenhando a 60 quadros/s até ela
// terminar. Só uma chamada por vez; o resultado volta em `r`.
// ---------------------------------------------------------------------------
typedef enum {
    CH_RENOVAR, CH_STREAMING, CH_LISTAR, CH_SESSAO_INICIAR, CH_SESSAO_ESTADO, CH_TOKEN_CONSOLE,
    CH_SESSAO_CONECTAR, CH_ENVIAR_SDP, CH_RECEBER_SDP, CH_ENVIAR_ICE, CH_RECEBER_ICE, CH_OBTER_NOVO,
    CH_NUVEM_ACESSO, CH_NUVEM_LISTAR, CH_TEMPO_ESPERA,
} TipoChamada;

static AuthStatus obter_streaming_novo(AuthConta *conta, AuthStreaming *novo);
static AuthStatus obter_nuvem_novo(AuthConta *conta, AuthStreaming *novo);

typedef struct {
    TipoChamada tipo;
    AuthConta *conta;
    AuthStreaming *streaming;
    XSessao *sessao;
    const char *texto;   // server_id, oferta, token, ufrag ou titleId
    const char *texto2;  // titleId ao abrir sessão na nuvem (NULL = console)
    void *saida;         // XConsoles*, char** (token/resposta), XCandidatos*
    const void *entrada; // XCandidatos* locais
    char *detalhe;
    size_t tam;
    long r;
    volatile bool pronto;
} Chamada;

static void chamada_executar(Chamada *c) {
    switch (c->tipo) {
        case CH_RENOVAR:         c->r = auth_renovar(c->conta); break;
        case CH_STREAMING:       c->r = auth_streaming(c->conta, c->streaming); break;
        case CH_LISTAR:          c->r = xapi_listar_consoles(c->streaming, (XConsoles *)c->saida); break;
        case CH_SESSAO_INICIAR:  c->r = xapi_sessao_iniciar(c->streaming, c->texto, c->texto2, c->sessao); break;
        case CH_SESSAO_ESTADO:   c->r = xapi_sessao_estado(c->streaming, c->sessao, c->detalhe, (int)c->tam); break;
        case CH_TOKEN_CONSOLE:   c->r = auth_token_console(c->conta, (char **)c->saida); break;
        case CH_SESSAO_CONECTAR: c->r = xapi_sessao_conectar(c->streaming, c->sessao, c->texto); break;
        case CH_ENVIAR_SDP:      c->r = xapi_sessao_enviar_sdp(c->streaming, c->sessao, c->texto); break;
        case CH_RECEBER_SDP:     c->r = xapi_sessao_receber_sdp(c->streaming, c->sessao, (char **)c->saida); break;
        case CH_ENVIAR_ICE:
            c->r = xapi_sessao_enviar_ice(c->streaming, c->sessao, (const XCandidatos *)c->entrada, c->texto);
            break;
        case CH_RECEBER_ICE:     c->r = xapi_sessao_receber_ice(c->streaming, c->sessao, (XCandidatos *)c->saida); break;
        case CH_OBTER_NOVO:      c->r = obter_streaming_novo(c->conta, c->streaming); break;
        case CH_NUVEM_ACESSO:    c->r = obter_nuvem_novo(c->conta, c->streaming); break;
        case CH_NUVEM_LISTAR:    c->r = xnuvem_listar(c->streaming, (XTitulos *)c->saida); break;
        case CH_TEMPO_ESPERA:    c->r = xapi_tempo_espera(c->streaming, c->texto); break;
    }
}

static void *thread_chamada(void *arg) {
    Chamada *c = arg;
    chamada_executar(c);
    __atomic_store_n(&c->pronto, true, __ATOMIC_RELEASE);
    return NULL;
}

// Executa a chamada mantendo a tela animada. Sem interface gráfica (vídeo na
// tela ou console de texto) ou sem thread, executa direto, como antes.
static long chamar(Chamada c) {
    c.pronto = false;
    if (!ui_ativa()) {
        chamada_executar(&c);
        return c.r;
    }
    pthread_t t;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 512 * 1024); // curl + TLS + JSON
    bool criada = pthread_create(&t, &at, thread_chamada, &c) == 0;
    pthread_attr_destroy(&at);
    if (!criada) {
        chamada_executar(&c);
        return c.r;
    }
    bool fechar = false;
    while (!__atomic_load_n(&c.pronto, __ATOMIC_ACQUIRE))
        quadro(&fechar); // botões apertados durante a espera ficam para depois
    pthread_join(t, NULL);
    return c.r;
}

// ---------------------------------------------------------------------------
// Passo: login por código
// ---------------------------------------------------------------------------

// Abre a página de login no navegador do Switch, já com o código preenchido
// (parâmetro otc). Trava até o usuário fechar o navegador.
static bool abrir_pagina_login(const AuthCodigo *codigo) {
    char url[512];
    snprintf(url, sizeof url, "%s?otc=%s", codigo->endereco, codigo->codigo_usuario);
    WebCommonConfig cfg;
    WebCommonReply resposta;
    Result rc = webPageCreate(&cfg, url);
    if (R_SUCCEEDED(rc))
        rc = webConfigSetWhitelist(&cfg, "^http");
    if (R_SUCCEEDED(rc))
        rc = webConfigShow(&cfg, &resposta);
    LOG("main: navegador para o login %s (0x%x)", R_SUCCEEDED(rc) ? "fechado" : "falhou", rc);
    g_tick_anterior = armGetSystemTick(); // o tempo no navegador não é suspensão
    return R_SUCCEEDED(rc);
}

static Passo passo_login(AuthConta *conta, const char *aviso) {
    for (;;) {
        mostrar_aguarde("Gerando o código de login...");
        AuthCodigo codigo;
        AuthStatus st = auth_codigo_iniciar(&codigo);
        if (st != AUTH_OK) {
            u64 b = mostrar_erro("Não foi possível gerar o código de login.", auth_ultimo_erro(), false);
            if (b != HidNpadButton_A)
                return PASSO_SAIR;
            continue;
        }

        u64 inicio = armGetSystemTick();
        int intervalo = codigo.intervalo_s;
        double proxima = intervalo;
        int ultimo_segundo = -1;
        bool sem_rede = false, navegador_falhou = false;
        const char *motivo_fim = NULL;
        bool fechar = false;
        modo_ui();

        while (!fechar && !motivo_fim) {
            double passou = segundos_desde(inicio);
            int restante = codigo.expira_em_s - (int)passou;

            if ((int)passou != ultimo_segundo) {
                ultimo_segundo = (int)passou;
                char status[128];
                if (sem_rede)
                    snprintf(status, sizeof status, "Sem conexão no momento, tentando de novo...");
                else if (navegador_falhou)
                    snprintf(status, sizeof status, "Não foi possível abrir o navegador; use o celular");
                else
                    snprintf(status, sizeof status, "Aguardando login · o código expira em %d:%02d",
                             restante > 0 ? restante / 60 : 0, restante > 0 ? restante % 60 : 0);
                if (ui_ativa()) {
                    ui_codigo(aviso, endereco_curto(codigo.endereco), codigo.codigo_usuario, status,
                              "A=Abrir a página no Switch|+=Fechar");
                } else {
                    cabecalho();
                    if (aviso) printf(COR_DESTAQ "%s" COR_FIM "\n\n", aviso);
                    printf("No celular ou PC, acesse " COR_DESTAQ "%s" COR_FIM "\n\n",
                           endereco_curto(codigo.endereco));
                    printf("e digite o codigo " COR_DESTAQ "%s" COR_FIM "\n\n%s\n\n+ = fechar o app\n",
                           codigo.codigo_usuario, status);
                }
            }

            if (restante <= 0) {
                motivo_fim = "O codigo expirou.";
                break;
            }

            if (passou >= proxima) {
                st = auth_codigo_verificar(&codigo, conta);
                sem_rede = (st == AUTH_ERRO_REDE);
                switch (st) {
                    case AUTH_OK:
                        if (!auth_salvar_refresh(NX_ARQ_TOKEN, conta->refresh_token))
                            LOG("main: nao foi possivel salvar o token em %s", NX_ARQ_TOKEN);
                        return PASSO_STREAMING;
                    case AUTH_LENTO:
                        intervalo += 5;
                        break;
                    case AUTH_PENDENTE:
                    case AUTH_ERRO_REDE:
                        break;
                    default:
                        motivo_fim = auth_ultimo_erro();
                        break;
                }
                proxima = segundos_desde(inicio) + intervalo;
            }

            u64 b = quadro(&fechar);
            if (b & HidNpadButton_Plus)
                return PASSO_SAIR;
            if (b & HidNpadButton_A) {
                navegador_falhou = !abrir_pagina_login(&codigo);
                proxima = 0;          // ao voltar, confere o login na hora
                ultimo_segundo = -1;
            }
        }
        if (fechar)
            return PASSO_SAIR;

        mostrar_mensagem(UI_AVISO, motivo_fim, NULL, "A=Gerar outro código|+=Fechar");
        if (esperar_botao(HidNpadButton_A | HidNpadButton_Plus) != HidNpadButton_A)
            return PASSO_SAIR;
        aviso = NULL;
    }
}

// ---------------------------------------------------------------------------
// Passo: tokens do Xbox Live e do streaming
// ---------------------------------------------------------------------------
static Passo passo_streaming(AuthConta *conta, AuthStreaming *streaming, bool conta_recem_logada,
                             const char **aviso_login) {
    // O token da conta dura ~1 h; renovar sempre garante que está válido.
    // A Microsoft troca o refresh token a cada renovação, então salva de novo.
    if (!conta_recem_logada) {
        mostrar_aguarde("Entrando na sua conta...");
        AuthStatus st = (AuthStatus)chamar((Chamada){.tipo = CH_RENOVAR, .conta = conta});
        if (st == AUTH_INVALIDO) {
            auth_apagar(NX_ARQ_TOKEN);
            auth_conta_liberar(conta);
            *aviso_login = "Sua sessão expirou. Faça login de novo.";
            return PASSO_LOGIN;
        }
        if (st != AUTH_OK) {
            u64 b = mostrar_erro("Não foi possível entrar na conta.", auth_ultimo_erro(), true);
            if (b == HidNpadButton_X) {
                auth_apagar(NX_ARQ_TOKEN);
                auth_conta_liberar(conta);
                return PASSO_LOGIN;
            }
            return b == HidNpadButton_A ? PASSO_STREAMING : PASSO_SAIR;
        }
        if (!auth_salvar_refresh(NX_ARQ_TOKEN, conta->refresh_token))
            LOG("main: nao foi possivel salvar o token renovado");
    }

    mostrar_aguarde("Conectando ao Xbox Live...");
    AuthStatus st = (AuthStatus)chamar((Chamada){.tipo = CH_STREAMING, .conta = conta, .streaming = streaming});
    if (st != AUTH_OK) {
        u64 b = mostrar_erro("Não foi possível acessar o Xbox Live.", auth_ultimo_erro(), true);
        if (b == HidNpadButton_X) {
            auth_apagar(NX_ARQ_TOKEN);
            auth_conta_liberar(conta);
            return PASSO_LOGIN;
        }
        return b == HidNpadButton_A ? PASSO_STREAMING : PASSO_SAIR;
    }
    return PASSO_LISTA;
}

// ---------------------------------------------------------------------------
// Passo: lista de consoles
// ---------------------------------------------------------------------------
static bool confirmar_sair_da_conta(void) {
    mostrar_mensagem(UI_AVISO, "Sair da conta?", "Na próxima vez será preciso fazer login de novo.",
                     "A=Sair da conta|B=Cancelar");
    return esperar_botao(HidNpadButton_A | HidNpadButton_B) == HidNpadButton_A;
}

// ---------------------------------------------------------------------------
// Abertura rápida (v0.10.5). Um arquivo pequeno no SD guarda a gamertag e, quando
// a conta tem um só console, os dados dele. Na abertura seguinte o app pula a
// consulta da lista (~1 s) e a contagem de 2 s e já abre a sessão; a gamertag
// também não precisa ser pedida de novo. Não guarda nenhum token.
// ---------------------------------------------------------------------------
static XConsole g_console_salvo; // server_id vazio = nenhum
// Console que sabemos estar DESLIGADO (economia de energia), não em repouso. A
// Microsoft continua informando o último estado que ele avisou (ConnectedStandby):
// só sabemos ao tentar conectar ("WaitingForServerToRegister") ou quando ele
// avisa que foi desligado no meio da sessão (KickForServerShutdown). Desde a
// v0.12.3 fica guardado em conta.json; some quando uma sessão com ele dá certo.
static char g_console_desligado[64];
// Última aba usada (v0.13.0): o app abre nela (decisão do roadmap do xCloud)
static bool g_aba_nuvem = false;
static bool g_console_salvo_unico = false;

static void conta_salvar(void) {
    cJSON *raiz = cJSON_CreateObject();
    cJSON_AddStringToObject(raiz, "gamertag", g_gamertag);
    if (g_console_salvo.server_id[0]) {
        cJSON *c = cJSON_AddObjectToObject(raiz, "console");
        cJSON_AddStringToObject(c, "nome", g_console_salvo.nome);
        cJSON_AddStringToObject(c, "id", g_console_salvo.server_id);
        cJSON_AddStringToObject(c, "tipo", g_console_salvo.tipo);
        cJSON_AddBoolToObject(c, "unico", g_console_salvo_unico);
    }
    if (g_console_desligado[0])
        cJSON_AddStringToObject(raiz, "desligado", g_console_desligado);
    cJSON_AddStringToObject(raiz, "aba", g_aba_nuvem ? "nuvem" : "consoles");
    char *txt = cJSON_PrintUnformatted(raiz);
    cJSON_Delete(raiz);
    FILE *f = txt ? fopen(NX_ARQ_CONTA, "w") : NULL;
    if (f) {
        fputs(txt, f);
        fclose(f);
    }
    free(txt);
}

static void conta_ler(void) {
    char buf[1024];
    FILE *f = fopen(NX_ARQ_CONTA, "r");
    if (!f)
        return;
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = '\0';
    cJSON *raiz = cJSON_Parse(buf);
    if (!raiz)
        return;
    const cJSON *g = cJSON_GetObjectItemCaseSensitive(raiz, "gamertag");
    if (cJSON_IsString(g))
        snprintf(g_gamertag, sizeof g_gamertag, "%s", g->valuestring);
    const cJSON *c = cJSON_GetObjectItemCaseSensitive(raiz, "console");
    const cJSON *nome = cJSON_GetObjectItemCaseSensitive(c, "nome");
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(c, "id");
    const cJSON *tipo = cJSON_GetObjectItemCaseSensitive(c, "tipo");
    if (cJSON_IsString(nome) && cJSON_IsString(id) && id->valuestring[0]) {
        memset(&g_console_salvo, 0, sizeof g_console_salvo);
        snprintf(g_console_salvo.nome, sizeof g_console_salvo.nome, "%s", nome->valuestring);
        snprintf(g_console_salvo.server_id, sizeof g_console_salvo.server_id, "%s", id->valuestring);
        if (cJSON_IsString(tipo))
            snprintf(g_console_salvo.tipo, sizeof g_console_salvo.tipo, "%s", tipo->valuestring);
        g_console_salvo_unico = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(c, "unico"));
    }
    const cJSON *aba = cJSON_GetObjectItemCaseSensitive(raiz, "aba");
    g_aba_nuvem = cJSON_IsString(aba) && !strcmp(aba->valuestring, "nuvem");
    const cJSON *desl = cJSON_GetObjectItemCaseSensitive(raiz, "desligado");
    if (cJSON_IsString(desl))
        snprintf(g_console_desligado, sizeof g_console_desligado, "%s", desl->valuestring);
    cJSON_Delete(raiz);
    LOG("main: conta salva lida (gamertag %s, console %s%s%s, aba %s)", g_gamertag[0] ? "sim" : "nao",
        g_console_salvo.server_id[0] ? g_console_salvo.nome : "nenhum",
        g_console_salvo_unico ? ", unico" : "", g_console_desligado[0] ? ", desligado da ultima vez" : "",
        g_aba_nuvem ? "nuvem" : "consoles");
}

static void conta_esquecer(void) {
    remove(NX_ARQ_CONTA);
    g_gamertag[0] = '\0';
    memset(&g_console_salvo, 0, sizeof g_console_salvo);
    g_console_salvo_unico = false;
    g_console_desligado[0] = '\0';
}

static void aba_definir(bool nuvem) {
    if (g_aba_nuvem == nuvem)
        return;
    g_aba_nuvem = nuvem;
    conta_salvar();
}

// Marca (id) ou desmarca (NULL) o console como desligado, gravando se mudou
static void console_marcar_desligado(const char *id) {
    const char *novo = id ? id : "";
    if (!strcmp(novo, g_console_desligado))
        return;
    snprintf(g_console_desligado, sizeof g_console_desligado, "%s", novo);
    LOG("main: console %s", id ? "marcado como desligado" : "nao esta mais desligado");
    conta_salvar();
}

// Guarda o que a lista mostrou (só regrava o arquivo se algo mudou)
static void conta_lembrar_consoles(const XConsoles *consoles) {
    XConsole novo;
    memset(&novo, 0, sizeof novo);
    bool unico = consoles->total == 1;
    if (unico) {
        snprintf(novo.nome, sizeof novo.nome, "%s", consoles->itens[0].nome);
        snprintf(novo.server_id, sizeof novo.server_id, "%s", consoles->itens[0].server_id);
        snprintf(novo.tipo, sizeof novo.tipo, "%s", consoles->itens[0].tipo);
    }
    if (unico == g_console_salvo_unico && !strcmp(novo.server_id, g_console_salvo.server_id) &&
        !strcmp(novo.nome, g_console_salvo.nome) && !strcmp(novo.tipo, g_console_salvo.tipo))
        return;
    g_console_salvo = novo;
    g_console_salvo_unico = unico;
    conta_salvar();
}

// ---------------------------------------------------------------------------
// Passo: jogos na nuvem (xCloud, fase X0)
// Acesso à oferta da nuvem e lista dos jogos em texto. Abrir um jogo é a X1.
// ---------------------------------------------------------------------------
static AuthStreaming g_nuvem;        // gsToken da nuvem (separado do console)
static bool g_nuvem_recusado = false; // a API respondeu 401: renovar o acesso antes de tentar de novo
#define NUVEM_RENOVAR_ANTES_S 300
static XTitulos g_titulos;           // lista carregada (total_servidor 0 = ainda não)
static char g_nuvem_ultimo[64];      // titleId do último jogo aberto (a grade volta nele)
static char g_busca[64];             // busca na grade ("" = todos)
static int *g_grade_idx;             // jogos da grade (índices em g_titulos), por nome
static int g_n_grade;
static int g_faixa_idx[32];          // jogados recentemente, na ordem
static int g_n_faixa;

static void nuvem_esquecer(void) {
    auth_streaming_liberar(&g_nuvem);
    memset(&g_nuvem, 0, sizeof g_nuvem);
    xnuvem_liberar(&g_titulos);
    g_nuvem_recusado = false;
    g_nuvem_ultimo[0] = '\0';
    g_busca[0] = '\0';
    free(g_grade_idx);
    g_grade_idx = NULL;
    g_n_grade = g_n_faixa = 0;
}

// O token da conta dura ~1 h e a nuvem pode ser aberta bem depois do início:
// renova a conta antes, como na renovação do streaming do console. v0.11.3: só
// se ele tiver mais de 30 min (log de 25/11: renovava 10 s depois da abertura
// já ter renovado, ~1 s a mais para a lista aparecer).
#define CONTA_RENOVAR_DEPOIS_S (30 * 60)
static AuthStatus obter_nuvem_novo(AuthConta *conta, AuthStreaming *novo) {
    bool recente = conta->obtido_em && time(NULL) - conta->obtido_em < CONTA_RENOVAR_DEPOIS_S;
    if (recente)
        return auth_nuvem(conta, novo);
    AuthStatus st = auth_renovar(conta);
    if (st == AUTH_OK) {
        if (!auth_salvar_refresh(NX_ARQ_TOKEN, conta->refresh_token))
            LOG("main: nao foi possivel salvar o token renovado");
        st = auth_nuvem(conta, novo);
    }
    return st;
}

static const char *oferta_amigavel(const char *oferta) {
    if (!strcmp(oferta, GSSV_OFERTA_NUVEM))
        return "Game Pass Ultimate";
    if (!strcmp(oferta, GSSV_OFERTA_NUVEM_F2P))
        return "só jogos gratuitos";
    return oferta;
}

// Copia UTF-8 sem cortar um caractere no meio (nomes longos de jogos)
static void copiar_utf8(char *dst, size_t tam, const char *src) {
    size_t n = strlen(src);
    if (n >= tam) {
        n = tam - 1;
        while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80)
            n--;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

// Erro na nuvem: A tenta de novo, B volta para os consoles
static Passo erro_nuvem(const char *titulo, const char *detalhe) {
    mostrar_mensagem(UI_ERRO, titulo, detalhe, "A=Tentar de novo|B=Voltar|+=Fechar");
    u64 b = esperar_botao(HidNpadButton_A | HidNpadButton_B | HidNpadButton_Plus);
    return b == HidNpadButton_A ? PASSO_NUVEM : b == HidNpadButton_B ? PASSO_LISTA : PASSO_SAIR;
}

static void mostrar_titulo_detalhe(const XTitulo *x) {
    char det[512];
    snprintf(det, sizeof det,
             "%s%s%sID na nuvem: %s\nID na loja: %s%s\n\nJogar na nuvem chega na próxima versão do NXStream.",
             x->editora[0] ? "Editora: " : "", x->editora, x->editora[0] ? "\n" : "", x->titulo_id,
             x->produto_id[0] ? x->produto_id : "-", x->recente ? "\nJogado recentemente" : "");
    mostrar_mensagem(UI_INFO, x->nome[0] ? x->nome : x->titulo_id, det, "B=Voltar");
    esperar_botao(HidNpadButton_B | HidNpadButton_A);
}

// --- Grade de capas da nuvem (v0.13.0) ----------------------------------------
#define NV_COLUNAS 7
#define NV_X0      102
#define NV_PASSO_X (UI_CAPA_L + 16)
#define NV_TOPO    100   // área rolável: das capas...
#define NV_BASE    592   // ...até o nome do jogo selecionado
#define NV_ROTULO  36
#define NV_ESPACO  22

// Linha r do conteúdo (faixa = linha 0, se houver): devolve o topo (com o título,
// se tiver) e põe em *cartao_y onde as capas começam
static int nv_linha(int r, bool tem_faixa, int *cartao_y) {
    int primeira_grade = tem_faixa ? 1 : 0, y = 0;
    for (int i = 0; i < r; i++) {
        bool rotulo = (tem_faixa && i == 0) || i == primeira_grade;
        y += (rotulo ? NV_ROTULO : 0) + UI_CAPA_A + NV_ESPACO;
    }
    bool rotulo = (tem_faixa && r == 0) || r == primeira_grade;
    *cartao_y = y + (rotulo ? NV_ROTULO : 0);
    return y;
}

static void nv_cartao(UiNuvem *n, int x, int y, int idx, bool selecionado) {
    if (n->n_cartoes >= UI_MAX_CARTOES)
        return;
    const XTitulo *t = &g_titulos.itens[idx];
    UiCartao *c = &n->cartoes[n->n_cartoes++];
    c->x = x;
    c->y = y;
    copiar_utf8(c->nome, sizeof c->nome, t->nome[0] ? t->nome : t->titulo_id);
    c->capa = capas_pegar(t->produto_id, t->capa);
    c->selecionado = selecionado;
}

static int comparar_por_nome(const void *a, const void *b) {
    const XTitulo *x = &g_titulos.itens[*(const int *)a], *y = &g_titulos.itens[*(const int *)b];
    const char *nx = x->nome[0] ? x->nome : x->titulo_id, *ny = y->nome[0] ? y->nome : y->titulo_id;
    int c = strcasecmp(nx, ny);
    return c ? c : strcmp(x->titulo_id, y->titulo_id);
}

// Grade: todos os jogos (ou os da busca) por nome. Faixa: os recentes, na ordem.
static void nuvem_montar_indices(void) {
    free(g_grade_idx);
    g_grade_idx = malloc(sizeof *g_grade_idx * (size_t)(g_titulos.total ? g_titulos.total : 1));
    g_n_grade = g_n_faixa = 0;
    for (int i = 0; g_grade_idx && i < g_titulos.total; i++) {
        const XTitulo *t = &g_titulos.itens[i];
        if (xnuvem_combina(t->nome[0] ? t->nome : t->titulo_id, g_busca))
            g_grade_idx[g_n_grade++] = i;
    }
    if (g_n_grade > 1)
        qsort(g_grade_idx, (size_t)g_n_grade, sizeof *g_grade_idx, comparar_por_nome);
    // xnuvem_filtrar_ordenar deixa os recentes primeiro, na ordem
    for (int i = 0; !g_busca[0] && i < g_titulos.total && g_titulos.itens[i].recente; i++)
        if (g_n_faixa < (int)(sizeof g_faixa_idx / sizeof g_faixa_idx[0]))
            g_faixa_idx[g_n_faixa++] = i;
}

// Teclado do Switch para a busca. true = o texto mudou (inclusive para vazio).
static bool busca_teclado(char *texto, size_t tam) {
    SwkbdConfig kbd;
    char saida[64] = "";
    Result rc = swkbdCreate(&kbd, 0);
    if (R_FAILED(rc)) {
        LOG("main: teclado indisponivel (0x%x)", rc);
        return false;
    }
    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetHeaderText(&kbd, "Buscar jogo");
    swkbdConfigSetGuideText(&kbd, "Nome do jogo");
    swkbdConfigSetOkButtonText(&kbd, "Buscar");
    swkbdConfigSetInitialText(&kbd, texto);
    swkbdConfigSetStringLenMax(&kbd, 40);
    rc = swkbdShow(&kbd, saida, sizeof saida);
    swkbdClose(&kbd);
    g_tick_anterior = armGetSystemTick(); // o tempo no teclado não é suspensão
    if (R_FAILED(rc)) // cancelado (B) ou erro: mantém a busca
        return false;
    // Tira espaços das pontas
    char *ini = saida;
    while (*ini == ' ')
        ini++;
    size_t n = strlen(ini);
    while (n && ini[n - 1] == ' ')
        ini[--n] = '\0';
    if (!strcmp(ini, texto))
        return false;
    snprintf(texto, tam, "%s", ini);
    return true;
}

static Passo passo_nuvem(AuthConta *conta) {
    // 1. Acesso (gsToken próprio, dura ~4 h)
    bool vencendo = g_nuvem.duracao_s > 0 && time(NULL) - g_nuvem.obtido_em > g_nuvem.duracao_s - NUVEM_RENOVAR_ANTES_S;
    if (!g_nuvem.gs_token || vencendo || g_nuvem_recusado) {
        mostrar_aguarde("Acessando o Xbox Cloud Gaming...");
        AuthStreaming novo = {0};
        AuthStatus st = (AuthStatus)chamar((Chamada){.tipo = CH_NUVEM_ACESSO, .conta = conta, .streaming = &novo});
        if (st == AUTH_INVALIDO) {
            auth_streaming_liberar(&novo);
            auth_apagar(NX_ARQ_TOKEN);
            auth_conta_liberar(conta);
            return PASSO_LOGIN;
        }
        if (st != AUTH_OK) {
            auth_streaming_liberar(&novo);
            return erro_nuvem("Não foi possível acessar o Xbox Cloud Gaming.", auth_ultimo_erro());
        }
        auth_streaming_liberar(&g_nuvem);
        g_nuvem = novo;
        g_nuvem_recusado = false;
    }

    // 2. Lista (fica na memória até Y=Atualizar)
    if (g_titulos.total_servidor == 0) {
        mostrar_aguarde("Carregando os jogos da nuvem...");
        if (!chamar((Chamada){.tipo = CH_NUVEM_LISTAR, .streaming = &g_nuvem, .saida = &g_titulos})) {
            if (strstr(xnuvem_ultimo_erro(), "HTTP 401"))
                g_nuvem_recusado = true; // acesso recusado: renova na próxima tentativa
            return erro_nuvem("Não foi possível carregar os jogos da nuvem.", xnuvem_ultimo_erro());
        }
    }

    // 3. Faixa de recentes + grade de capas (v0.13.0)
    aba_definir(true);
    capas_iniciar();
    nuvem_montar_indices();
    int zona = g_n_faixa ? 0 : 1, sel_f = 0, sel_g = 0;
    // Volta no último jogo aberto
    for (int i = 0; g_nuvem_ultimo[0] && i < g_n_faixa; i++)
        if (!strcmp(g_titulos.itens[g_faixa_idx[i]].titulo_id, g_nuvem_ultimo)) {
            zona = 0, sel_f = i;
            g_nuvem_ultimo[0] = '\0';
        }
    for (int i = 0; g_nuvem_ultimo[0] && i < g_n_grade; i++)
        if (!strcmp(g_titulos.itens[g_grade_idx[i]].titulo_id, g_nuvem_ultimo)) {
            zona = 1, sel_g = i;
            g_nuvem_ultimo[0] = '\0';
        }
    float rol = 0, rol_alvo = 0, fx = 0;
    int fprim = sel_f > NV_COLUNAS - 1 ? sel_f - NV_COLUNAS + 1 : 0;
    fx = (float)fprim;
    u64 segurando = 0;
    double segurando_desde = 0, ultima_repeticao = 0;
    bool primeiro_quadro = true;
    for (;;) {
        bool tem_faixa = g_n_faixa > 0;
        if (!tem_faixa)
            zona = 1;
        if (sel_g >= g_n_grade) sel_g = g_n_grade ? g_n_grade - 1 : 0;
        if (sel_f >= g_n_faixa) sel_f = g_n_faixa ? g_n_faixa - 1 : 0;
        int linhas = (tem_faixa ? 1 : 0) + (g_n_grade + NV_COLUNAS - 1) / NV_COLUNAS;
        int primeira_grade = tem_faixa ? 1 : 0;
        int linha_sel = zona == 0 ? 0 : primeira_grade + sel_g / NV_COLUNAS;

        // Rolagem: a linha selecionada inteira na área visível (animada)
        int cartao_y;
        int topo_sel = nv_linha(linha_sel, tem_faixa, &cartao_y);
        const int altura_vista = NV_BASE - NV_TOPO;
        if (topo_sel < rol_alvo)
            rol_alvo = (float)topo_sel;
        if (cartao_y + UI_CAPA_A > rol_alvo + altura_vista)
            rol_alvo = (float)(cartao_y + UI_CAPA_A - altura_vista);
        if (rol_alvo < 0)
            rol_alvo = 0;
        if (zona == 0) {
            if (sel_f < fprim) fprim = sel_f;
            if (sel_f >= fprim + NV_COLUNAS) fprim = sel_f - NV_COLUNAS + 1;
        }
        if (primeiro_quadro) {
            rol = rol_alvo, fx = (float)fprim;
            primeiro_quadro = false;
        }
        rol += (rol_alvo - rol) * 0.3f;
        fx += (fprim - fx) * 0.3f;
        if (fabsf(rol_alvo - rol) < 0.5f) rol = rol_alvo;
        if (fabsf(fprim - fx) < 0.01f) fx = (float)fprim;

        // Monta o que aparece (e pede as capas: visíveis e a próxima linha)
        capas_novo_quadro();
        static UiNuvem n;
        memset(&n, 0, sizeof n);
        n.clip_topo = NV_TOPO;
        n.clip_base = NV_BASE;
        int ultima_visivel = -1;
        for (int r = 0; r < linhas; r++) {
            int cy;
            int topo = nv_linha(r, tem_faixa, &cy);
            int sy = NV_TOPO + topo - (int)rol, scy = NV_TOPO + cy - (int)rol;
            if (scy + UI_CAPA_A < NV_TOPO)
                continue;
            if (sy > NV_BASE)
                break;
            ultima_visivel = r;
            if (topo != cy && n.n_rotulos < 4) { // linha com título
                UiRotulo *rt = &n.rotulos[n.n_rotulos++];
                rt->y = sy;
                if (tem_faixa && r == 0)
                    snprintf(rt->texto, sizeof rt->texto, "Jogados recentemente");
                else if (g_busca[0])
                    snprintf(rt->texto, sizeof rt->texto, "Resultados para “%.40s” (%d)", g_busca, g_n_grade);
                else
                    snprintf(rt->texto, sizeof rt->texto, "Todos os jogos (%d) · %s", g_n_grade,
                             oferta_amigavel(g_nuvem.oferta));
            }
            if (tem_faixa && r == 0) {
                int i0 = (int)fx - 1;
                for (int i = i0 < 0 ? 0 : i0; i < g_n_faixa && i <= (int)fx + NV_COLUNAS; i++) {
                    int x = NV_X0 + (int)((i - fx) * NV_PASSO_X);
                    if (x < 0 || x + UI_CAPA_L > UI_LARGURA)
                        continue;
                    nv_cartao(&n, x, scy, g_faixa_idx[i], zona == 0 && i == sel_f);
                }
            } else {
                int base = (r - primeira_grade) * NV_COLUNAS;
                for (int c = 0; c < NV_COLUNAS && base + c < g_n_grade; c++)
                    nv_cartao(&n, NV_X0 + c * NV_PASSO_X, scy, g_grade_idx[base + c], zona == 1 && base + c == sel_g);
            }
        }
        // Próxima linha da grade: só pede as capas (chegam antes de rolar)
        if (ultima_visivel >= 0 && ultima_visivel + 1 < linhas) {
            int base = (ultima_visivel + 1 - primeira_grade) * NV_COLUNAS;
            for (int c = 0; base >= 0 && c < NV_COLUNAS && base + c < g_n_grade; c++) {
                const XTitulo *x = &g_titulos.itens[g_grade_idx[base + c]];
                capas_pegar(x->produto_id, x->capa);
            }
        }

        const XTitulo *atual = NULL;
        if (zona == 0 && g_n_faixa)
            atual = &g_titulos.itens[g_faixa_idx[sel_f]];
        else if (zona == 1 && g_n_grade)
            atual = &g_titulos.itens[g_grade_idx[sel_g]];
        if (atual) {
            copiar_utf8(n.nome, sizeof n.nome, atual->nome[0] ? atual->nome : atual->titulo_id);
            snprintf(n.info, sizeof n.info, "%s%s%s", atual->editora, atual->editora[0] && atual->recente ? " · " : "",
                     atual->recente ? "jogado recentemente" : "");
        } else if (g_busca[0]) {
            snprintf(n.aviso, sizeof n.aviso, "Nenhum jogo com “%.40s”.\nY busca de novo; B mostra todos.", g_busca);
        } else {
            snprintf(n.aviso, sizeof n.aviso, "Nenhum jogo liberado para esta conta na nuvem.");
        }
        const char *rodape = g_busca[0] ? "A=Jogar|X=Detalhes|Y=Buscar|B=Limpar busca|+=Fechar"
                                        : "A=Jogar|X=Detalhes|Y=Buscar|-=Atualizar|+=Fechar";
        modo_ui();
        if (ui_ativa()) {
            ui_definir_abas(1);
            ui_nuvem(&n, rodape);
        } else {
            cabecalho();
            printf("Jogos na nuvem (%d)\n\n%s\n\nA = jogar   Y = buscar   L/R = consoles   + = fechar\n", g_n_grade,
                   atual ? n.nome : n.aviso);
        }

        bool fechar = false;
        u64 b = quadro(&fechar);
        if (fechar || (b & HidNpadButton_Plus)) {
            ui_definir_abas(-1);
            return PASSO_SAIR;
        }
        if (b & (HidNpadButton_L | HidNpadButton_R)) {
            ui_definir_abas(-1);
            return PASSO_LISTA; // aba Consoles
        }
        if ((b & HidNpadButton_B) && g_busca[0]) {
            g_busca[0] = '\0';
            nuvem_montar_indices();
            zona = g_n_faixa ? 0 : 1, sel_f = sel_g = 0, rol_alvo = 0, fprim = 0;
            continue;
        }
        if (b & HidNpadButton_Y) {
            if (busca_teclado(g_busca, sizeof g_busca)) {
                nuvem_montar_indices();
                zona = 1, sel_g = 0, rol_alvo = 0;
                LOG("main: busca na nuvem: \"%s\" (%d jogo(s))", g_busca, g_n_grade);
            }
            continue;
        }
        if ((b & HidNpadButton_Minus) && !g_busca[0]) {
            ui_definir_abas(-1);
            xnuvem_liberar(&g_titulos);
            return PASSO_NUVEM;
        }
        if ((b & HidNpadButton_X) && atual) {
            ui_definir_abas(-1);
            mostrar_titulo_detalhe(atual);
            continue;
        }
        if ((b & HidNpadButton_A) && atual) {
            // Fase X1: abre o jogo na nuvem pelo mesmo fluxo da sessão do console
            ui_definir_abas(-1);
            g_jogo = *atual;
            g_na_nuvem = true;
            memset(&g_console, 0, sizeof g_console);
            copiar_utf8(g_console.nome, sizeof g_console.nome, g_jogo.nome[0] ? g_jogo.nome : g_jogo.titulo_id);
            snprintf(g_console.tipo, sizeof g_console.tipo, "Xbox Cloud Gaming");
            snprintf(g_nuvem_ultimo, sizeof g_nuvem_ultimo, "%s", g_jogo.titulo_id);
            LOG("main: jogo na nuvem escolhido: %s (%s)", g_console.nome, g_jogo.titulo_id);
            return PASSO_SESSAO;
        }

        // Direcional: um passo ao apertar; segurando, repete depois de 0,4 s
        const u64 dirs = HidNpadButton_AnyUp | HidNpadButton_AnyDown | HidNpadButton_AnyLeft | HidNpadButton_AnyRight;
        u64 mover = b & dirs;
        u64 seguro = padGetButtons(&g_controle) & dirs;
        double agora = segundos_desde(0);
        if (mover) {
            segurando = mover;
            segurando_desde = ultima_repeticao = agora;
        } else if (seguro && (seguro & segurando)) {
            if (agora - segurando_desde > 0.4 && agora - ultima_repeticao > 0.09) {
                mover = seguro & segurando;
                ultima_repeticao = agora;
            }
        } else {
            segurando = 0;
        }
        if (zona == 0) {
            if ((mover & HidNpadButton_AnyLeft) && sel_f > 0) sel_f--;
            if ((mover & HidNpadButton_AnyRight) && sel_f + 1 < g_n_faixa) sel_f++;
            if ((mover & HidNpadButton_AnyDown) && g_n_grade) {
                int col = sel_f - fprim;
                zona = 1;
                sel_g = col < g_n_grade ? col : g_n_grade - 1;
            }
        } else {
            int col = sel_g % NV_COLUNAS;
            if ((mover & HidNpadButton_AnyLeft) && sel_g > 0) sel_g--;
            if ((mover & HidNpadButton_AnyRight) && sel_g + 1 < g_n_grade) sel_g++;
            if (mover & HidNpadButton_AnyUp) {
                if (sel_g >= NV_COLUNAS) {
                    sel_g -= NV_COLUNAS;
                } else if (g_n_faixa) {
                    zona = 0;
                    sel_f = fprim + col < g_n_faixa ? fprim + col : g_n_faixa - 1;
                }
            }
            if (mover & HidNpadButton_AnyDown) {
                if (sel_g + NV_COLUNAS < g_n_grade)
                    sel_g += NV_COLUNAS;
                else if (sel_g / NV_COLUNAS < (g_n_grade - 1) / NV_COLUNAS)
                    sel_g = g_n_grade - 1; // última linha incompleta
            }
        }
    }
}

static Passo esperar_xbox_ligar(AuthStreaming *streaming);

static bool console_esta_desligado(const XConsole *x) {
    return g_console_desligado[0] && !strcmp(x->server_id, g_console_desligado) && strcmp(x->estado, "On");
}

static Passo passo_lista(AuthConta *conta, AuthStreaming *streaming) {
    g_na_nuvem = false; // daqui em diante as sessões são do console
    // gsToken vencendo (dura ~4 h): obtém outro antes de usar
    if (streaming->duracao_s > 0 && time(NULL) - streaming->obtido_em > streaming->duracao_s - 120)
        return PASSO_STREAMING;

    // Abertura rápida: a conta tinha um só console da última vez. Abre a sessão
    // direto; B na tela de conexão volta para a lista (que aí é consultada).
    // v0.13.0: o app abre na última aba usada
    if (!g_auto_conexao_feita && g_aba_nuvem) {
        g_auto_conexao_feita = true;
        LOG("main: abrindo na aba Nuvem (ultima usada)");
        return PASSO_NUVEM;
    }
    aba_definir(false);

    // v0.12.4: se ele estava desligado da última vez, consulta a lista antes
    // (se já estiver "On", conecta; senão, espera ser ligado sem tentar à toa)
    bool estava_desligado = g_console_salvo.server_id[0] && !strcmp(g_console_salvo.server_id, g_console_desligado);
    if (!g_auto_conexao_feita && g_console_salvo_unico && g_console_salvo.server_id[0] && !estava_desligado) {
        g_auto_conexao_feita = true;
        g_console = g_console_salvo;
        LOG("main: abertura rapida: direto para %s (console salvo, sem consultar a lista)", g_console.nome);
        return PASSO_SESSAO;
    }

    mostrar_aguarde("Procurando seus consoles...");
    XConsoles consoles;
    if (!chamar((Chamada){.tipo = CH_LISTAR, .streaming = streaming, .saida = &consoles})) {
        u64 b = mostrar_erro("Não foi possível listar seus consoles.", xapi_ultimo_erro(), true);
        if (b == HidNpadButton_X && confirmar_sair_da_conta()) {
            auth_apagar(NX_ARQ_TOKEN);
            auth_conta_liberar(conta);
            return PASSO_LOGIN;
        }
        return b == HidNpadButton_Plus || b == 0 ? PASSO_SAIR : PASSO_LISTA;
    }

    conta_lembrar_consoles(&consoles);
    // "On" é confiável: o console marcado como desligado foi ligado
    for (int i = 0; i < consoles.total; i++)
        if (!strcmp(consoles.itens[i].estado, "On") && !strcmp(consoles.itens[i].server_id, g_console_desligado))
            console_marcar_desligado(NULL);

    // Só um console na conta: na primeira vez depois do login/abertura do app,
    // conecta sozinho, sem espera (até a v0.10.4 havia 2 s para desistir; agora
    // o B da tela de conexão faz isso). Depois de desconectar, a lista fica parada.
    if (!g_auto_conexao_feita && consoles.total == 1) {
        g_auto_conexao_feita = true;
        g_console = consoles.itens[0];
        LOG("main: console escolhido automaticamente (unico na conta): %s (%s)", g_console.nome, g_console.estado);
        if (console_esta_desligado(&g_console))
            return esperar_xbox_ligar(streaming);
        return PASSO_SESSAO;
    }
    g_auto_conexao_feita = true;

    int sel = 0;
    const char *mensagem = NULL;
    for (;;) {
        char conta_txt[96];
        if (streaming->gamertag[0])
            snprintf(conta_txt, sizeof conta_txt, "Conectado como %s", streaming->gamertag);
        else
            snprintf(conta_txt, sizeof conta_txt, "Conta conectada");
        UiItem itens[UI_MAX_ITENS];
        int total = consoles.total < UI_MAX_ITENS ? consoles.total : UI_MAX_ITENS;
        for (int i = 0; i < total; i++) {
            const XConsole *x = &consoles.itens[i];
            snprintf(itens[i].nome, sizeof itens[i].nome, "%s", x->nome);
            bool desligado = console_esta_desligado(x);
            snprintf(itens[i].detalhe, sizeof itens[i].detalhe, "%s · %s", xapi_tipo_amigavel(x->tipo),
                     desligado ? "Desligado (ligue pelo botão)" : xapi_estado_amigavel(x->estado));
            itens[i].estado = !strcmp(x->estado, "On") ? 1
                            : desligado                       ? 0
                            : !strcmp(x->estado, "ConnectedStandby") ? 2 : 0;
        }
        const char *vazia = "Nenhum console encontrado nesta conta.\nNo Xbox, ative em: Configurações > "
                            "Dispositivos e conexões > Recursos remotos > Habilitar recursos remotos.";
        modo_ui();
        if (ui_ativa()) {
            ui_definir_abas(0); // L/R troca para a aba Nuvem
            ui_lista("Seus consoles", conta_txt, itens, total, sel, total ? mensagem : vazia,
                     total ? "A=Conectar|Y=Atualizar|X=Sair da conta|-=Diagnóstico|+=Fechar"
                           : "Y=Atualizar|X=Sair da conta|-=Diagnóstico|+=Fechar");
        } else {
            cabecalho();
            printf("%s\n\n", conta_txt);
            for (int i = 0; i < total; i++)
                printf("%s %s (%s)\n" COR_FIM, i == sel ? COR_DESTAQ " >" : "  ", itens[i].nome, itens[i].detalhe);
            printf("\n%s\nA = conectar   L/R = nuvem   Y = atualizar   X = sair da conta   - = diagnostico   + = fechar\n",
                   total ? (mensagem ? mensagem : "") : vazia);
        }

        u64 b = esperar_botao(HidNpadButton_A | HidNpadButton_Y | HidNpadButton_X | HidNpadButton_Plus |
                              HidNpadButton_Minus | HidNpadButton_L | HidNpadButton_R | HidNpadButton_AnyUp |
                              HidNpadButton_AnyDown);
        mensagem = NULL;
        if (b == 0 || (b & HidNpadButton_Plus))
            return PASSO_SAIR;
        if (b & (HidNpadButton_L | HidNpadButton_R))
            return PASSO_NUVEM;
        if (b & HidNpadButton_Y)
            return PASSO_LISTA;
        if (b & HidNpadButton_Minus)
            return PASSO_DIAG;
        if ((b & HidNpadButton_X) && confirmar_sair_da_conta()) {
            auth_apagar(NX_ARQ_TOKEN);
            auth_conta_liberar(conta);
            LOG("main: usuario saiu da conta");
            conta_esquecer();
            nuvem_esquecer();
            ui_definir_conta(NULL);
            g_auto_conexao_feita = false; // depois do próximo login, conecta sozinho de novo
            return PASSO_LOGIN;
        }
        if ((b & HidNpadButton_AnyUp) && sel > 0)
            sel--;
        if ((b & HidNpadButton_AnyDown) && sel < consoles.total - 1)
            sel++;
        if ((b & HidNpadButton_A) && consoles.total > 0) {
            g_console = consoles.itens[sel];
            LOG("main: console escolhido: %s (%s)", g_console.nome, g_console.estado);
            return PASSO_SESSAO;
        }
    }
}

// ---------------------------------------------------------------------------
// Passo: sessão de streaming (fase 2a)
// Abre a sessão, acompanha os estados até ficar pronta e a mantém viva.
// A conexão com o console (WebRTC) entra na fase 2b.
// ---------------------------------------------------------------------------

// Tela de erro da sessão. Retorna o próximo passo.
// Espera do Xbox ser ligado (esperar_xbox_ligar, v0.12.4)
static double g_espera_desde = -1;  // início da espera (segundos_desde(0)); -1 = sem espera

static Passo erro_sessao(const char *titulo, const char *detalhe) {
    g_espera_desde = -1; // um erro de verdade encerra a espera
    if (detalhe && strstr(detalhe, "HTTP 401"))
        g_forcar_renovacao = true; // token recusado: renova antes da próxima tentativa
    if (g_reconectando) { // fase 4: durante a reconexão, a falha vira nova tentativa
        LOG("main: tentativa falhou: %s %s", titulo, detalhe ? detalhe : "");
        return PASSO_RECONECTAR;
    }
    mostrar_mensagem(UI_ERRO, titulo, detalhe, "A=Tentar de novo|B=Voltar para a lista|+=Fechar");
    u64 b = esperar_botao(HidNpadButton_A | HidNpadButton_B | HidNpadButton_Plus);
    return b == HidNpadButton_A ? PASSO_SESSAO : b == HidNpadButton_B ? PASSO_LISTA : PASSO_SAIR;
}

// ---------------------------------------------------------------------------
// Renovação do acesso ao streaming (v0.9.2). O gsToken vale ~4 h (log: "valido
// por 14400 s"). Depois disso toda chamada à API responde HTTP 401: no log da
// v0.9.1, ao acordar o Switch depois de 6 h, as 10 tentativas de reconexão
// falharam assim. Agora o token é renovado antes de vencer: durante o streaming
// numa thread (sem travar o vídeo) e antes de cada tentativa de reconexão.
// ---------------------------------------------------------------------------
#define RENOVAR_ANTES_S        900  // conectado: renova 15 min antes de vencer
#define RENOVAR_ANTES_SESSAO_S 300  // antes de abrir uma sessão

static bool streaming_vencendo(const AuthStreaming *s, int margem_s) {
    if (g_forcar_renovacao)
        return true;
    return s->duracao_s > 0 && time(NULL) - s->obtido_em > s->duracao_s - margem_s;
}

// Renova a conta e obtém um gsToken novo em *novo (não mexe no atual)
static AuthStatus obter_streaming_novo(AuthConta *conta, AuthStreaming *novo) {
    if (g_na_nuvem) // sessão na nuvem: o token a renovar é o da oferta xgpuweb
        return obter_nuvem_novo(conta, novo);
    AuthStatus st = auth_renovar(conta);
    if (st == AUTH_OK) {
        if (!auth_salvar_refresh(NX_ARQ_TOKEN, conta->refresh_token))
            LOG("main: nao foi possivel salvar o token renovado");
        snprintf(novo->gamertag, sizeof novo->gamertag, "%s", g_gamertag); // já conhecida
        st = auth_streaming(conta, novo);
    }
    return st;
}

static void keepalive_esperar(void);
static void mostrar_conexao(const char *status, const char *detalhe, const char *rodape);

static void aplicar_streaming_novo(AuthStreaming *atual, AuthStreaming *novo) {
    keepalive_esperar(); // o keepalive em voo lê o token atual
    auth_streaming_liberar(atual);
    *atual = *novo;
    memset(novo, 0, sizeof *novo);
    if (!atual->obtido_em)
        atual->obtido_em = time(NULL);
    g_forcar_renovacao = false;
}

// Antes de abrir uma sessão (inclusive ao reconectar): renova se estiver vencendo
static void garantir_streaming(AuthConta *conta, AuthStreaming *s) {
    if (!streaming_vencendo(s, RENOVAR_ANTES_SESSAO_S))
        return;
    LOG("main: acesso ao streaming %s (obtido ha %ld s); renovando",
        g_forcar_renovacao ? "recusado (HTTP 401)" : "vencendo", (long)(time(NULL) - s->obtido_em));
    if (g_reconectando || g_console.nome[0]) {
        mostrar_conexao("Renovando o acesso ao Xbox Live", NULL, NULL);
        mostrar_agora();
    } else {
        mostrar_aguarde("Renovando o acesso ao Xbox Live...");
    }
    AuthStreaming novo = {0};
    AuthStatus st = (AuthStatus)chamar((Chamada){.tipo = CH_OBTER_NOVO, .conta = conta, .streaming = &novo});
    if (st == AUTH_OK) {
        aplicar_streaming_novo(s, &novo);
        LOG("main: acesso ao streaming renovado");
    } else {
        LOG("main: falha ao renovar o acesso (%s)", auth_ultimo_erro());
        auth_streaming_liberar(&novo);
    }
}

// Renovação em segundo plano enquanto conectado
static struct {
    pthread_t thread;
    bool rodando;
    volatile bool pronto;
    AuthStatus st;
    AuthStreaming novo;
    double ultima_tentativa; // segundos desde o início da conexão
} g_ren;

static void *thread_renovacao(void *arg) {
    (void)arg;
    g_ren.st = obter_streaming_novo(g_conta, &g_ren.novo);
    __atomic_store_n(&g_ren.pronto, true, __ATOMIC_RELEASE);
    return NULL;
}

static void renovacao_disparar(void) {
    if (g_ren.rodando || !g_conta)
        return;
    LOG("main: acesso ao streaming vencendo; renovando em segundo plano");
    memset(&g_ren.novo, 0, sizeof g_ren.novo);
    g_ren.pronto = false;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 256 * 1024); // curl + TLS
    g_ren.rodando = pthread_create(&g_ren.thread, &at, thread_renovacao, NULL) == 0;
    pthread_attr_destroy(&at);
    if (!g_ren.rodando)
        LOG("main: sem thread para renovar; fica para a proxima reconexao");
}

// Junta a thread (esperando, se `esperar`) e aplica o token novo
static void renovacao_concluir(AuthStreaming *s, bool esperar) {
    if (!g_ren.rodando)
        return;
    if (!esperar && !__atomic_load_n(&g_ren.pronto, __ATOMIC_ACQUIRE))
        return;
    pthread_join(g_ren.thread, NULL);
    g_ren.rodando = false;
    if (g_ren.st == AUTH_OK) {
        aplicar_streaming_novo(s, &g_ren.novo);
        LOG("main: acesso ao streaming renovado durante a conexao");
    } else {
        LOG("main: falha ao renovar o acesso (%s); nova tentativa em 1 min", auth_ultimo_erro());
        auth_streaming_liberar(&g_ren.novo);
    }
}

// ---------------------------------------------------------------------------
// Keepalive em segundo plano (v0.7.2): a requisição HTTP leva até algumas
// centenas de ms; feita na thread principal, travava o vídeo a cada 30 s.
// ---------------------------------------------------------------------------
static struct {
    pthread_t thread;
    bool rodando;           // há uma thread (terminada ou não) para juntar
    volatile bool pronto;   // a thread terminou; o resultado está em `status`
    long status;
    const AuthStreaming *s;
    const XSessao *sessao;
} g_ka;

static void *thread_keepalive(void *arg) {
    (void)arg;
    g_ka.status = xapi_sessao_keepalive(g_ka.s, g_ka.sessao);
    __atomic_store_n(&g_ka.pronto, true, __ATOMIC_RELEASE);
    return NULL;
}

// Dispara um keepalive se não houver outro em andamento. Retorna false se não deu.
static bool keepalive_disparar(const AuthStreaming *s, const XSessao *sessao) {
    if (g_ka.rodando)
        return false;
    g_ka.s = s;
    g_ka.sessao = sessao;
    g_ka.status = 0;
    g_ka.pronto = false;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 256 * 1024); // curl + TLS
    bool ok = pthread_create(&g_ka.thread, &at, thread_keepalive, NULL) == 0;
    pthread_attr_destroy(&at);
    if (!ok) { // sem thread: faz na hora, como antes
        LOG("main: keepalive sem thread; enviando direto");
        g_ka.status = xapi_sessao_keepalive(s, sessao);
        g_ka.pronto = true;
        return true;
    }
    g_ka.rodando = true;
    return true;
}

// Se o keepalive terminou, entrega o status (HTTP) e libera para o próximo
static bool keepalive_resultado(long *status) {
    if (!__atomic_load_n(&g_ka.pronto, __ATOMIC_ACQUIRE))
        return false;
    if (g_ka.rodando) {
        pthread_join(g_ka.thread, NULL);
        g_ka.rodando = false;
    }
    g_ka.pronto = false;
    *status = g_ka.status;
    return true;
}

// Espera um keepalive em andamento (antes de encerrar a sessão)
static void keepalive_esperar(void) {
    if (g_ka.rodando) {
        pthread_join(g_ka.thread, NULL);
        g_ka.rodando = false;
    }
    g_ka.pronto = false;
}

static void encerrar_sessao(const AuthStreaming *streaming, const XSessao *sessao) {
    mostrar_aguarde("Encerrando a sessão...");
    keepalive_esperar(); // não encerrar com um keepalive ainda em voo
    xapi_sessao_encerrar(streaming, sessao);
}

// ---------------------------------------------------------------------------
// Conexão WebRTC com o console (fase 2b, parte 2)
// docs/PROTOCOLO.md, seção 3: oferta -> resposta -> candidatos -> conexão -> handshake
// ---------------------------------------------------------------------------
typedef enum { ET_OFERTA, ET_RESPOSTA, ET_CANDIDATOS, ET_CONECTANDO, ET_CONECTADO } EtapaConexao;

#define CONEXAO_LIMITE_OFERTA_S     10
#define CONEXAO_LIMITE_TROCA_S      30   // resposta SDP e candidatos
#define CONEXAO_LIMITE_CONECTAR_S   20
#define CONEXAO_INTERVALO_CONSULTA  0.25

// Título das telas de conexão: "Conectando a XBOX" ou "Reconectando a XBOX"
static const char *titulo_conexao(void) {
    static char t[96];
    snprintf(t, sizeof t, "%s a %s", g_reconectando ? "Reconectando" : "Conectando", g_console.nome);
    return t;
}

static void mostrar_conexao(const char *status, const char *detalhe, const char *rodape) {
    modo_ui();
    if (ui_ativa()) {
        ui_status(titulo_conexao(), status, detalhe, rodape);
    } else {
        cabecalho();
        printf("%s\n\n%s\n%s\n", titulo_conexao(), status ? status : "", detalhe ? detalhe : "");
    }
}

static Passo falha_conexao(const AuthStreaming *s, const XSessao *sessao,
                           const char *titulo, const char *detalhe) {
    LOG("main: conexao: %s %s", titulo, detalhe ? detalhe : "");
    modo_ui();
    conexao_encerrar();
    if (detalhe && strstr(detalhe, "HTTP 401"))
        g_forcar_renovacao = true;
    if (g_reconectando) // fase 4: passo_sessao tenta de novo com uma sessão nova
        return PASSO_RECONECTAR;
    encerrar_sessao(s, sessao);
    return erro_sessao(titulo, detalhe);
}

// A conexão caiu depois de conectada: solta a tela e a conexão (a sessão continua)
static Passo cair_para_reconexao(const char *motivo, bool repouso) {
    LOG("main: %s; reconectando", motivo);
    // A tela de reconexão aparece já: antes o último quadro do vídeo ficava
    // parado na tela até a sessão nova começar (até ~5 s ao voltar do repouso)
    modo_ui();
    g_reconectando = true;
    mostrar_conexao(repouso ? "Voltando do repouso" : "A conexão caiu", "Abrindo uma sessão nova com o console",
                    NULL);
    mostrar_agora();
    conexao_encerrar();
    return PASSO_RECONECTAR;
}

// Menu do vídeo (+ e - segurados por 1 s)
enum { MENU_VOLTAR, MENU_XBOX, MENU_ESTATISTICAS, MENU_DESCONECTAR, MENU_FECHAR, MENU_ITENS };

static void desenhar_menu(int item, bool estatisticas, double conectado_ha) {
    const char *nomes[MENU_ITENS] = {"Voltar ao jogo", "Botão Xbox (guia)",
                                     estatisticas ? "Esconder estatísticas" : "Mostrar estatísticas",
                                     "Desconectar", "Fechar o NXStream"};
    char sub[192];
    int seg = conectado_ha > 0 ? (int)conectado_ha : 0;
    if (seg >= 3600)
        snprintf(sub, sizeof sub, "%s · conectado há %d h %02d min", g_console.nome, seg / 3600, seg % 3600 / 60);
    else
        snprintf(sub, sizeof sub, "%s · conectado há %d min", g_console.nome, seg / 60);
    sobre_menu("NXStream", sub, nomes, MENU_ITENS, item, "A=Escolher|B=Voltar");
}

static void desenhar_aviso_menu(void) {
    const char *t = " Segure + e - por 1 s para abrir o menu ";
    sobre_cursor((SOBRE_COLUNAS - (int)strlen(t)) / 2, SOBRE_LINHAS - 2);
    sobre_printf("%s", t);
}

static Passo passo_conectar(AuthStreaming *streaming, XSessao *sessao, double tempo_preparo, int tentativa) {
    char tent[48] = "";
    if (tentativa > 1)
        snprintf(tent, sizeof tent, "Tentativa %d de 5", tentativa);
    mostrar_conexao("Montando a conexão", tent, NULL);
    mostrar_agora();
    if (!conexao_criar()) {
        ConexaoEstado e;
        conexao_ler(&e);
        return falha_conexao(streaming, sessao, "Nao foi possivel montar a conexao WebRTC.", e.erro);
    }
    g_tick_anterior = armGetSystemTick();

    EtapaConexao etapa = ET_OFERTA;
    u64 inicio = armGetSystemTick(), inicio_etapa = inicio;
    double t_etapa[ET_CONECTADO + 1] = {0}; // quando cada etapa terminou (desde o início)
    double proxima_consulta = 0;
    XCandidatos remotos = {0};
    int remotos_aceitos = 0;

    u64 ultimo_keepalive = 0;
    // Estatísticas de vídeo por segundo (fase 2c.1)
    double t_janela = 0, fps = 0, mbps = 0;
    unsigned long quadros_janela = 0, bytes_janela = 0;
    double ultimo_pedido_chave = -10;
    unsigned long decod_janela = 0;
    double fps_decod = 0;
    float hist_mbps[UI_HIST]; // taxa de cada segundo, para o gráfico das estatísticas
    int hist_inicio = 0, hist_total = 0;
    double t_log_video = 0;
    long status_keepalive = -1;
    int ka_ok = 0, ka_falhas = 0;
    bool keepalive_apos_sleep = false, keepalive_marcar = false;
    char apos_sleep[160] = "";
    int ultimo_quadro = -1;
    bool fechar = false;
    g_ultimo_congelamento = 0;
    // Vídeo na tela e menu (fase 2c.3)
    bool video_tentou = false;
    char video_erro[160] = "";
    double t_video = 0;
    bool menu_aberto = false, menu_mudou = false, esperar_soltar = false;
    bool mostrar_estatisticas = false;
    int menu_item = 0;
    double segurando_desde = -1;
    // Controle (fase 3)
    MetadadoQuadro metadados[INPUT_MAX_METADADOS];
    int num_metadados = 0;
    uint32_t seq_input = 0;       // o pacote de metadados do cliente (sequência 0) já foi pela conexão
    unsigned long pacotes_input = 0;
    double nexus_ate = 0, menu_xbox_ate = 0;
    double t_queda = -1; // fase 4: quando a conexão saiu de "conectado" (-1 = está conectada)
    double mais_desde = -1, menos_desde = -1; // atalho do botão Xbox (+ segurado, clique no -)
    bool mais_em_atalho = false;
    bool primeiro_botao_logado = false;

    for (;;) {
        ConexaoEstado e;
        conexao_ler(&e);
        double na_etapa = segundos_desde(inicio_etapa);

        if (e.erro[0])
            return falha_conexao(streaming, sessao, "Erro na conexao WebRTC.", e.erro);

        switch (etapa) {
            case ET_OFERTA:
                if (e.oferta_pronta && e.coleta_completa) {
                    char *oferta = conexao_oferta();
                    bool ok = oferta && chamar((Chamada){.tipo = CH_ENVIAR_SDP, .streaming = streaming,
                                                         .sessao = sessao, .texto = oferta});
                    free(oferta);
                    if (!ok)
                        return falha_conexao(streaming, sessao, "O console nao recebeu a oferta.", xapi_ultimo_erro());
                    t_etapa[ET_OFERTA] = segundos_desde(inicio);
                    etapa = ET_RESPOSTA;
                    inicio_etapa = armGetSystemTick();
                    proxima_consulta = 0;
                } else if (na_etapa > CONEXAO_LIMITE_OFERTA_S) {
                    return falha_conexao(streaming, sessao, "A oferta local nao ficou pronta.", NULL);
                }
                break;

            case ET_RESPOSTA:
                if (na_etapa >= proxima_consulta) {
                    char *resposta = NULL;
                    int st = (int)chamar((Chamada){.tipo = CH_RECEBER_SDP, .streaming = streaming,
                                                   .sessao = sessao, .saida = &resposta});
                    proxima_consulta = segundos_desde(inicio_etapa) + CONEXAO_INTERVALO_CONSULTA;
                    if (st < 0)
                        return falha_conexao(streaming, sessao, "O console nao aceitou a oferta.", xapi_ultimo_erro());
                    if (st == 1) {
                        bool ok = conexao_aplicar_resposta(resposta);
                        free(resposta);
                        if (!ok) {
                            conexao_ler(&e);
                            return falha_conexao(streaming, sessao, "A resposta do console nao foi aceita.", e.erro);
                        }
                        if (!chamar((Chamada){.tipo = CH_ENVIAR_ICE, .streaming = streaming, .sessao = sessao,
                                              .entrada = &e.locais, .texto = e.ufrag}))
                            return falha_conexao(streaming, sessao, "O console nao recebeu os candidatos de rede.",
                                                 xapi_ultimo_erro());
                        t_etapa[ET_RESPOSTA] = segundos_desde(inicio);
                        etapa = ET_CANDIDATOS;
                        inicio_etapa = armGetSystemTick();
                        proxima_consulta = 0;
                    }
                }
                if (etapa == ET_RESPOSTA && segundos_desde(inicio_etapa) > CONEXAO_LIMITE_TROCA_S)
                    return falha_conexao(streaming, sessao, "O console nao respondeu a oferta.", NULL);
                break;

            case ET_CANDIDATOS:
                if (na_etapa >= proxima_consulta) {
                    int st = (int)chamar((Chamada){.tipo = CH_RECEBER_ICE, .streaming = streaming,
                                                   .sessao = sessao, .saida = &remotos});
                    proxima_consulta = segundos_desde(inicio_etapa) + CONEXAO_INTERVALO_CONSULTA;
                    if (st < 0)
                        return falha_conexao(streaming, sessao, "Falha ao receber os candidatos do console.",
                                             xapi_ultimo_erro());
                    if (st == 1) {
                        // Só IPv4 da rede local: público/IPv6 não servem e IPv6 fechava o app
                        int recebidos = remotos.total;
                        if (g_na_nuvem)
                            xapi_filtrar_ipv4(&remotos); // servidores do xCloud: endereços públicos
                        else
                            xapi_filtrar_rede_local(&remotos);
                        LOG("main: %d candidato(s) recebido(s), %d usado(s) (%s)", recebidos, remotos.total,
                            g_na_nuvem ? "IPv4" : "rede local");
                        remotos_aceitos = remotos.total > 0 ? conexao_adicionar_remotos(&remotos) : 0;
                        t_etapa[ET_CANDIDATOS] = segundos_desde(inicio);
                        etapa = ET_CONECTANDO;
                        inicio_etapa = armGetSystemTick();
                    }
                }
                if (etapa == ET_CANDIDATOS && segundos_desde(inicio_etapa) > CONEXAO_LIMITE_TROCA_S)
                    return falha_conexao(streaming, sessao, "O console nao enviou os candidatos de rede.", NULL);
                break;

            case ET_CONECTANDO:
                if (e.conectado) {
                    t_etapa[ET_CONECTANDO] = e.t_conectado;
                    etapa = ET_CONECTADO;
                    g_chegou_a_conectar = true;
                    if (tentativa > 0)
                        LOG("main: reconectado (tentativa %d), %.1f s desde a queda", tentativa,
                            g_tick_queda ? segundos_desde(g_tick_queda) : 0.0);
                    ultimo_keepalive = armGetSystemTick();
                    g_ultimo_congelamento = 0;
                    LOG("main: conectado ao console em %.2f s (oferta %.1f s, resposta %.1f s, candidatos %.1f s, %d candidato(s) aceito(s))", e.t_conectado, t_etapa[ET_OFERTA], t_etapa[ET_RESPOSTA], t_etapa[ET_CANDIDATOS], remotos_aceitos);
                } else if (e.falhou) {
                    return falha_conexao(streaming, sessao, "A conexao com o console falhou.",
                                         "Confira se o Switch e o Xbox estao na mesma rede.");
                } else if (na_etapa > CONEXAO_LIMITE_CONECTAR_S) {
                    return falha_conexao(streaming, sessao, "O console nao completou a conexao.",
                                         "Confira se o Switch e o Xbox estao na mesma rede.");
                }
                break;

            case ET_CONECTADO: {
                // Quadros e taxa a cada segundo
                double agora = segundos_desde(inicio);
                if (agora - t_janela >= 1.0) {
                    double dt = agora - t_janela;
                    fps = (e.quadros - quadros_janela) / dt;
                    mbps = (e.bytes_video - bytes_janela) * 8.0 / dt / 1e6;
                    quadros_janela = e.quadros;
                    bytes_janela = e.bytes_video;
                    DecodEstado d;
                    decod_ler(&d);
                    fps_decod = (d.decodificados - decod_janela) / dt;
                    decod_janela = d.decodificados;
                    if (hist_total < UI_HIST) {
                        hist_mbps[(hist_inicio + hist_total) % UI_HIST] = (float)mbps;
                        hist_total++;
                    } else {
                        hist_mbps[hist_inicio] = (float)mbps;
                        hist_inicio = (hist_inicio + 1) % UI_HIST;
                    }
                    t_janela = agora;
                    // Uma linha a cada 10 s no log, para comparar com o que a tela mostra
                    if (agora - t_log_video >= 10.0) {
                        t_log_video = agora;
                        DecodEstado d;
                        decod_ler(&d);
                        AudioEstado a;
                        audio_ler(&a);
                        LOG("main: video %.0f fps, %.1f Mbps, %lu quadros, %lu chave, %dx%d | decod %.0f/s, "
                            "%.1f ms (max %.1f), %lu erros, %lu descartados | tela %lu pulados, %lu repetidos | "
                            "audio %lu pacotes, %lu perdidos, fila %d ms, %lu faltas, %lu cortados ms, "
                            "%lu+ %lu- blocos | rede %lu buracos, %lu recuperados, %lu fora de ordem, "
                            "%lu quadros perdidos",
                            fps, mbps, e.quadros, e.quadros_chave,
                            e.sps_valido ? e.sps.largura : 0, e.sps_valido ? e.sps.altura : 0,
                            fps_decod, d.ms_medio, d.ms_maximo, d.erros, d.descartados, d.nao_exibidos,
                            d.repetidos, a.pacotes, a.perdidos, a.fila_ms, a.faltas, a.descartados_ms,
                            a.acelerados, a.desacelerados, e.rede.perdidos, e.rede.recuperados,
                            e.rede.fora_de_ordem, e.rede.quadros_perdidos);
                    }
                }
                // Sem quadro-chave não há como começar a decodificar: pede ao console
                // (a cada 2 s, no máximo 5 vezes) se ele não veio sozinho
                double desde_conexao = agora - e.t_conectado;
                if (e.quadros_chave == 0 && desde_conexao > 2.0 && agora - ultimo_pedido_chave >= 2.0 &&
                    e.pedidos_quadro_chave < 5) {
                    conexao_pedir_quadro_chave();
                    ultimo_pedido_chave = agora;
                }
                // O decodificador perdeu quadros (fila cheia ou erro): pede um
                // quadro-chave para recomeçar limpo, no máximo um por segundo
                {
                    DecodEstado d;
                    decod_ler(&d);
                    if (d.precisa_chave && agora - ultimo_pedido_chave >= 1.0) {
                        decod_limpar_pedido_chave();
                        conexao_pedir_quadro_chave();
                        ultimo_pedido_chave = agora;
                    }
                }
                // O console avisou que vai encerrar (serverInitiatedDisconnect). No
                // log de 25/11 era o usuário desligando o Xbox no meio da sessão, e a
                // reconexão automática da v0.11.3 o religou (42 s em "Provisioning").
                // v0.11.5: não reconecta sozinho; pergunta.
                if (e.servidor_encerrou) {
                    LOG("main: o Xbox encerrou a conexao (%s); sem reconexao automatica",
                        e.motivo_encerramento[0] ? e.motivo_encerramento : "sem motivo");
                    modo_ui();
                    conexao_encerrar();
                    encerrar_sessao(streaming, sessao);
                    g_reconectando = false;
                    // Log de 25/11: desligar o Xbox no meio da sessão manda este motivo
                    bool desligou = !g_na_nuvem && strstr(e.motivo_encerramento, "KickForServerShutdown");
                    if (desligou)
                        console_marcar_desligado(g_console.server_id);
                    if (g_na_nuvem)
                        mostrar_mensagem(UI_INFO, "A sessão na nuvem foi encerrada.",
                                         "O servidor fechou a sessão (tempo sem jogar, saída do jogo ou manutenção).",
                                         "A=Jogar de novo|B=Voltar para os jogos|+=Fechar");
                    else if (desligou)
                        mostrar_mensagem(UI_INFO, "O Xbox foi desligado.",
                                         "Conectar de novo liga o Xbox outra vez, se ele estiver em suspensão.",
                                         "A=Conectar de novo|B=Voltar para a lista|+=Fechar");
                    else
                        mostrar_mensagem(UI_INFO, "O Xbox encerrou a conexão.",
                                         "Ele foi desligado ou a sessão foi fechada no próprio console.\n"
                                         "Conectar de novo liga o Xbox outra vez, se estiver em suspensão.",
                                         "A=Conectar de novo|B=Voltar para a lista|+=Fechar");
                    u64 b = esperar_botao(HidNpadButton_A | HidNpadButton_B | HidNpadButton_Plus);
                    return b == HidNpadButton_A ? PASSO_SESSAO : b == HidNpadButton_B ? PASSO_LISTA : PASSO_SAIR;
                }
                // Fase 4: queda. "desconectado" pode voltar sozinho em poucos segundos
                // (oscilação do Wi-Fi); "falhou"/"fechado" ou mais de 4 s fora = reconectar.
                if (e.estado != RTC_CONNECTED) {
                    if (t_queda < 0) {
                        t_queda = agora;
                        LOG("main: conexao instavel (%s); aguardando", conexao_nome_estado(e.estado));
                    }
                    if (e.estado == RTC_FAILED || e.estado == RTC_CLOSED || agora - t_queda > 4.0) {
                        char m[96];
                        snprintf(m, sizeof m, "conexao caiu (%s ha %.1f s)", conexao_nome_estado(e.estado),
                                 agora - t_queda);
                        return cair_para_reconexao(m, false);
                    }
                } else if (t_queda >= 0) {
                    LOG("main: conexao voltou sozinha em %.1f s", agora - t_queda);
                    t_queda = -1;
                }
                // Voltou do repouso: as conexões de rede morreram no sleep; refaz já
                if (g_ultimo_congelamento > 2.0) {
                    char m[96];
                    snprintf(m, sizeof m, "voltou de %.0f s em repouso", g_ultimo_congelamento);
                    g_ultimo_congelamento = 0;
                    return cair_para_reconexao(m, true);
                }
                bool forcar = false;
                if (g_ultimo_congelamento > 0) {
                    snprintf(apos_sleep, sizeof apos_sleep, "Voltou de %.0f s suspenso", g_ultimo_congelamento);
                    g_ultimo_congelamento = 0;
                    forcar = true;
                }
                if (forcar)
                    keepalive_apos_sleep = true;
                if ((keepalive_apos_sleep || segundos_desde(ultimo_keepalive) >= SESSAO_INTERVALO_KEEPALIVE_S) &&
                    keepalive_disparar(streaming, sessao)) {
                    ultimo_keepalive = armGetSystemTick(); // o próximo conta a partir do envio
                    keepalive_marcar = keepalive_apos_sleep;
                    keepalive_apos_sleep = false;
                }
                // gsToken vencendo: renova em segundo plano (uma tentativa por minuto)
                renovacao_concluir(streaming, false);
                if (!g_ren.rodando && streaming_vencendo(streaming, RENOVAR_ANTES_S) &&
                    (g_ren.ultima_tentativa <= 0 || agora - g_ren.ultima_tentativa >= 60.0 ||
                     agora < g_ren.ultima_tentativa)) {
                    g_ren.ultima_tentativa = agora;
                    renovacao_disparar();
                }
                long st;
                if (keepalive_resultado(&st)) {
                    status_keepalive = st;
                    if (st >= 200 && st < 300) ka_ok++; else ka_falhas++;
                    if (st == 401) {
                        LOG("main: keepalive recusado (HTTP 401): acesso vencido");
                        g_forcar_renovacao = true;
                    }
                    if (keepalive_marcar) {
                        size_t n = strlen(apos_sleep);
                        snprintf(apos_sleep + n, sizeof apos_sleep - n, ": keepalive HTTP %ld", st);
                        LOG("main: %s", apos_sleep);
                        keepalive_marcar = false;
                    }
                    ultimo_quadro = -1;
                }
                break;
            }
        }

        // --- Tela (4 vezes por segundo) ----------------------------------------
        // Com o vídeo na tela, o mesmo texto vai para a camada por cima dele, e só
        // quando as estatísticas estão ligadas no menu.
        int marca = (int)(segundos_desde(inicio) * 4);
        bool em_video = video_tela_ativa();
        bool redesenhar = marca != ultimo_quadro || menu_mudou;
        if (redesenhar) {
            ultimo_quadro = marca;
            menu_mudou = false;
            if (em_video) {
                sobre_limpar(menu_aberto ? 150 : 0); // o cartão de estatísticas tem fundo próprio
                g_capturando = true;
            }
        }
        if (redesenhar && !em_video) {
            // Antes do vídeo: tela de conexão com o símbolo pulsando
            static const char *nomes[] = {"Preparando a conexão", "Aguardando o console",
                                          "Trocando endereços de rede", "Conectando", "Abrindo o vídeo"};
            char detalhe[160];
            int n = snprintf(detalhe, sizeof detalhe, "Etapa %d de 5", (int)etapa + 1);
            if (tentativa > 1)
                n += snprintf(detalhe + n, sizeof detalhe - n, " · tentativa %d de 5", tentativa);
            if (video_erro[0])
                snprintf(detalhe + n, sizeof detalhe - n, "\nErro na tela: %s", video_erro);
            mostrar_conexao(nomes[etapa], detalhe, "B=Desconectar|+=Fechar");
        }
        if (redesenhar && em_video && mostrar_estatisticas) {
            // Cartão gráfico (v0.10.6); antes era uma tela inteira de texto
            UiEstatisticas st;
            memset(&st, 0, sizeof st); // o cartão só é redesenhado se algo mudar (memcmp)
            snprintf(st.console, sizeof st.console, "%s", g_console.nome);
            st.conectado_s = (int)(segundos_desde(inicio) - e.t_conectado);
            DecodEstado d;
            decod_ler(&d);
            st.largura = d.largura ? d.largura : (e.sps_valido ? e.sps.largura : 0);
            st.altura = d.altura ? d.altura : (e.sps_valido ? e.sps.altura : 0);
            st.fps = (float)fps;
            st.fps_decod = (float)fps_decod;
            st.mbps = (float)mbps;
            st.decod_ms = (float)d.ms_medio;
            st.decod_ms_max = (float)d.ms_maximo;
            st.hardware = d.hardware;
            st.pulados = d.nao_exibidos;
            st.repetidos = d.repetidos;
            AudioEstado a;
            audio_ler(&a);
            st.audio_ativo = a.ativo;
            st.audio_fila_ms = a.fila_ms;
            st.audio_perdidos = a.perdidos;
            st.audio_faltas = a.faltas;
            st.rede_pacotes = e.rede.pacotes;
            st.rede_fora_de_ordem = e.rede.fora_de_ordem;
            st.rede_recuperados = e.rede.recuperados;
            st.rede_quadros_perdidos = e.rede.quadros_perdidos;
            st.keepalive_status = status_keepalive;
            st.keepalive_ha_s = status_keepalive < 0 ? 0 : (int)segundos_desde(ultimo_keepalive);
            st.hist_total = hist_total;
            for (int i = 0; i < hist_total; i++)
                st.hist_mbps[i] = hist_mbps[(hist_inicio + i) % UI_HIST];
            sobre_estatisticas(&st);
        }
        if (redesenhar && em_video) {
            if (menu_aberto)
                desenhar_menu(menu_item, mostrar_estatisticas, segundos_desde(inicio) - e.t_conectado);
            else if (!mostrar_estatisticas && segundos_desde(inicio) - t_video < 6.0)
                desenhar_aviso_menu();
            g_capturando = false;
        }

        // --- Vídeo na tela (fase 2c.3) ------------------------------------------
        // Assim que o primeiro quadro é decodificado, a GPU assume a tela.
        if (etapa == ET_CONECTADO && !em_video && !video_tentou) {
            DecodEstado d;
            decod_ler(&d);
            if (d.decodificados > 0) {
                video_tentou = true;
                soltar_janela(); // a interface e a GPU usam a mesma janela
                if (video_tela_iniciar()) {
                    t_video = segundos_desde(inicio);
                    LOG("main: video na tela %.1f s apos conectar", t_video - e.t_conectado);
                    ultimo_quadro = -1; // monta a camada de texto já no próximo quadro
                } else {
                    modo_ui();
                    snprintf(video_erro, sizeof video_erro, "%s", video_tela_erro());
                    LOG("main: video: %s", video_erro);
                }
            }
        }
        if (video_tela_ativa() && !g_em_foco) {
            // HOME (fase 4): o app segue rodando em segundo plano; sem desenhar,
            // mas mantendo conexão, keepalive e som. Solta os quadros e ritma o laço.
            decod_liberar_quadro(decod_pegar_quadro());
            svcSleepThread(16000000ULL);
        } else if (video_tela_ativa()) {
            long long pts = video_tela_desenhar(decod_pegar_quadro()); // espera o vsync
            if (pts >= 0 && num_metadados < INPUT_MAX_METADADOS) {
                uint32_t ms = (uint32_t)(segundos_desde(inicio) * 1000.0);
                metadados[num_metadados++] = (MetadadoQuadro){(uint32_t)pts, ms, ms, ms, ms};
            }
        }

        u64 b = quadro(&fechar);
        u64 segurados = padGetButtons(&g_controle);
        int acao = 0; // 1 = desconectar, 2 = fechar o app
        if (fechar) {
            acao = 2;
        } else if (!video_tela_ativa()) {
            if (b & HidNpadButton_Plus) acao = 2;
            else if (b & HidNpadButton_B) acao = 1;
        } else {
            // + e - segurados juntos por 1 s abrem o menu (+ sozinho não fecha mais
            // o app: na fase 3 os botões vão para o jogo)
            double agora = segundos_desde(inicio);
            bool combinacao = (segurados & HidNpadButton_Plus) && (segurados & HidNpadButton_Minus);
            // Botão Xbox: com + segurado, um clique no - (solto antes de 1 s, sem abrir o menu)
            bool mais = segurados & HidNpadButton_Plus;
            if (mais && mais_desde < 0) {
                mais_desde = agora;
                mais_em_atalho = false;
            }
            if (mais && (b & HidNpadButton_Minus)) {
                mais_em_atalho = true;
                menos_desde = agora;
            }
            if (menos_desde >= 0 && !(segurados & HidNpadButton_Minus)) {
                if (agora - menos_desde < 1.0 && !menu_aberto && !esperar_soltar) {
                    nexus_ate = agora + 0.25;
                    LOG("main: botao Xbox (+ segurado e clique no -)");
                }
                menos_desde = -1;
            }
            if (!mais && mais_desde >= 0) {
                // + solto: se foi um toque rápido sem o -, manda um toque no Menu do Xbox
                if (!mais_em_atalho && agora - mais_desde < 0.3)
                    menu_xbox_ate = agora + 0.1;
                mais_desde = -1;
            }
            if (!combinacao) {
                segurando_desde = -1;
                esperar_soltar = false;
            } else if (!menu_aberto && !esperar_soltar) {
                if (segurando_desde < 0) {
                    segurando_desde = agora;
                } else if (agora - segurando_desde >= 1.0) {
                    menu_aberto = true;
                    menu_item = 0;
                    menu_mudou = true;
                    esperar_soltar = true;
                }
            }
            if (menu_aberto && !esperar_soltar) {
                if (b & (HidNpadButton_AnyUp)) {
                    menu_item = (menu_item + MENU_ITENS - 1) % MENU_ITENS;
                    menu_mudou = true;
                } else if (b & (HidNpadButton_AnyDown)) {
                    menu_item = (menu_item + 1) % MENU_ITENS;
                    menu_mudou = true;
                } else if (b & HidNpadButton_B) {
                    menu_aberto = false;
                    menu_mudou = true;
                } else if (b & HidNpadButton_A) {
                    menu_aberto = false;
                    menu_mudou = true;
                    if (menu_item == MENU_XBOX) {
                        nexus_ate = agora + 0.25; // aperta o botão Xbox por 250 ms
                        LOG("main: botao Xbox (guia)");
                    } else if (menu_item == MENU_ESTATISTICAS) {
                        mostrar_estatisticas = !mostrar_estatisticas;
                        LOG("main: estatisticas %s", mostrar_estatisticas ? "ligadas" : "desligadas");
                    } else if (menu_item == MENU_DESCONECTAR) {
                        acao = 1;
                    } else if (menu_item == MENU_FECHAR) {
                        acao = 2;
                    }
                }
            }

            // --- Controle -> Xbox (fase 3): um pacote por quadro de tela, com os
            // metadados dos quadros exibidos, como o cliente oficial
            if (e.handshake_ok) {
                EstadoControle c;
                if (menu_aberto || esperar_soltar || !g_em_foco) {
                    memset(&c, 0, sizeof c); // menu aberto ou HOME: controle parado no Xbox
                } else {
                    controle_ler(&g_controle, &c);
                    // + só vira Menu do Xbox depois de 0,3 s sem o - (ou num toque rápido,
                    // ao soltar); com + segurado o - é do NXStream (botão Xbox / menu)
                    c.botoes &= (uint16_t)~XB_MENU;
                    if (mais_desde >= 0 && !mais_em_atalho && agora - mais_desde >= 0.3)
                        c.botoes |= XB_MENU;
                    if (agora < menu_xbox_ate)
                        c.botoes |= XB_MENU;
                    if (mais || combinacao)
                        c.botoes &= (uint16_t)~XB_VIEW;
                }
                if (agora < nexus_ate)
                    c.botoes |= XB_NEXUS;
                uint8_t pacote[INPUT_TAMANHO_MAX];
                size_t n = pacote_input_montar(pacote, ++seq_input, agora * 1000.0, metadados, num_metadados, &c);
                conexao_enviar_input(pacote, n);
                num_metadados = 0;
                pacotes_input++;
                if (c.botoes && !primeiro_botao_logado) {
                    primeiro_botao_logado = true;
                    LOG("main: primeiro botao enviado ao Xbox (0x%04x)", c.botoes);
                }
            }
        }
        if (acao) {
            modo_ui(); // solta os quadros da tela antes de fechar o decodificador
            conexao_encerrar();
            encerrar_sessao(streaming, sessao);
            return acao == 2 ? PASSO_SAIR : PASSO_LISTA;
        }
    }
}

// "WNSError ... State WaitingForServerToRegister": o serviço da Microsoft não
// consegue chamar o console. Nos logs de 22/11 e 25/11 o Xbox estava DESLIGADO
// (modo de economia de energia), não em repouso, embora a lista dissesse
// "ConnectedStandby". Assim ele não acorda de longe; a v0.11.1 esperava e tentava
// por 1 min, sem efeito. v0.11.2: avisa na hora o que fazer.
#define DETALHE_XBOX_DESLIGADO \
    "Ele nao esta em repouso, entao nao da para liga-lo de longe. Ligue o console pelo botao e aperte A.\n" \
    "Para o NXStream ligar o Xbox sozinho: Configuracoes > Geral > Opcoes de energia > Suspensao."

static void mostrar_espera_ligar(int segundos) {
    char detalhe[256];
    snprintf(detalhe, sizeof detalhe,
             "Ligue o console pelo botão: o NXStream conecta sozinho quando ele ligar (%d s).\n"
             "Para ligar de longe: Configurações > Geral > Opções de energia > Suspensão.",
             segundos);
    mostrar_conexao("O Xbox está desligado", detalhe, "B=Voltar para a lista|R=Jogos na nuvem|+=Fechar");
}

// v0.12.4: em vez do erro, espera o Xbox ser ligado pelo botão. A Microsoft
// pode dizer "ConnectedStandby" com o console desligado (estado velho), mas
// "On" só aparece quando ele está ligado de verdade.
// v0.12.5: lista a cada 2 s (era 5); o usuário mediu ~10 s entre o Xbox
// terminar de ligar e a conexão.
// v0.12.6: sem as "sondas" da v0.12.5 (tentar a sessão a cada 8 s). Log de
// 25/11: uma sonda feita enquanto o Xbox ligava foi aceita, mas ficou 90 s em
// Provisioning e falhou ("ServerNeverRegistered"); a lista já dizia On muito
// antes. A sessão só é aberta quando a lista mostra On.
#define ESPERA_LIGAR_INTERVALO_S 2.0
#define ESPERA_LIGAR_MAX_S       900  // 15 min: depois disso, a tela de erro
static void espera_terminar(void) {
    g_espera_desde = -1;
}

static Passo esperar_xbox_ligar(AuthStreaming *streaming) {
    if (g_espera_desde < 0) {
        g_espera_desde = segundos_desde(0);
        LOG("main: esperando o Xbox ser ligado (lista a cada %.0f s)", ESPERA_LIGAR_INTERVALO_S);
        console_marcar_desligado(g_console.server_id);
    }
    u64 inicio = armGetSystemTick();
    double proxima = ESPERA_LIGAR_INTERVALO_S;
    int ultimo = -1;
    char estado[32] = "";
    bool fechar = false;
    for (;;) {
        double total = segundos_desde(0) - g_espera_desde;
        double aqui = segundos_desde(inicio);
        if (total > ESPERA_LIGAR_MAX_S) {
            LOG("main: o Xbox nao foi ligado em %d s", ESPERA_LIGAR_MAX_S);
            espera_terminar();
            return erro_sessao("O Xbox esta desligado.", DETALHE_XBOX_DESLIGADO);
        }
        if (aqui >= proxima) {
            proxima = aqui + ESPERA_LIGAR_INTERVALO_S;
            XConsoles cs;
            if (chamar((Chamada){.tipo = CH_LISTAR, .streaming = streaming, .saida = &cs})) {
                for (int i = 0; i < cs.total; i++)
                    if (!strcmp(cs.itens[i].server_id, g_console.server_id))
                        snprintf(estado, sizeof estado, "%s", cs.itens[i].estado);
                if (!strcmp(estado, "On")) {
                    LOG("main: o Xbox ligou (%.0f s de espera); conectando", segundos_desde(0) - g_espera_desde);
                    espera_terminar();
                    console_marcar_desligado(NULL);
                    return PASSO_SESSAO;
                }
            }
            ultimo = -1; // redesenha (a consulta pode ter levado um tempo)
        }
        if ((int)total != ultimo) {
            ultimo = (int)total;
            mostrar_espera_ligar((int)total);
        }
        u64 b = quadro(&fechar);
        if (fechar || (b & HidNpadButton_Plus)) {
            espera_terminar();
            return PASSO_SAIR;
        }
        if (b & HidNpadButton_B) {
            espera_terminar();
            return PASSO_LISTA;
        }
        if (b & HidNpadButton_R) {
            espera_terminar();
            return PASSO_NUVEM;
        }
    }
}

#define NUVEM_TEMPO_MAX_PREPARO_S 900 // 15 min: fila em horário de pico

static const char *estado_nuvem_texto(XSessaoEstado e) {
    switch (e) {
        case XSESSAO_PREPARANDO:    return "Preparando o jogo na nuvem";
        case XSESSAO_AGUARDA_LOGIN: return "Autorizando o acesso";
        case XSESSAO_NA_FILA:       return "Na fila para jogar";
        default:                    return xapi_sessao_estado_texto(e);
    }
}

// Uma tentativa de abrir a sessão e esperar ela ficar pronta. *pronta = true se
// deu certo; senão retorna o passo escolhido na tela de erro (PASSO_SESSAO = "A,
// tentar de novo"), ou lista/sair/reconectar.
static Passo abrir_sessao_uma(AuthConta *conta, AuthStreaming *streaming, XSessao *sessao_saida, double *preparo,
                              bool *pronta) {
    *pronta = false;
    mostrar_conexao("Abrindo a sessão", NULL, NULL);
    mostrar_agora();

    XSessao sessao;
    if (!chamar((Chamada){.tipo = CH_SESSAO_INICIAR, .streaming = streaming,
                          .texto = g_na_nuvem ? "" : g_console.server_id,
                          .texto2 = g_na_nuvem ? g_jogo.titulo_id : NULL, .sessao = &sessao}))
        return erro_sessao("Nao foi possivel abrir a sessao.", xapi_ultimo_erro());
    // Nuvem: a fila (WaitingForResources) e a preparação de um servidor podem
    // levar minutos; o console responde em segundos
    const int tempo_max = g_na_nuvem ? NUVEM_TEMPO_MAX_PREPARO_S : SESSAO_TEMPO_MAX_PREPARO_S;
    double proxima_lista = 0, t_ligado = -1; // sessão presa (v0.12.6)
    int espera_estimada = -1; // nuvem, na fila: estimativa do servidor (segundos)
    bool espera_consultada = false;

    // --- 1. Preparação: consulta o estado até ficar pronta -----------------
    u64 inicio = armGetSystemTick();
    double proxima = 0;
    int tentativas_connect = 0, erros_seguidos = 0, ultimo_segundo = -1;
    XSessaoEstado estado = XSESSAO_PREPARANDO;
    char detalhe[160] = "";
    bool fechar = false;

    while (estado != XSESSAO_PRONTA) {
        double passou = segundos_desde(inicio);

        if (passou > tempo_max) {
            LOG("main: sessao nao ficou pronta em %d s", tempo_max);
            encerrar_sessao(streaming, &sessao);
            if (g_na_nuvem)
                return erro_sessao("A nuvem demorou demais para preparar o jogo.",
                                   "A fila ou a preparacao passou do limite. Tente de novo em alguns minutos.");
            return erro_sessao("O console demorou demais para responder.",
                               "Confira se ele esta ligado ou em repouso e conectado a internet.");
        }

        if (passou >= proxima) {
            // Intervalo contado do início da consulta (antes contava do fim e,
            // com ~1 s por requisição, a consulta saía a cada 2 s)
            proxima = passou + SESSAO_INTERVALO_ESTADO_S;
            estado = (XSessaoEstado)chamar((Chamada){.tipo = CH_SESSAO_ESTADO, .streaming = streaming,
                                                     .sessao = &sessao, .detalhe = detalhe, .tam = sizeof detalhe});
            LOG("main: sessao: %s (%.0f s)", detalhe[0] ? detalhe : xapi_sessao_estado_texto(estado), passou);

            // v0.12.6: sessão aberta enquanto o Xbox ainda ligava fica presa em
            // Provisioning até a Microsoft desistir (90 s, "ServerNeverRegistered").
            // Depois de 15 s preparando, consulta a lista; se o console já está
            // On e a sessão continua preparando 10 s depois, abre outra (aberta
            // com o Xbox ligado, ela fica pronta em ~4 s).
            if (!g_na_nuvem && estado == XSESSAO_PREPARANDO && passou >= 15 && passou >= proxima_lista) {
                proxima_lista = passou + 3;
                XConsoles cs;
                if (t_ligado < 0 && chamar((Chamada){.tipo = CH_LISTAR, .streaming = streaming, .saida = &cs}))
                    for (int i = 0; i < cs.total; i++)
                        if (!strcmp(cs.itens[i].server_id, g_console.server_id) && !strcmp(cs.itens[i].estado, "On")) {
                            t_ligado = passou;
                            LOG("main: o Xbox esta ligado e a sessao ainda preparando (%.0f s)", passou);
                        }
                if (t_ligado >= 0 && passou - t_ligado >= 10) {
                    LOG("main: sessao presa (aberta com o Xbox ainda ligando); abrindo outra");
                    encerrar_sessao(streaming, &sessao);
                    return PASSO_SESSAO; // abrir_sessao tenta de novo
                }
            }

            if (estado == XSESSAO_ERRO) {
                if (++erros_seguidos >= 5) {
                    encerrar_sessao(streaming, &sessao);
                    return erro_sessao("Perdi contato com o servico de streaming.", xapi_ultimo_erro());
                }
            } else {
                erros_seguidos = 0;
            }

            if (estado == XSESSAO_FALHOU) {
                encerrar_sessao(streaming, &sessao);
                // Também "XboxPowerOff" (log de 25/11, 22:26: desligado pelo menu
                // do Xbox, a Microsoft sabe e a lista diz "Off")
                if (strstr(detalhe, "WaitingForServerToRegister") || strstr(detalhe, "XboxPowerOff")) {
                    LOG("main: o Xbox nao respondeu ao chamado (desligado ou sem internet)");
                    if (g_reconectando) { // na reconexão, a falha vira nova tentativa (como antes)
                        console_marcar_desligado(g_console.server_id);
                        return erro_sessao("O Xbox esta desligado.", DETALHE_XBOX_DESLIGADO);
                    }
                    return esperar_xbox_ligar(streaming);
                }
                return erro_sessao(g_na_nuvem ? "A nuvem recusou a sessao." : "O console recusou a sessao.",
                                   detalhe);
            }

            if (estado == XSESSAO_NA_FILA && g_na_nuvem && !espera_consultada) {
                espera_consultada = true;
                espera_estimada = (int)chamar((Chamada){.tipo = CH_TEMPO_ESPERA, .streaming = streaming,
                                                        .texto = g_jogo.titulo_id});
                LOG("main: na fila da nuvem; espera estimada %d s", espera_estimada);
            }

            if (estado == XSESSAO_AGUARDA_LOGIN) {
                if (tentativas_connect >= SESSAO_MAX_CONNECT) {
                    encerrar_sessao(streaming, &sessao);
                    return erro_sessao("O console nao aceitou o login.",
                                       "Confira se e a mesma conta configurada no Xbox.");
                }
                tentativas_connect++;
                char *token = NULL;
                if (chamar((Chamada){.tipo = CH_TOKEN_CONSOLE, .conta = conta, .saida = &token}) != AUTH_OK) {
                    encerrar_sessao(streaming, &sessao);
                    return erro_sessao("Nao foi possivel autorizar o acesso ao console.", auth_ultimo_erro());
                }
                bool ok = chamar((Chamada){.tipo = CH_SESSAO_CONECTAR, .streaming = streaming, .sessao = &sessao,
                                           .texto = token});
                free(token);
                if (!ok) {
                    encerrar_sessao(streaming, &sessao);
                    return erro_sessao("O console recusou o login.", xapi_ultimo_erro());
                }
            }
        }

        if ((int)passou != ultimo_segundo) {
            ultimo_segundo = (int)passou;
            char detalhe[200];
            if (g_na_nuvem) {
                char fila[64] = "";
                if (estado == XSESSAO_NA_FILA && espera_estimada > 0)
                    snprintf(fila, sizeof fila, "\nEspera estimada: %d min", (espera_estimada + 59) / 60);
                snprintf(detalhe, sizeof detalhe, "Xbox Cloud Gaming · %d s%s", (int)passou, fila);
            } else {
                snprintf(detalhe, sizeof detalhe, "%s · %d s%s", xapi_tipo_amigavel(g_console.tipo), (int)passou,
                         estado == XSESSAO_PREPARANDO
                             ? "\nSe o console estava em repouso, ele acorda sem ligar a TV."
                             : "");
            }
            mostrar_conexao(g_na_nuvem ? estado_nuvem_texto(estado) : xapi_sessao_estado_texto(estado), detalhe,
                            "B=Cancelar");
        }

        u64 b = quadro(&fechar);
        if (fechar) {
            xapi_sessao_encerrar(streaming, &sessao);
            return PASSO_SAIR;
        }
        if (b & HidNpadButton_B) {
            encerrar_sessao(streaming, &sessao);
            return PASSO_LISTA;
        }
    }

    *preparo = segundos_desde(inicio);
    *sessao_saida = sessao;
    LOG("main: sessao pronta em %.1f s", *preparo);
    if (!g_na_nuvem)
        console_marcar_desligado(NULL);
    *pronta = true;
    return PASSO_SESSAO;
}

// Abre a sessão. Retorna PASSO_SESSAO só se ficou pronta (ou o passo seguinte:
// lista, sair, reconectar). v0.11.2: antes, "A = tentar de novo" na tela de erro
// também voltava PASSO_SESSAO e quem chamava seguia para a conexão com uma sessão
// que tinha falhado ("URL using bad/illegal format", logs de 22/11 e 25/11).
static Passo abrir_sessao(AuthConta *conta, AuthStreaming *streaming, XSessao *sessao_saida, double *preparo) {
    for (;;) {
        bool pronta = false;
        Passo p = abrir_sessao_uma(conta, streaming, sessao_saida, preparo, &pronta);
        if (pronta || p != PASSO_SESSAO)
            return p;
        LOG("main: tentando abrir a sessao de novo");
        garantir_streaming(conta, streaming); // renova antes, se a falha foi 401
    }
}

// Espera antes de uma nova tentativa, mostrando o que está acontecendo.
// Retorna PASSO_RECONECTAR para seguir, ou PASSO_LISTA/PASSO_SAIR.
#define RECONEXAO_TENTATIVAS 5
static Passo esperar_reconexao(int tentativa, double espera) {
    u64 inicio = armGetSystemTick();
    bool fechar = false;
    int ultimo = -1;
    for (;;) {
        double falta = espera - segundos_desde(inicio);
        if (falta <= 0)
            return PASSO_RECONECTAR;
        int marca = (int)(falta * 4);
        if (marca != ultimo) {
            ultimo = marca;
            char status[64], detalhe[64];
            snprintf(status, sizeof status, "Nova tentativa em %.0f s", falta + 0.49);
            snprintf(detalhe, sizeof detalhe, "Tentativa %d de %d", tentativa, RECONEXAO_TENTATIVAS);
            g_reconectando = true;
            mostrar_conexao(status, detalhe, "B=Voltar para a lista");
        }
        u64 b = quadro(&fechar);
        if (fechar)
            return PASSO_SAIR;
        if (b & HidNpadButton_B)
            return PASSO_LISTA;
    }
}

// Sessão + conexão, com reconexão automática (fase 4).
//
// v0.9.2: a reconexão abre sempre uma sessão nova. No log da v0.9.1, refazer só a
// conexão WebRTC na sessão antiga nunca funcionou (4 de 4 quedas): a 1ª oferta
// recebe "AgentNotListening" e as seguintes "SessionInUnexpectedState"; cada
// queda gastava ~25 s em 5 tentativas inúteis antes de abrir a sessão nova, que
// fica pronta em 2-3 s com o console já acordado.
// Encerramento da sessão antiga em segundo plano (v0.9.3): no log da v0.9.2 ele
// levou de 1 a 7 s, e a sessão nova só era pedida depois. Agora as duas coisas
// correm juntas. A thread usa uma cópia do token (o principal pode renová-lo).
static struct {
    pthread_t thread;
    bool rodando;
    AuthStreaming s;
    XSessao sessao;
} g_enc;

static void *thread_encerrar(void *arg) {
    (void)arg;
    xapi_sessao_encerrar(&g_enc.s, &g_enc.sessao);
    return NULL;
}

static void encerramento_esperar(void) {
    if (g_enc.rodando) {
        pthread_join(g_enc.thread, NULL);
        g_enc.rodando = false;
    }
    free(g_enc.s.gs_token);
    g_enc.s.gs_token = NULL;
}

static void encerrar_em_segundo_plano(const AuthStreaming *s, const XSessao *sessao) {
    encerramento_esperar();
    g_enc.s = *s;
    g_enc.s.gs_token = strdup(s->gs_token ? s->gs_token : "");
    g_enc.sessao = *sessao;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 256 * 1024); // curl + TLS
    g_enc.rodando = g_enc.s.gs_token && pthread_create(&g_enc.thread, &at, thread_encerrar, NULL) == 0;
    pthread_attr_destroy(&at);
    if (!g_enc.rodando) { // sem thread: encerra na hora, como antes
        xapi_sessao_encerrar(s, sessao);
        encerramento_esperar();
    }
}

static Passo passo_sessao(AuthConta *conta, AuthStreaming *streaming) {
    XSessao sessao;
    double tempo_preparo = 0;
    g_conta = conta;
    memset(&g_ren, 0, sizeof g_ren);
    garantir_streaming(conta, streaming);
    Passo p = abrir_sessao(conta, streaming, &sessao, &tempo_preparo);
    if (p != PASSO_SESSAO)
        return p;

    int tentativa = 0;
    for (;;) {
        g_reconectando = tentativa > 0;
        g_chegou_a_conectar = false;
        p = passo_conectar(streaming, &sessao, tempo_preparo, tentativa);
        // nada de thread mexendo na conta daqui em diante; a espera anima a tela
        if (g_ren.rodando && p == PASSO_RECONECTAR) {
            mostrar_conexao("Renovando o acesso ao Xbox Live", NULL, NULL);
            bool fechar = false;
            while (!__atomic_load_n(&g_ren.pronto, __ATOMIC_ACQUIRE) && !fechar)
                quadro(&fechar);
        }
        renovacao_concluir(streaming, true);
        g_ren.ultima_tentativa = 0;
        if (p != PASSO_RECONECTAR) {
            g_reconectando = false;
            return p;
        }
        if (g_chegou_a_conectar) // estava funcionando: recomeça a contagem
            tentativa = 0;

        // Fecha a sessão antiga (não aceita outra conexão) e abre outra. O token
        // é renovado antes se tiver vencido (senão o próprio encerramento dá 401).
        g_tick_queda = armGetSystemTick();
        {   // keepalive de antes do repouso ainda em voo: espera animando a tela
            bool fechar = false;
            while (g_ka.rodando && !__atomic_load_n(&g_ka.pronto, __ATOMIC_ACQUIRE) && !fechar)
                quadro(&fechar);
        }
        keepalive_esperar();
        garantir_streaming(conta, streaming);
        encerrar_em_segundo_plano(streaming, &sessao);
        for (;;) {
            tentativa++;
            if (tentativa > RECONEXAO_TENTATIVAS) {
                g_reconectando = false;
                return erro_sessao("Nao foi possivel reconectar ao console.",
                                   "Confira o Wi-Fi do Switch e se o Xbox continua ligado ou em repouso.");
            }
            // 1ª tentativa na hora; depois 1, 2, 4, 8 s
            if (tentativa > 1) {
                double espera = (double)(1 << (tentativa - 2));
                if (espera > 8) espera = 8;
                p = esperar_reconexao(tentativa, espera);
                if (p != PASSO_RECONECTAR) {
                    g_reconectando = false;
                    return p;
                }
            }
            g_reconectando = true;
            garantir_streaming(conta, streaming);
            p = abrir_sessao(conta, streaming, &sessao, &tempo_preparo);
            if (p == PASSO_SESSAO)
                break;  // sessão pronta: conecta
            if (p != PASSO_RECONECTAR) {
                g_reconectando = false;
                return p;
            }
        }
        LOG("main: sessao nova pronta para a reconexao (tentativa %d)", tentativa);
    }
}

// ---------------------------------------------------------------------------
// Passo: diagnóstico WebRTC (fase 2b, parte 1) - sem o Xbox
// ---------------------------------------------------------------------------
#define DIAG_TEMPO_LIMITE_S 30

static const char *sim_nao(bool v) {
    return v ? COR_OK "sim" COR_FIM : COR_ERRO "nao" COR_FIM;
}

static Passo passo_diag(void) {
    mostrar_aguarde("Iniciando o diagnostico WebRTC...");
    rtc_diag_iniciar();
    g_tick_anterior = armGetSystemTick(); // o início pode demorar; não é suspensão

    int ultimo_quadro = -1;
    bool fechar = false;
    for (;;) {
        RtcDiagEstado e;
        rtc_diag_ler(&e);

        int marca = (int)(e.segundos * 4); // redesenha 4 vezes por segundo
        if (marca != ultimo_quadro) {
            ultimo_quadro = marca;
            cabecalho();
            printf(COR_TITULO "Diagnostico WebRTC" COR_FIM " (sem o Xbox)\n\n");
            printf(COR_FRACA "Monta uma conexao WebRTC local com video, audio e canal de\n"
                   "dados, para testar a biblioteca no Switch." COR_FIM "\n\n");

            printf("1. Certificado e oferta SDP: ");
            if (e.oferta_pronta)
                printf(COR_OK "ok" COR_FIM " em %.2f s (%d bytes)\n", e.t_oferta, e.tamanho_oferta);
            else
                printf("gerando... %.0f s\n", e.segundos);
            if (e.oferta_pronta)
                printf("   certificado DTLS: %s   video: %s   audio: %s   dados: %s\n",
                       sim_nao(e.tem_fingerprint), sim_nao(e.tem_video),
                       sim_nao(e.tem_audio), sim_nao(e.tem_dados));
            printf("\n");

            printf("2. Candidatos de rede: %d", e.num_candidatos);
            if (e.coleta_completa)
                printf("  (coleta " COR_OK "completa" COR_FIM " em %.2f s)\n", e.t_coleta);
            else
                printf("  (coletando...)\n");
            for (int i = 0; i < e.num_candidatos; i++)
                printf("   - %s\n", e.candidatos[i]);
            printf("\n");

            bool tudo_ok = e.oferta_pronta && e.tem_fingerprint && e.tem_video && e.tem_audio &&
                           e.tem_dados && e.coleta_completa && e.num_candidatos > 0 && !e.erro[0];
            if (e.erro[0])
                printf(COR_ERRO "Erro: %s" COR_FIM "\n\n", e.erro);
            if (tudo_ok)
                printf(COR_OK "Resultado: tudo funcionando no Switch!" COR_FIM "\n\n");
            else if (e.coleta_completa && e.num_candidatos == 0)
                printf(COR_ERRO "Resultado: nenhum candidato de rede (o Wi-Fi esta conectado?)" COR_FIM "\n\n");
            else if (e.segundos > DIAG_TEMPO_LIMITE_S && !e.oferta_pronta)
                printf(COR_ERRO "Resultado: a biblioteca nao respondeu em %d s" COR_FIM "\n\n",
                       DIAG_TEMPO_LIMITE_S);

            printf("A = repetir   B = voltar\n");
        }

        u64 b = quadro(&fechar);
        if (fechar) {
            rtc_diag_encerrar();
            return PASSO_SAIR;
        }
        if (b & HidNpadButton_B) {
            rtc_diag_encerrar();
            return PASSO_LISTA;
        }
        if (b & HidNpadButton_A) {
            mostrar_aguarde("Repetindo o diagnostico...");
            rtc_diag_iniciar();
            g_tick_anterior = armGetSystemTick();
            ultimo_quadro = -1;
        }
    }
}

// ---------------------------------------------------------------------------
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&g_controle);
    g_tick_anterior = armGetSystemTick();

    // Rede com mais memória que o padrão: todos os sockets do app (HTTPS do curl,
    // UDP do WebRTC, pipe da compat) dividem um poço de buffers. Com o padrão
    // (~2,25 MB) o socket da libjuice esgotava o poço e o HTTPS falhava durante
    // a conexão com o console. Mais sessões BSD evitam que chamadas bloqueantes
    // de threads diferentes (poll da libjuice, connect do curl) esperem umas pelas outras.
    SocketInitConfig rede = *socketGetDefaultInitConfig();
    rede.sb_efficiency = 8;      // padrão 4: dobra o poço de buffers
    rede.num_bsd_sessions = 6;   // padrão 3
    Result rc_rede = socketInitialize(&rede);
    bool nxlink = R_SUCCEEDED(rc_rede) && nxlinkStdioForDebug() >= 0;

    mkdir(NX_PASTA_APP, 0777);
    log_iniciar(NX_ARQ_LOG, nxlink);
    LOG("main: versao %s, nxlink %s", APP_VERSION, nxlink ? "ativo" : "inativo");
    terminar_instalar();

    AppletHookCookie cookie;
    appletHook(&cookie, ao_evento_do_sistema, NULL);
    // Fase 4: HOME não pausa o app (o stream continua); o repouso pausa sempre.
    appletSetFocusHandlingMode(AppletFocusHandlingMode_NoSuspend);

    http_definir_ao_terminar(ao_terminar_http);

    if (appletGetAppletType() != AppletType_Application) {
        mostrar_mensagem(UI_ERRO, "O NXStream precisa ser aberto em title mode.",
                         "Segure R ao abrir um jogo e escolha o NXStream no menu.", "+=Fechar");
        LOG("main: aberto em applet mode, encerrando");
        esperar_botao(HidNpadButton_Plus);
    } else if (R_FAILED(rc_rede) || !http_iniciar()) {
        char det[64];
        snprintf(det, sizeof det, "Código 0x%x. Confira se o Wi-Fi está ligado.", rc_rede);
        mostrar_mensagem(UI_ERRO, "Falha ao iniciar a rede.", det, "+=Fechar");
        esperar_botao(HidNpadButton_Plus);
    } else {
        AuthConta conta = {0};
        AuthStreaming streaming = {0};
        bool recem_logada = false;
        const char *aviso_login = NULL;
        Passo passo = PASSO_INICIO;

        while (passo != PASSO_SAIR) {
            switch (passo) {
                case PASSO_INICIO:
                    conta.refresh_token = auth_carregar_refresh(NX_ARQ_TOKEN);
                    LOG("main: token salvo %s", conta.refresh_token ? "encontrado" : "nao encontrado");
                    if (conta.refresh_token) {
                        conta_ler();
                        ui_definir_conta(g_gamertag);
                    }
                    passo = conta.refresh_token ? PASSO_STREAMING : PASSO_LOGIN;
                    break;
                case PASSO_LOGIN:
                    conta_esquecer(); // outra conta pode entrar
                    ui_definir_conta(NULL);
                    passo = passo_login(&conta, aviso_login);
                    aviso_login = NULL;
                    recem_logada = (passo == PASSO_STREAMING);
                    break;
                case PASSO_STREAMING:
                    auth_streaming_liberar(&streaming);
                    snprintf(streaming.gamertag, sizeof streaming.gamertag, "%s", g_gamertag);
                    passo = passo_streaming(&conta, &streaming, recem_logada, &aviso_login);
                    if (streaming.gamertag[0] && strcmp(g_gamertag, streaming.gamertag) != 0) {
                        snprintf(g_gamertag, sizeof g_gamertag, "%s", streaming.gamertag);
                        conta_salvar();
                    }
                    ui_definir_conta(g_gamertag);
                    recem_logada = false;
                    g_forcar_renovacao = false;
                    break;
                case PASSO_LISTA:
                    passo = passo_lista(&conta, &streaming);
                    break;
                case PASSO_SESSAO:
                    passo = passo_sessao(&conta, g_na_nuvem ? &g_nuvem : &streaming);
                    if (g_na_nuvem && passo == PASSO_LISTA)
                        passo = PASSO_NUVEM; // da nuvem, "voltar" é para a lista de jogos
                    break;
                case PASSO_DIAG:
                    passo = passo_diag();
                    break;
                case PASSO_NUVEM:
                    g_na_nuvem = false;
                    passo = passo_nuvem(&conta);
                    break;
                default:
                    passo = PASSO_SAIR;
                    break;
            }
        }

        // Cada passo vai para o log: os logs da v0.3.0 e v0.4.1 terminavam antes
        // de "encerrado" mesmo com o app fechando sem erro na tela.
        LOG("main: encerrando (1/4: liberando a conta)");
        nuvem_esquecer();
        auth_streaming_liberar(&streaming);
        auth_conta_liberar(&conta);
        LOG("main: encerrando (2/4: fechando conexoes WebRTC)");
        conexao_encerrar();
        rtc_diag_finalizar_biblioteca();
        LOG("main: encerrando (3/4: liberando a libdatachannel)");
        rtc_comum_finalizar();
        capas_encerrar(); // threads das capas usam o HTTPS
        LOG("main: encerrando (4/4: finalizando o HTTPS)");
        encerramento_esperar();
        http_encerrar();
    }

    appletUnhook(&cookie);
    log_fechar();
    socketExit();
    soltar_janela();
    return 0;
}
