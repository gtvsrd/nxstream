// NXStream - login, lista de consoles, conexão com o console e vídeo na tela
//
// Interface gráfica simples (ui/tela, v0.10.0) inspirada no app do Xbox; o
// console de texto do libnx fica para o diagnóstico. Com o vídeo na tela, o
// texto (estatísticas, menu) vai para uma camada por cima dele. Fluxo:
//   INICIO -> (sem token salvo) LOGIN -> STREAMING -> LISTA
//   INICIO -> (com token salvo) STREAMING -> LISTA
//   LISTA -> (A no console) SESSAO -> LISTA
// Nenhum erro fecha o app: sempre há "tentar de novo" ou "sair da conta".

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <switch.h>
#include <rtc/rtc.h> // estados da conexão (RTC_CONNECTED...) para a reconexão

#include "api/config.h"
#include "api/xapi.h"
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
#include "net/rtc_comum.h"
#include "net/rtc_diag.h"

#define COR_TITULO  "\x1b[36;1m"
#define COR_DESTAQ  "\x1b[33;1m"
#define COR_OK      "\x1b[32;1m"
#define COR_ERRO    "\x1b[31;1m"
#define COR_FRACA   "\x1b[37m"
#define COR_FIM     "\x1b[0m"

typedef enum { PASSO_INICIO, PASSO_LOGIN, PASSO_STREAMING, PASSO_LISTA, PASSO_SESSAO, PASSO_DIAG, PASSO_SAIR,
               PASSO_RECONECTAR } Passo; // PASSO_RECONECTAR: só entre passo_conectar e passo_sessao

static PadState g_controle;
static u64 g_tick_anterior;
static double g_ultimo_congelamento = 0; // segundos suspenso no último sleep/HOME (0 = nada novo)
static bool g_retorno_pendente = false;   // sistema avisou que o app voltou a rodar
static bool g_em_foco = true;             // último estado de foco conhecido
static XConsole g_console;               // console escolhido na lista
static bool g_auto_conexao_feita = false; // já passou pela lista uma vez (conexão automática só na 1ª)
static bool g_reconectando = false;      // fase 4: falhas na conexão viram nova tentativa, não tela de erro
static bool g_chegou_a_conectar = false; // esta tentativa chegou a conectar (zera a contagem de tentativas)
static time_t g_streaming_obtido = 0;    // quando o gsToken atual foi obtido (vale ~4 h)
static bool g_forcar_renovacao = false;  // o servidor respondeu 401: renovar o gsToken antes da próxima chamada
static u64 g_tick_queda = 0;            // quando a conexão caiu (para medir a reconexão inteira)
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
        AuthStatus st = auth_renovar(conta);
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
    AuthStatus st = auth_streaming(conta, streaming);
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

