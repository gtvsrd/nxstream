// NXStream - log com horário
//
// Escreve no arquivo de log (cartão SD) e, se ativado, no stderr
// (que vai para o terminal do PC quando o app é aberto via nxlink).
//
// REGRA: nunca registrar tokens, códigos de login ou dados da conta.
#pragma once
#include <stdbool.h>

void log_iniciar(const char *caminho, bool tambem_stderr);
void log_fechar(void);
void log_msg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#define LOG(...) log_msg(__VA_ARGS__)
