// NXStream - Fase 0: app de diagnóstico
//
// Objetivo desta versão: validar o build automático e testar no Switch
// três coisas que a o streaming vai precisar:
//   1. Se o app está rodando em title mode (obrigatório para o streaming)
//   2. Se a rede está funcionando (mostra o IP do Switch)
//   3. Se conseguimos detectar quando o Switch dorme e acorda
//      (base para corrigir o bug de "tela desligou -> app encerra")

#include <stdio.h>
#include <time.h>
#include <arpa/inet.h>
#include <switch.h>

static int g_eventos_resume = 0;

static void imprimir_hora(void) {
    time_t agora = time(NULL);
    struct tm *t = localtime(&agora);
    printf("[%02d:%02d:%02d] ", t->tm_hour, t->tm_min, t->tm_sec);
}

// Chamado pelo libnx (dentro do appletMainLoop) quando o estado do sistema muda
static void ao_evento_do_sistema(AppletHookType tipo, void *param) {
    (void)param;
    switch (tipo) {
        case AppletHookType_OnResume:
            g_eventos_resume++;
            imprimir_hora();
            printf("Switch acordou do sleep (vez #%d)\n", g_eventos_resume);
            break;
        case AppletHookType_OnFocusState:
            imprimir_hora();
            printf("Foco mudou: %s\n",
                   appletGetFocusState() == AppletFocusState_InFocus
                       ? "app em primeiro plano"
                       : "app em segundo plano");
            break;
        case AppletHookType_OnOperationMode:
            imprimir_hora();
            printf("Modo mudou: %s\n",
                   appletGetOperationMode() == AppletOperationMode_Console
                       ? "na dock (TV)"
                       : "portatil");
            break;
        default:
            break;
    }
}

static void mostrar_rede(void) {
    u32 ip = 0;
    Result rc = nifmGetCurrentIpAddress(&ip); // falha se não houver conexão
    struct in_addr endereco = { .s_addr = ip };
    if (R_FAILED(rc) || ip == 0)
        printf("Rede: SEM CONEXAO\n");
    else
        printf("Rede: conectado, IP do Switch = %s\n", inet_ntoa(endereco));
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState controle;
    padInitializeDefault(&controle);

    Result rc = socketInitializeDefault();
    if (R_SUCCEEDED(rc))
        rc = nifmInitialize(NifmServiceType_User);

    AppletHookCookie cookie;
    appletHook(&cookie, ao_evento_do_sistema, NULL);

    // Por padrão o libnx congela o app em silêncio no sleep e no HOME.
    // Este modo continua suspendendo, mas pede ao sistema para AVISAR.
    Result rc_foco = appletSetFocusHandlingMode(AppletFocusHandlingMode_SuspendHomeSleepNotify);

    printf("NXStream - diagnostico v%s\n", APP_VERSION);
    printf("========================================\n\n");

    if (appletGetAppletType() == AppletType_Application)
        printf("Modo: TITLE MODE (ok)\n");
    else
        printf("Modo: APPLET MODE - abra segurando R ao iniciar um jogo!\n");

    if (R_FAILED(rc))
        printf("Rede: falha ao iniciar servicos de rede (0x%x)\n", rc);
    else
        mostrar_rede();

    if (R_FAILED(rc_foco))
        printf("Avisos do sistema: NAO ativados (0x%x)\n", rc_foco);
    else
        printf("Avisos do sistema: ativados\n");

    printf("\nTeste 1: aperte POWER, espere e acorde o Switch.\n");
    printf("Teste 2: aperte HOME, espere e volte para o app.\n");
    printf("Os eventos vao aparecer abaixo sozinhos.\n\n");
    printf("Y = checar rede de novo   + = sair\n\n");

    u64 tick_anterior = armGetSystemTick();

    while (appletMainLoop()) {
        // Detector de congelamento: se passou muito tempo desde o último
        // quadro, o app esteve suspenso (sleep ou HOME), com ou sem aviso.
        u64 tick_agora = armGetSystemTick();
        u64 ns = armTicksToNs(tick_agora - tick_anterior);
        tick_anterior = tick_agora;
        if (ns > 1000000000ULL) {
            imprimir_hora();
            printf("App ficou congelado por %.1f s\n", ns / 1e9);
            imprimir_hora();
            mostrar_rede();
        }

        padUpdate(&controle);
        u64 apertados = padGetButtonsDown(&controle);

        if (apertados & HidNpadButton_Plus)
            break;
        if (apertados & HidNpadButton_Y) {
            imprimir_hora();
            mostrar_rede();
        }

        consoleUpdate(NULL);
    }

    appletUnhook(&cookie);
    nifmExit();
    socketExit();
    consoleExit(NULL);
    return 0;
}