static Passo passo_lista(AuthConta *conta, AuthStreaming *streaming) {
    // gsToken vencendo (dura ~4 h): obtém outro antes de usar
    if (streaming->duracao_s > 0 && time(NULL) - g_streaming_obtido > streaming->duracao_s - 120)
        return PASSO_STREAMING;

    mostrar_aguarde("Procurando seus consoles...");
    XConsoles consoles;
    if (!xapi_listar_consoles(streaming, &consoles)) {
        u64 b = mostrar_erro("Não foi possível listar seus consoles.", xapi_ultimo_erro(), true);
        if (b == HidNpadButton_X && confirmar_sair_da_conta()) {
            auth_apagar(NX_ARQ_TOKEN);
            auth_conta_liberar(conta);
            return PASSO_LOGIN;
        }
        return b == HidNpadButton_Plus || b == 0 ? PASSO_SAIR : PASSO_LISTA;
    }

    // Só um console na conta: na primeira vez depois do login/abertura do app,
    // conecta sozinho (2 s para desistir com B). Depois de desconectar, a lista
    // fica parada como antes.
    if (!g_auto_conexao_feita && consoles.total == 1) {
        g_auto_conexao_feita = true;
        const XConsole *x = &consoles.itens[0];
        u64 inicio = armGetSystemTick();
        bool fechar = false;
        int ultimo = -1;
        for (;;) {
            double falta = 2.0 - segundos_desde(inicio);
            if (falta <= 0) {
                g_console = *x;
                LOG("main: console escolhido automaticamente (unico na conta): %s (%s)", g_console.nome,
                    g_console.estado);
                return PASSO_SESSAO;
            }
            if ((int)(falta * 4) != ultimo) {
                ultimo = (int)(falta * 4);
                char titulo[96], status[64], detalhe[160];
                snprintf(titulo, sizeof titulo, "Conectando a %s", x->nome);
                snprintf(status, sizeof status, "Em %.0f s", falta + 0.49);
                snprintf(detalhe, sizeof detalhe, "%s · %s\nÉ o único console da conta: a conexão é automática.",
                         xapi_tipo_amigavel(x->tipo), xapi_estado_amigavel(x->estado));
                modo_ui();
                if (ui_ativa()) {
                    ui_status(titulo, status, detalhe, "A=Conectar agora|B=Ficar na lista");
                } else {
                    cabecalho();
                    printf("%s %s\n\nA = conectar agora   B = ficar na lista\n", titulo, status);
                }
            }
            u64 b = quadro(&fechar);
            if (fechar)
                return PASSO_SAIR;
            if (b & HidNpadButton_A)
                inicio = armGetSystemTick() - armNsToTicks(3000000000ULL); // já
            if (b & HidNpadButton_B)
                break;
        }
    }
    g_auto_conexao_feita = true;

    int sel = 0;
    const char *mensagem = NULL;
    for (;;) {
        char conta_txt[96];
        snprintf(conta_txt, sizeof conta_txt, "Conta: %s", streaming->gamertag[0] ? streaming->gamertag : "conectada");
        UiItem itens[UI_MAX_ITENS];
        int total = consoles.total < UI_MAX_ITENS ? consoles.total : UI_MAX_ITENS;
        for (int i = 0; i < total; i++) {
            const XConsole *x = &consoles.itens[i];
            snprintf(itens[i].nome, sizeof itens[i].nome, "%s", x->nome);
            snprintf(itens[i].detalhe, sizeof itens[i].detalhe, "%s · %s", xapi_tipo_amigavel(x->tipo),
                     xapi_estado_amigavel(x->estado));
            itens[i].estado = !strcmp(x->estado, "On") ? 1 : !strcmp(x->estado, "ConnectedStandby") ? 2 : 0;
        }
        const char *vazia = "Nenhum console encontrado nesta conta.\nNo Xbox, ative em: Configurações > "
                            "Dispositivos e conexões > Recursos remotos > Habilitar recursos remotos.";
        modo_ui();
        if (ui_ativa()) {
            ui_lista("Seus consoles", conta_txt, itens, total, sel, total ? mensagem : vazia,
                     total ? "A=Conectar|Y=Atualizar|X=Sair da conta|-=Diagnóstico|+=Fechar"
                           : "Y=Atualizar|X=Sair da conta|-=Diagnóstico|+=Fechar");
        } else {
            cabecalho();
            printf("%s\n\n", conta_txt);
            for (int i = 0; i < total; i++)
                printf("%s %s (%s)\n" COR_FIM, i == sel ? COR_DESTAQ " >" : "  ", itens[i].nome, itens[i].detalhe);
            printf("\n%s\nA = conectar   Y = atualizar   X = sair da conta   - = diagnostico   + = fechar\n",
                   total ? (mensagem ? mensagem : "") : vazia);
        }

        u64 b = esperar_botao(HidNpadButton_A | HidNpadButton_Y | HidNpadButton_X | HidNpadButton_Plus |
                              HidNpadButton_Minus | HidNpadButton_AnyUp | HidNpadButton_AnyDown);
        mensagem = NULL;
        if (b == 0 || (b & HidNpadButton_Plus))
            return PASSO_SAIR;
        if (b & HidNpadButton_Y)
            return PASSO_LISTA;
        if (b & HidNpadButton_Minus)
            return PASSO_DIAG;
        if ((b & HidNpadButton_X) && confirmar_sair_da_conta()) {
            auth_apagar(NX_ARQ_TOKEN);
            auth_conta_liberar(conta);
            LOG("main: usuario saiu da conta");
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
static Passo erro_sessao(const char *titulo, const char *detalhe) {
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
    return s->duracao_s > 0 && time(NULL) - g_streaming_obtido > s->duracao_s - margem_s;
}

// Renova a conta e obtém um gsToken novo em *novo (não mexe no atual)
static AuthStatus obter_streaming_novo(AuthConta *conta, AuthStreaming *novo) {
    AuthStatus st = auth_renovar(conta);
    if (st == AUTH_OK) {
        if (!auth_salvar_refresh(NX_ARQ_TOKEN, conta->refresh_token))
            LOG("main: nao foi possivel salvar o token renovado");
        st = auth_streaming(conta, novo);
    }
    return st;
}

static void keepalive_esperar(void);

static void aplicar_streaming_novo(AuthStreaming *atual, AuthStreaming *novo) {
    keepalive_esperar(); // o keepalive em voo lê o token atual
    auth_streaming_liberar(atual);
    *atual = *novo;
    memset(novo, 0, sizeof *novo);
    g_streaming_obtido = time(NULL);
    g_forcar_renovacao = false;
}

// Antes de abrir uma sessão (inclusive ao reconectar): renova se estiver vencendo
static void garantir_streaming(AuthConta *conta, AuthStreaming *s) {
    if (!streaming_vencendo(s, RENOVAR_ANTES_SESSAO_S))
        return;
    LOG("main: acesso ao streaming %s (obtido ha %ld s); renovando",
        g_forcar_renovacao ? "recusado (HTTP 401)" : "vencendo", (long)(time(NULL) - g_streaming_obtido));
    if (g_reconectando || g_console.nome[0]) {
        mostrar_conexao("Renovando o acesso ao Xbox Live", NULL, NULL);
        mostrar_agora();
    } else {
        mostrar_aguarde("Renovando o acesso ao Xbox Live...");
    }
    AuthStreaming novo = {0};
    AuthStatus st = obter_streaming_novo(conta, &novo);
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
    LOG("main: acesso ao streaming vencendo (obtido ha %ld s); renovando em segundo plano",
        (long)(time(NULL) - g_streaming_obtido));
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

static void tempo_texto(char *buf, size_t tam, int s) {
    if (s >= 3600)
        snprintf(buf, tam, "%d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60);
    else
        snprintf(buf, tam, "%d:%02d", s / 60, s % 60);
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

static void linha_etapa(const char *nome, EtapaConexao atual, EtapaConexao esta, double t, const char *extra) {
    if (atual > esta)
        tp("  %-26s " COR_OK "ok" COR_FIM " %5.1f s  %s\n", nome, t, extra ? extra : "");
    else if (atual == esta)
        tp("  %-26s " COR_DESTAQ "..." COR_FIM "\n", nome);
    else
        tp("  %-26s " COR_FRACA "-" COR_FIM "\n", nome);
}

// Menu do vídeo (+ e - segurados por 1 s)
enum { MENU_VOLTAR, MENU_XBOX, MENU_ESTATISTICAS, MENU_DESCONECTAR, MENU_FECHAR, MENU_ITENS };

static void desenhar_menu(int item, bool estatisticas) {
    const char *nomes[MENU_ITENS] = {"Voltar ao jogo", "Botao Xbox (guia)",
                                     estatisticas ? "Esconder estatisticas" : "Mostrar estatisticas",
                                     "Desconectar", "Fechar o app"};
    const int x = 22, y = 15, largura = 36;
    // Cada linha é preenchida até a largura para o fundo da caixa ficar uniforme
    sobre_cursor(x, y);
    sobre_printf("%-*s", largura, "");
    sobre_cursor(x, y + 1);
    sobre_printf("  " COR_TITULO "NXStream" COR_FIM "%-*s", largura - 10, "");
    sobre_cursor(x, y + 2);
    sobre_printf("%-*s", largura, "");
    for (int i = 0; i < MENU_ITENS; i++) {
        sobre_cursor(x, y + 3 + i);
        if (i == item)
            sobre_printf("  " COR_DESTAQ "> %-*s" COR_FIM, largura - 4, nomes[i]);
        else
            sobre_printf("    %-*s", largura - 4, nomes[i]);
    }
    sobre_cursor(x, y + 3 + MENU_ITENS);
    sobre_printf("%-*s", largura, "");
    sobre_cursor(x, y + 4 + MENU_ITENS);
    sobre_printf(COR_FRACA "  Cima/baixo, A = ok, B = voltar    " COR_FIM);
    sobre_cursor(x, y + 5 + MENU_ITENS);
    sobre_printf("%-*s", largura, "");
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
                    bool ok = oferta && xapi_sessao_enviar_sdp(streaming, sessao, oferta);
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
                    int st = xapi_sessao_receber_sdp(streaming, sessao, &resposta);
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
                        if (!xapi_sessao_enviar_ice(streaming, sessao, &e.locais, e.ufrag))
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
                    int st = xapi_sessao_receber_ice(streaming, sessao, &remotos);
                    proxima_consulta = segundos_desde(inicio_etapa) + CONEXAO_INTERVALO_CONSULTA;
                    if (st < 0)
                        return falha_conexao(streaming, sessao, "Falha ao receber os candidatos do console.",
                                             xapi_ultimo_erro());
                    if (st == 1) {
                        // Só IPv4 da rede local: público/IPv6 não servem e IPv6 fechava o app
                        int recebidos = remotos.total;
                        xapi_filtrar_rede_local(&remotos);
                        LOG("main: %d candidato(s) do console, %d da rede local usado(s)", recebidos, remotos.total);
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
                    LOG("main: conectado ao console em %.2f s", e.t_conectado);
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
                            "%lu+ %lu- blocos",
                            fps, mbps, e.quadros, e.quadros_chave,
                            e.sps_valido ? e.sps.largura : 0, e.sps_valido ? e.sps.altura : 0,
                            fps_decod, d.ms_medio, d.ms_maximo, d.erros, d.descartados, d.nao_exibidos,
                            d.repetidos, a.pacotes, a.perdidos, a.fila_ms, a.faltas, a.descartados_ms,
                            a.acelerados, a.desacelerados);
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
                sobre_limpar(mostrar_estatisticas || menu_aberto ? 150 : 0);
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
            char extra[64];
            cabecalho();
            tp("Console: " COR_DESTAQ "%s" COR_FIM " (%s)   sessao pronta em %.0f s\n\n", g_console.nome,
                   xapi_tipo_amigavel(g_console.tipo), tempo_preparo);
            tp("Conectando ao console:\n");
            snprintf(extra, sizeof extra, "(%d candidatos locais)", e.locais.total);
            linha_etapa("1. Oferta local", etapa, ET_OFERTA, t_etapa[ET_OFERTA], extra);
            linha_etapa("2. Resposta do console", etapa, ET_RESPOSTA, t_etapa[ET_RESPOSTA], NULL);
            snprintf(extra, sizeof extra, "(%d do console, %d aceitos)", remotos.total, remotos_aceitos);
            linha_etapa("3. Candidatos de rede", etapa, ET_CANDIDATOS, t_etapa[ET_CANDIDATOS], extra);
            linha_etapa("4. Conexao WebRTC", etapa, ET_CONECTANDO, t_etapa[ET_CONECTANDO], NULL);
            if (e.handshake_ok)
                tp("  %-26s " COR_OK "ok" COR_FIM " %5.1f s\n", "5. Handshake dos canais", e.t_handshake);
            else
                tp("  %-26s %s\n", "5. Handshake dos canais",
                       etapa == ET_CONECTADO ? COR_DESTAQ "..." COR_FIM : COR_FRACA "-" COR_FIM);
            tp("\nCanais:");
            for (int c = 0; c < NUM_CANAIS; c++)
                tp("  %s %s", conexao_nome_canal((Canal)c),
                       e.canal_aberto[c] ? COR_OK "aberto" COR_FIM : COR_FRACA "-" COR_FIM);
            tp("\n");

            if (etapa == ET_CONECTADO) {
                char t_con[16];
                tempo_texto(t_con, sizeof t_con, (int)(segundos_desde(inicio) - e.t_conectado));
                tp("\n" COR_OK "CONECTADO" COR_FIM " ha %s\n", t_con);
                tp("\nVideo:\n");
                if (e.sps_valido)
                    tp("  Resolucao: " COR_DESTAQ "%dx%d" COR_FIM "  (H.264 %s, nivel %d.%d)\n",
                           e.sps.largura, e.sps.altura, h264_nome_perfil(e.sps.perfil),
                           e.sps.nivel / 10, e.sps.nivel % 10);
                else
                    tp("  Resolucao: " COR_FRACA "aguardando o primeiro SPS" COR_FIM "\n");
                tp("  Quadros:   %s%.0f/s" COR_FIM "  %.1f Mbps  (%lu no total, %.1f MB)\n",
                       fps >= 50 ? COR_OK : fps >= 25 ? COR_DESTAQ : COR_ERRO, fps, mbps, e.quadros,
                       e.bytes_video / 1048576.0);
                tp("  Chave:     %lu", e.quadros_chave);
                if (e.quadros_chave)
                    tp("  (primeiro em %.1f s apos conectar)", e.t_primeiro_chave - e.t_conectado);
                if (e.pedidos_quadro_chave)
                    tp("  [%lu pedido(s)]", e.pedidos_quadro_chave);
                tp("\n  Maior quadro: %.1f KB   Audio: %lu pacotes\n", e.maior_quadro / 1024.0,
                       e.pacotes_audio);

                DecodEstado d;
                decod_ler(&d);
                tp("\nDecodificacao:\n");
                if (!d.ativo && d.decodificados == 0) {
                    tp("  " COR_ERRO "Decodificador nao iniciou" COR_FIM " (detalhes no log)\n");
                } else {
                    tp("  %s%s" COR_FIM "\n", d.hardware ? COR_OK : COR_DESTAQ, d.descricao);
                    tp("  Decodificados: %s%.0f/s" COR_FIM "  (%lu no total)",
                           fps_decod >= 50 ? COR_OK : fps_decod >= 25 ? COR_DESTAQ : COR_ERRO, fps_decod,
                           d.decodificados);
                    if (d.largura)
                        tp("  %dx%d %s", d.largura, d.altura, d.formato);
                    tp("\n  Tempo:  %s%.1f ms" COR_FIM " medio, %.1f ms maximo  (limite p/ 60/s: 16,7 ms)\n",
                           d.ms_medio < 12 ? COR_OK : d.ms_medio < 16.7 ? COR_DESTAQ : COR_ERRO, d.ms_medio,
                           d.ms_maximo);
                    tp("  Erros: %s%lu" COR_FIM "   Descartados: %lu   Fila: %d\n",
                           d.erros ? COR_ERRO : COR_OK, d.erros, d.descartados, d.fila);
                    tp("  Tela: %lu pulados, %lu repetidos\n", d.nao_exibidos, d.repetidos);
                }
                tp("\n");
                if (status_keepalive < 0)
                    tp("Keepalive: primeiro envio em %d s\n",
                           SESSAO_INTERVALO_KEEPALIVE_S - (int)segundos_desde(ultimo_keepalive));
                else
                    tp("Keepalive: %s (HTTP %ld) ha %d s  [%d ok, %d falhas]\n",
                           (status_keepalive >= 200 && status_keepalive < 300) ? COR_OK "ok" COR_FIM
                                                                                : COR_ERRO "falhou" COR_FIM,
                           status_keepalive, (int)segundos_desde(ultimo_keepalive), ka_ok, ka_falhas);
                if (apos_sleep[0])
                    tp("%s\n", apos_sleep);
                AudioEstado a;
                audio_ler(&a);
                if (!a.ativo)
                    tp("Audio: " COR_ERRO "desligado" COR_FIM " (detalhes no log)\n");
                else
                    tp("Audio: %s, %lu pacotes, %lu perdidos, fila %d ms, %lu faltas\n", a.descricao, a.pacotes,
                       a.perdidos, a.fila_ms, a.faltas);
                tp("Controle: %lu pacotes enviados ao Xbox\n", pacotes_input);
                if (video_erro[0])
                    tp(COR_ERRO "Tela: %s" COR_FIM " (detalhes no log)\n", video_erro);
                else if (!em_video)
                    tp(COR_FRACA "O video aparece assim que o primeiro quadro for decodificado." COR_FIM "\n");
            }
            if (em_video)
                tp("\nSegure + e - por 1 s: menu (desconectar, fechar, botao Xbox)\n");
            else
                tp("\nB = desconectar   + = desconectar e fechar\n");
        }
        if (redesenhar && em_video) {
            if (menu_aberto)
                desenhar_menu(menu_item, mostrar_estatisticas);
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

// Abre a sessão e espera ela ficar pronta. Retorna PASSO_SESSAO se deu certo
// (ou o passo seguinte: erro, lista, sair).
static Passo abrir_sessao(AuthConta *conta, AuthStreaming *streaming, XSessao *sessao_saida, double *preparo) {
    mostrar_conexao("Abrindo a sessão", NULL, NULL);
    mostrar_agora();

    XSessao sessao;
    if (!xapi_sessao_iniciar(streaming, g_console.server_id, &sessao))
        return erro_sessao("Nao foi possivel abrir a sessao.", xapi_ultimo_erro());

    // --- 1. Preparação: consulta o estado até ficar pronta -----------------
    u64 inicio = armGetSystemTick();
    double proxima = 0;
    int tentativas_connect = 0, erros_seguidos = 0, ultimo_segundo = -1;
    XSessaoEstado estado = XSESSAO_PREPARANDO;
    char detalhe[160] = "";
    bool fechar = false;

    while (estado != XSESSAO_PRONTA) {
        double passou = segundos_desde(inicio);

        if (passou > SESSAO_TEMPO_MAX_PREPARO_S) {
            LOG("main: sessao nao ficou pronta em %d s", SESSAO_TEMPO_MAX_PREPARO_S);
            encerrar_sessao(streaming, &sessao);
            return erro_sessao("O console demorou demais para responder.",
                               "Confira se ele esta ligado ou em repouso e conectado a internet.");
        }

        if (passou >= proxima) {
            // Intervalo contado do início da consulta (antes contava do fim e,
            // com ~1 s por requisição, a consulta saía a cada 2 s)
            proxima = passou + SESSAO_INTERVALO_ESTADO_S;
            estado = xapi_sessao_estado(streaming, &sessao, detalhe, sizeof detalhe);
            LOG("main: sessao: %s (%.0f s)", detalhe[0] ? detalhe : xapi_sessao_estado_texto(estado), passou);

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
                return erro_sessao("O console recusou a sessao.", detalhe);
            }

            if (estado == XSESSAO_AGUARDA_LOGIN) {
                if (tentativas_connect >= SESSAO_MAX_CONNECT) {
                    encerrar_sessao(streaming, &sessao);
                    return erro_sessao("O console nao aceitou o login.",
                                       "Confira se e a mesma conta configurada no Xbox.");
                }
                tentativas_connect++;
                char *token = NULL;
                if (auth_token_console(conta, &token) != AUTH_OK) {
                    encerrar_sessao(streaming, &sessao);
                    return erro_sessao("Nao foi possivel autorizar o acesso ao console.", auth_ultimo_erro());
                }
                bool ok = xapi_sessao_conectar(streaming, &sessao, token);
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
            snprintf(detalhe, sizeof detalhe, "%s · %d s%s", xapi_tipo_amigavel(g_console.tipo), (int)passou,
                     estado == XSESSAO_PREPARANDO
                         ? "\nSe o console estava em repouso, ele acorda sem ligar a TV."
                         : "");
            mostrar_conexao(xapi_sessao_estado_texto(estado), detalhe, "B=Cancelar");
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
    return PASSO_SESSAO;
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
                    passo = conta.refresh_token ? PASSO_STREAMING : PASSO_LOGIN;
                    break;
                case PASSO_LOGIN:
                    passo = passo_login(&conta, aviso_login);
                    aviso_login = NULL;
                    recem_logada = (passo == PASSO_STREAMING);
                    break;
                case PASSO_STREAMING:
                    auth_streaming_liberar(&streaming);
                    passo = passo_streaming(&conta, &streaming, recem_logada, &aviso_login);
                    recem_logada = false;
                    g_streaming_obtido = time(NULL);
                    g_forcar_renovacao = false;
                    break;
                case PASSO_LISTA:
                    passo = passo_lista(&conta, &streaming);
                    break;
                case PASSO_SESSAO:
                    passo = passo_sessao(&conta, &streaming);
                    break;
                case PASSO_DIAG:
                    passo = passo_diag();
                    break;
                default:
                    passo = PASSO_SAIR;
                    break;
            }
        }

        // Cada passo vai para o log: os logs da v0.3.0 e v0.4.1 terminavam antes
        // de "encerrado" mesmo com o app fechando sem erro na tela.
        LOG("main: encerrando (1/4: liberando a conta)");
        auth_streaming_liberar(&streaming);
        auth_conta_liberar(&conta);
        LOG("main: encerrando (2/4: fechando conexoes WebRTC)");
        conexao_encerrar();
        rtc_diag_finalizar_biblioteca();
        LOG("main: encerrando (3/4: liberando a libdatachannel)");
        rtc_comum_finalizar();
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
