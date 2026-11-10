#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

static FILE *g_arquivo = NULL;
static bool g_stderr = false;

void log_iniciar(const char *caminho, bool tambem_stderr) {
    g_stderr = tambem_stderr;
    if (caminho)
        g_arquivo = fopen(caminho, "a");
    log_msg("===== NXStream iniciado =====");
}

void log_fechar(void) {
    if (g_arquivo) {
        log_msg("===== NXStream encerrado =====");
        fclose(g_arquivo);
        g_arquivo = NULL;
    }
}

static void escrever(FILE *f, const char *hora, const char *fmt, va_list args) {
    fprintf(f, "[%s] ", hora);
    vfprintf(f, fmt, args);
    fputc('\n', f);
    fflush(f);
}

void log_msg(const char *fmt, ...) {
    char hora[32];
    time_t agora = time(NULL);
    struct tm *t = localtime(&agora);
    strftime(hora, sizeof hora, "%Y-%m-%d %H:%M:%S", t);

    va_list args;
    if (g_arquivo) {
        va_start(args, fmt);
        escrever(g_arquivo, hora, fmt, args);
        va_end(args);
    }
    if (g_stderr) {
        va_start(args, fmt);
        escrever(stderr, hora, fmt, args);
        va_end(args);
    }
}
