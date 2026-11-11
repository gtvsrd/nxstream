#include "log.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>

// Acima disto, o log é recomeçado do zero na próxima abertura do app
#define LOG_TAMANHO_MAXIMO (1024 * 1024)

static FILE *g_arquivo = NULL;
static bool g_stderr = false;
// As threads da libdatachannel também escrevem no log: cada linha sai inteira
static pthread_mutex_t g_trava = PTHREAD_MUTEX_INITIALIZER;

void log_iniciar(const char *caminho, bool tambem_stderr) {
    g_stderr = tambem_stderr;
    bool recomecar = false;
    if (caminho) {
        struct stat info;
        recomecar = stat(caminho, &info) == 0 && info.st_size > LOG_TAMANHO_MAXIMO;
        g_arquivo = fopen(caminho, recomecar ? "w" : "a");
    }
    log_msg("===== NXStream iniciado =====%s", recomecar ? " (log anterior passou de 1 MB e foi apagado)" : "");
}

void log_fechar(void) {
    if (g_arquivo) {
        log_msg("===== NXStream encerrado =====");
        pthread_mutex_lock(&g_trava);
        fclose(g_arquivo);
        g_arquivo = NULL;
        pthread_mutex_unlock(&g_trava);
    }
}

void log_msg(const char *fmt, ...) {
    // Monta a linha inteira antes de escrever, para nada se misturar
    char linha[8192]; // cabe um SDP inteiro (oferta e resposta do console)
    time_t agora = time(NULL);
    struct tm t;
    localtime_r(&agora, &t);
    int n = (int)strftime(linha, sizeof linha, "[%Y-%m-%d %H:%M:%S] ", &t);

    va_list args;
    va_start(args, fmt);
    int escrito = vsnprintf(linha + n, sizeof linha - (size_t)n, fmt, args);
    va_end(args);
    if (escrito < 0)
        escrito = 0;
    size_t tam = (size_t)n + (size_t)escrito;
    if (tam > sizeof linha - 2)
        tam = sizeof linha - 2; // linha truncada
    linha[tam++] = '\n';
    linha[tam] = '\0';

    pthread_mutex_lock(&g_trava);
    if (g_arquivo) {
        fwrite(linha, 1, tam, g_arquivo);
        fflush(g_arquivo);
    }
    if (g_stderr)
        fwrite(linha, 1, tam, stderr);
    pthread_mutex_unlock(&g_trava);
}
