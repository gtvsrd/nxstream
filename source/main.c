// NXStream - Fase 2b: login, lista de consoles, sessão de streaming e diagnóstico WebRTC
//
// Interface em texto simples (console do libnx). Fluxo:
//   INICIO -> (sem token salvo) LOGIN -> STREAMING -> LISTA
//   INICIO -> (com token salvo) STREAMING -> LISTA
//   LISTA -> (A no console) SESSAO -> LISTA
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
#include "net/rtc_diag.h"

#define COR_TITULO  "\x1b[36;1m"
#define COR_DESTAQ  "\x1b[33;1m"
#define COR_OK      "\x1b[32;1m"
#define COR_ERRO    "\x1b[31;1m"
#define COR_FRACA   "\x1b[37m"
#define COR_FIM     "\x1b[0m"

typedef enum { PASSO_INICIO, PASSO_LOGIN, PASSO_STREAMING, PASSO_LISTA, PASSO_SESSAO, PASSO_DIAG, PASSO_SAIR } Passo;

static PadState g_controle;
static u64 g_tick_anterior;
static double g_ultimo_congelamento = 0; // segundos suspenso no último sleep/HOME (0 = nada novo)
static bool g_retorno_pendente = false;   // sistema avisou que o app voltou a rodar
static bool g_em_foco = true;             // último estado de foco conhecido
static XConsole g_console;               // console escolhido na lista
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

// Um quadro: processa o sistema e o controle. Retorna os botões apertados.
// *fechar fica true quando o sistema pede para encerrar o app.
static u64 quadro(bool *fechar) {
    consoleUpdate(NULL);
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
        printf(COR_FRACA "- = diagnostico WebRTC" COR_FIM "\n");

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
    cabecalho();
    printf(COR_ERRO "%s" COR_FIM "\n\n", titulo);
    if (detalhe && detalhe[0])
        printf("%s\n\n", detalhe);
    printf("A = tentar de novo\nB = voltar para a lista\n+ = fechar o app\n");
    u64 b = esperar_botao(HidNpadButton_A | HidNpadButton_B | HidNpadButton_Plus);
    return b == HidNpadButton_A ? PASSO_SESSAO : b == HidNpadButton_B ? PASSO_LISTA : PASSO_SAIR;
}

static void encerrar_sessao(const AuthStreaming *streaming, const XSessao *sessao) {
    mostrar_aguarde("Encerrando a sessao...");
    xapi_sessao_encerrar(streaming, sessao);
}

