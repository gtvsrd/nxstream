// NXStream - Fase 1: login na conta Microsoft e lista de consoles
//
// Interface em texto simples (console do libnx). Fluxo:
//   INICIO -> (sem token salvo) LOGIN -> STREAMING -> LISTA
//   INICIO -> (com token salvo) STREAMING -> LISTA
// Nenhum erro fecha o app: sempre há "tentar de novo" ou "sair da conta".

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <switch.h>

#include "api/config.h"
#include "api/xapi.h"
#include "auth/auth.h"
#include "core/http.h"
#include "core/log.h"

#define COR_TITULO  "\x1b[36;1m"
#define COR_DESTAQ  "\x1b[33;1m"
#define COR_OK      "\x1b[32;1m"
#define COR_ERRO    "\x1b[31;1m"
#define COR_FRACA   "\x1b[37m"
#define COR_FIM     "\x1b[0m"

typedef enum { PASSO_INICIO, PASSO_LOGIN, PASSO_STREAMING, PASSO_LISTA, PASSO_SAIR } Passo;

static PadState g_controle;
static u64 g_tick_anterior;

// ---------------------------------------------------------------------------
// Sistema: eventos do applet e detector de congelamento
// ---------------------------------------------------------------------------
static void ao_evento_do_sistema(AppletHookType tipo, void *param) {
    (void)param;
    switch (tipo) {
        case AppletHookType_OnResume:
            LOG("sistema: app retomado");
            break;
        case AppletHookType_OnFocusState:
            LOG("sistema: foco = %s", appletGetFocusState() == AppletFocusState_InFocus
                                          ? "primeiro plano" : "segundo plano");
            break;
        case AppletHookType_OnOperationMode:
            LOG("sistema: modo = %s", appletGetOperationMode() == AppletOperationMode_Console
                                          ? "dock" : "portatil");
            break;
        default:
            break;
    }
}

static double segundos_desde(u64 tick) {
    return armTicksToNs(armGetSystemTick() - tick) / 1e9;
}

// Um quadro: processa o sistema e o controle. Retorna os botões apertados.
// *fechar fica true quando o sistema pede para encerrar o app.
static u64 quadro(bool *fechar) {
    consoleUpdate(NULL);
    if (!appletMainLoop()) {
        *fechar = true;
        return 0;
    }
    double parado = segundos_desde(g_tick_anterior);
    g_tick_anterior = armGetSystemTick();
    if (parado > 1.0)
        LOG("sistema: app ficou congelado por %.1f s", parado);

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
    consoleClear();
    printf(COR_TITULO "NXStream" COR_FIM " v%s - Xbox Remote Play\n", APP_VERSION);
    printf("================================================================\n\n");
}

static void mostrar_aguarde(const char *mensagem) {
    cabecalho();
    printf("%s\n", mensagem);
    consoleUpdate(NULL);
}

