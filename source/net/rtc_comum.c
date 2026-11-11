#include "rtc_comum.h"
#include "../core/log.h"

#include <stdbool.h>
#include <rtc/rtc.h>

static bool g_iniciada = false;

static void ao_log(rtcLogLevel nivel, const char *mensagem) {
    static const char *nomes[] = {"-", "FATAL", "ERRO", "AVISO", "INFO", "DEBUG", "VERBOSE"};
    LOG("rtc[%s]: %s", (nivel >= 0 && nivel <= 6) ? nomes[nivel] : "?", mensagem);
}

void rtc_comum_iniciar(void) {
    if (g_iniciada)
        return;
    rtcInitLogger(RTC_LOG_INFO, ao_log);
    g_iniciada = true;
}

void rtc_comum_finalizar(void) {
    // Não chama rtcCleanup(): no Switch ele lança std::system_error ao encerrar
    // as threads internas (log da v0.4.4), e o app fechava por exceção não
    // tratada no último passo. As conexões já foram apagadas antes daqui, e a
    // memória e as threads do processo são liberadas pelo sistema ao sair.
    g_iniciada = false;
}
