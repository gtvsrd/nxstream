#include "rtc_comum.h"
#include "../core/log.h"

#include <stdbool.h>
#include <string.h>
#include <rtc/rtc.h>

static bool g_iniciada = false;
// Ao desconectar, os pacotes de vídeo que ainda chegam geram um aviso por
// pacote ("dropping incoming message"): centenas por desconexão. Só conta.
static unsigned long g_avisos_descartados = 0;

static void ao_log(rtcLogLevel nivel, const char *mensagem) {
    static const char *nomes[] = {"-", "FATAL", "ERRO", "AVISO", "INFO", "DEBUG", "VERBOSE"};
    if (strstr(mensagem, "dropping incoming message")) {
        g_avisos_descartados++; // contador sem trava: só informativo
        return;
    }
    LOG("rtc[%s]: %s", (nivel >= 0 && nivel <= 6) ? nomes[nivel] : "?", mensagem);
}

void rtc_comum_iniciar(void) {
    if (g_iniciada)
        return;
    rtcInitLogger(RTC_LOG_INFO, ao_log);
    g_iniciada = true;
}

void rtc_comum_finalizar(void) {
    if (!g_iniciada)
        return;
    // Espera as threads da biblioteca terminarem ANTES de fechar a rede do app:
    // sem isso, fechar o app logo após desconectar (+ na tela conectada) deixava
    // threads usando sockets já fechados (erro de sistema na v0.5.0).
    // O std::system_error da v0.4.4 vinha do detach() da thread de limpeza,
    // corrigido em compat/patches/aplicar.py.
    if (g_avisos_descartados)
        LOG("rtc: %lu avisos 'dropping incoming message' omitidos do log", g_avisos_descartados);
    rtcCleanup();
    g_iniciada = false;
}