// Tela de erro com opções. Retorna o botão escolhido (0 = app fechando).
static u64 mostrar_erro(const char *titulo, const char *detalhe, bool permite_sair_conta) {
    cabecalho();
    printf(COR_ERRO "%s" COR_FIM "\n\n", titulo);
    if (detalhe && detalhe[0])
        printf("%s\n\n", detalhe);
    printf("A = tentar de novo\n");
    if (permite_sair_conta)
        printf("X = sair da conta\n");
    printf("+ = fechar o app\n");
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
static Passo passo_login(AuthConta *conta, const char *aviso) {
    for (;;) {
        mostrar_aguarde("Gerando codigo de login...");
        AuthCodigo codigo;
        AuthStatus st = auth_codigo_iniciar(&codigo);
        if (st != AUTH_OK) {
            u64 b = mostrar_erro("Nao foi possivel gerar o codigo de login.", auth_ultimo_erro(), false);
            if (b != HidNpadButton_A)
                return PASSO_SAIR;
            continue;
        }

        u64 inicio = armGetSystemTick();
        int intervalo = codigo.intervalo_s;
        double proxima = intervalo;
        int ultimo_segundo = -1;
        bool sem_rede = false;
        const char *motivo_fim = NULL;
        bool fechar = false;

        while (!fechar && !motivo_fim) {
            double passou = segundos_desde(inicio);
            int restante = codigo.expira_em_s - (int)passou;

            if ((int)passou != ultimo_segundo) {
                ultimo_segundo = (int)passou;
                cabecalho();
                if (aviso) printf(COR_DESTAQ "%s" COR_FIM "\n\n", aviso);
                printf("Para entrar com sua conta Microsoft:\n\n");
                printf("  1. No celular ou PC, acesse:\n\n");
                printf("       " COR_DESTAQ "%s" COR_FIM "\n\n", endereco_curto(codigo.endereco));
                printf("  2. Digite o codigo:\n\n");
                printf("       " COR_DESTAQ "%s" COR_FIM "\n\n", codigo.codigo_usuario);
                printf("  3. Entre com a conta do seu Xbox e confirme.\n\n");
                printf(COR_FRACA "Aguardando login... (codigo expira em %d:%02d)" COR_FIM "\n",
                       restante > 0 ? restante / 60 : 0, restante > 0 ? restante % 60 : 0);
                if (sem_rede)
                    printf(COR_ERRO "Sem conexao no momento, tentando de novo..." COR_FIM "\n");
                printf("\n+ = fechar o app\n");
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

            if (quadro(&fechar) & HidNpadButton_Plus)
                return PASSO_SAIR;
        }
        if (fechar)
            return PASSO_SAIR;

        cabecalho();
        printf(COR_ERRO "%s" COR_FIM "\n\n", motivo_fim);
        printf("A = gerar outro codigo\n+ = fechar o app\n");
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
            *aviso_login = "Sua sessao expirou. Faca login de novo.";
            return PASSO_LOGIN;
        }
        if (st != AUTH_OK) {
            u64 b = mostrar_erro("Nao foi possivel entrar na conta.", auth_ultimo_erro(), true);
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
        u64 b = mostrar_erro("Nao foi possivel acessar o Xbox Live.", auth_ultimo_erro(), true);
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
    cabecalho();
    printf("Sair da conta? Na proxima vez sera preciso fazer login de novo.\n\n");
    printf("A = sim, sair da conta\nB = cancelar\n");
    return esperar_botao(HidNpadButton_A | HidNpadButton_B) == HidNpadButton_A;
}

static Passo passo_lista(AuthConta *conta, AuthStreaming *streaming, time_t streaming_obtido) {
    // gsToken vencendo (dura ~4 h): obtém outro antes de usar
    if (streaming->duracao_s > 0 && time(NULL) - streaming_obtido > streaming->duracao_s - 120)
        return PASSO_STREAMING;

    mostrar_aguarde("Procurando seus consoles...");
    XConsoles consoles;
    if (!xapi_listar_consoles(streaming, &consoles)) {
        u64 b = mostrar_erro("Nao foi possivel listar seus consoles.", xapi_ultimo_erro(), true);
        if (b == HidNpadButton_X && confirmar_sair_da_conta()) {
            auth_apagar(NX_ARQ_TOKEN);
            auth_conta_liberar(conta);
            return PASSO_LOGIN;
        }
        return b == HidNpadButton_Plus || b == 0 ? PASSO_SAIR : PASSO_LISTA;
    }

    int sel = 0;
    const char *mensagem = NULL;
    for (;;) {
        cabecalho();
        if (streaming->gamertag[0])
            printf("Conta: " COR_OK "%s" COR_FIM "\n\n", streaming->gamertag);
        else
            printf("Conta: " COR_OK "conectada" COR_FIM "\n\n");

        if (consoles.total == 0) {
            printf(COR_DESTAQ "Nenhum console encontrado nesta conta." COR_FIM "\n\n");
            printf("No Xbox, ative em: Configuracoes > Dispositivos e conexoes >\n");
            printf("Recursos remotos > Habilitar recursos remotos.\n\n");
        } else {
            printf("Seus consoles:\n\n");
            for (int i = 0; i < consoles.total; i++) {
                const XConsole *x = &consoles.itens[i];
                printf("%s %-24.24s %-16s %s\n" COR_FIM, i == sel ? COR_DESTAQ " >" : "  ",
                       x->nome, xapi_tipo_amigavel(x->tipo), xapi_estado_amigavel(x->estado));
            }
            printf("\n");
        }

        if (mensagem)
            printf("%s\n\n", mensagem);

        if (consoles.total > 0)
            printf("A = conectar   ");
        printf("Y = atualizar   X = sair da conta   + = fechar\n");

        u64 b = esperar_botao(HidNpadButton_A | HidNpadButton_Y | HidNpadButton_X | HidNpadButton_Plus |
                              HidNpadButton_AnyUp | HidNpadButton_AnyDown);
        mensagem = NULL;
        if (b == 0 || (b & HidNpadButton_Plus))
            return PASSO_SAIR;
        if (b & HidNpadButton_Y)
            return PASSO_LISTA;
        if ((b & HidNpadButton_X) && confirmar_sair_da_conta()) {
            auth_apagar(NX_ARQ_TOKEN);
            auth_conta_liberar(conta);
            LOG("main: usuario saiu da conta");
            return PASSO_LOGIN;
        }
        if ((b & HidNpadButton_AnyUp) && sel > 0)
            sel--;
        if ((b & HidNpadButton_AnyDown) && sel < consoles.total - 1)
            sel++;
        if ((b & HidNpadButton_A) && consoles.total > 0) {
            LOG("main: console escolhido: %s", consoles.itens[sel].nome);
            mensagem = COR_DESTAQ "A conexao com o console chega na fase 2." COR_FIM;
        }
    }
}

// ---------------------------------------------------------------------------
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&g_controle);
    g_tick_anterior = armGetSystemTick();

    Result rc_rede = socketInitializeDefault();
    bool nxlink = R_SUCCEEDED(rc_rede) && nxlinkStdioForDebug() >= 0;

    mkdir(NX_PASTA_APP, 0777);
    log_iniciar(NX_ARQ_LOG, nxlink);
    LOG("main: versao %s, nxlink %s", APP_VERSION, nxlink ? "ativo" : "inativo");

    AppletHookCookie cookie;
    appletHook(&cookie, ao_evento_do_sistema, NULL);
    appletSetFocusHandlingMode(AppletFocusHandlingMode_SuspendHomeSleepNotify);

    if (appletGetAppletType() != AppletType_Application) {
        cabecalho();
        printf(COR_ERRO "O NXStream precisa ser aberto em title mode." COR_FIM "\n\n");
        printf("Segure R ao abrir um jogo e escolha o NXStream no menu.\n\n+ = fechar\n");
        LOG("main: aberto em applet mode, encerrando");
        esperar_botao(HidNpadButton_Plus);
    } else if (R_FAILED(rc_rede) || !http_iniciar()) {
        cabecalho();
        printf(COR_ERRO "Falha ao iniciar a rede (0x%x)." COR_FIM "\n\n+ = fechar\n", rc_rede);
        esperar_botao(HidNpadButton_Plus);
    } else {
        AuthConta conta = {0};
        AuthStreaming streaming = {0};
        time_t streaming_obtido = 0;
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
                    streaming_obtido = time(NULL);
                    break;
                case PASSO_LISTA:
                    passo = passo_lista(&conta, &streaming, streaming_obtido);
                    break;
                default:
                    passo = PASSO_SAIR;
                    break;
            }
        }

        auth_streaming_liberar(&streaming);
        auth_conta_liberar(&conta);
        http_encerrar();
    }

    appletUnhook(&cookie);
    log_fechar();
    socketExit();
    consoleExit(NULL);
    return 0;
}