static void tempo_texto(char *buf, size_t tam, int s) {
    if (s >= 3600)
        snprintf(buf, tam, "%d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60);
    else
        snprintf(buf, tam, "%d:%02d", s / 60, s % 60);
}

static Passo passo_sessao(AuthConta *conta, AuthStreaming *streaming) {
    char texto[160];
    snprintf(texto, sizeof texto, "Abrindo sessao com %s...", g_console.nome);
    mostrar_aguarde(texto);

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
            estado = xapi_sessao_estado(streaming, &sessao, detalhe, sizeof detalhe);
            proxima = segundos_desde(inicio) + SESSAO_INTERVALO_ESTADO_S;
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
            cabecalho();
            printf("Console: " COR_DESTAQ "%s" COR_FIM " (%s)\n\n", g_console.nome,
                   xapi_tipo_amigavel(g_console.tipo));
            printf("%s... %d s\n\n", xapi_sessao_estado_texto(estado), (int)passou);
            printf(COR_FRACA "Se o console estava em repouso, ele acorda no modo de\n"
                   "streaming (sem ligar a TV). Pode levar alguns segundos." COR_FIM "\n\n");
            printf("B = cancelar\n");
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

    double tempo_preparo = segundos_desde(inicio);
    LOG("main: sessao pronta em %.1f s", tempo_preparo);

    // --- 2. Sessão pronta: acompanha se continua viva ----------------------
    // O keepalive só é aceito depois que a conexão WebRTC com o console está
    // aberta (fase 2b): antes disso o servidor responde HTTP 400. Por isso,
    // nesta etapa, a sessão é acompanhada consultando o estado a cada 30 s.
    u64 pronta_desde = armGetSystemTick();
    u64 ultima_verif = armGetSystemTick();
    XSessaoEstado estado_verif = XSESSAO_PRONTA;
    int verif_ok = 0, verif_falhas = 0;
    char apos_sleep[128] = "";
    ultimo_segundo = -1;
    g_ultimo_congelamento = 0; // ignora suspensões anteriores à sessão ficar pronta

    for (;;) {
        bool forcar = false;
        if (g_ultimo_congelamento > 0) {
            // Acabou de voltar do sleep/HOME: testa na hora se a sessão sobreviveu
            snprintf(apos_sleep, sizeof apos_sleep, "Voltou de %.0f s suspenso", g_ultimo_congelamento);
            g_ultimo_congelamento = 0;
            forcar = true;
        }

        if (forcar || segundos_desde(ultima_verif) >= SESSAO_INTERVALO_KEEPALIVE_S) {
            estado_verif = xapi_sessao_estado(streaming, &sessao, detalhe, sizeof detalhe);
            ultima_verif = armGetSystemTick();
            bool viva = (estado_verif == XSESSAO_PRONTA);
            if (viva) verif_ok++; else verif_falhas++;
            LOG("main: verificacao da sessao: %s", detalhe[0] ? detalhe : xapi_sessao_estado_texto(estado_verif));
            if (forcar) {
                size_t n = strlen(apos_sleep);
                snprintf(apos_sleep + n, sizeof apos_sleep - n, ": sessao %s (%s)",
                         viva ? "continua viva" : "caiu",
                         detalhe[0] ? detalhe : xapi_sessao_estado_texto(estado_verif));
                LOG("main: %s", apos_sleep);
            }
            ultimo_segundo = -1; // redesenha
        }

        int pronta = (int)segundos_desde(pronta_desde);
        if (pronta != ultimo_segundo) {
            ultimo_segundo = pronta;
            char t_pronta[16];
            tempo_texto(t_pronta, sizeof t_pronta, pronta);
            int desde_verif = (int)segundos_desde(ultima_verif);

            cabecalho();
            printf("Console: " COR_DESTAQ "%s" COR_FIM " (%s)\n\n", g_console.nome,
                   xapi_tipo_amigavel(g_console.tipo));
            printf(COR_OK "Sessao pronta!" COR_FIM " (preparada em %.0f s)\n\n", tempo_preparo);
            printf("Pronta ha: %s\n", t_pronta);
            if (verif_ok + verif_falhas == 0)
                printf("Verificacao: primeira em %d s\n", SESSAO_INTERVALO_KEEPALIVE_S - desde_verif);
            else
                printf("Verificacao: %s ha %d s  [%d ok, %d falhas]\n",
                       estado_verif == XSESSAO_PRONTA ? COR_OK "viva" COR_FIM
                                                      : COR_ERRO "nao esta pronta" COR_FIM,
                       desde_verif, verif_ok, verif_falhas);
            if (estado_verif != XSESSAO_PRONTA && detalhe[0])
                printf("Estado: %s\n", detalhe);
            if (apos_sleep[0])
                printf("%s\n", apos_sleep);
            printf("\n" COR_FRACA "Fase 2a: a sessao existe no servidor, mas o app ainda nao\n"
                   "se conecta ao console. Isso (e o keepalive) chega na fase 2b." COR_FIM "\n\n");
            printf("Teste: deixe aberta alguns minutos, ou durma o Switch e volte.\n\n");
            printf("B = encerrar a sessao   + = encerrar e fechar\n");
        }

        u64 b = quadro(&fechar);
        if (fechar || (b & HidNpadButton_Plus)) {
            encerrar_sessao(streaming, &sessao);
            return PASSO_SAIR;
        }
        if (b & HidNpadButton_B) {
            encerrar_sessao(streaming, &sessao);
            return PASSO_LISTA;
        }
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

    http_definir_ao_terminar(ao_terminar_http);

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

        auth_streaming_liberar(&streaming);
        auth_conta_liberar(&conta);
        rtc_diag_finalizar_biblioteca();
        http_encerrar();
    }

    appletUnhook(&cookie);
    log_fechar();
    socketExit();
    consoleExit(NULL);
    return 0;
}
