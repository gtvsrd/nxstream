#!/usr/bin/env python3
"""Aplica as alterações para o Switch no código das dependências.

Uso: python3 compat/patches/aplicar.py <pasta da libdatachannel>

Cada alteração procura um trecho exato do código original. Se o trecho não
for encontrado (a dependência mudou), o script para com erro em vez de
aplicar algo às cegas. Rodar duas vezes não duplica nada.
"""
import pathlib
import sys

ldc = pathlib.Path(sys.argv[1])

# usrsctp: gerador de números aleatórios do Switch (randomGet da libnx,
# criptograficamente seguro). Sem isto, user_environment.c para com
# '#error "Unknown platform. Please provide platform specific RNG."'
USRSCTP_RNG = '''#elif defined(__SWITCH__)
/* NXStream: gerador aleatorio do sistema do Switch (libnx) */
#include <switch/kernel/random.h>

void
init_random(void)
{
	return;
}

void
read_random(void *buf, size_t size)
{
	randomGet(buf, size);
	return;
}

void
finish_random(void)
{
	return;
}
'''

# libjuice: mesmo problema. Sem um ramo para o Switch, ela cai no rand()
# semeado com o relógio para gerar credenciais ICE e IDs de transação STUN.
LIBJUICE_RNG = '''#elif defined(__SWITCH__)
/* NXStream: gerador aleatorio do sistema do Switch (libnx) */
#include <switch/kernel/random.h>
static int random_bytes(void *buf, size_t size) {
	randomGet(buf, size);
	return 0;
}

'''

ALTERACOES = [
    (
        "deps/usrsctp/usrsctplib/user_environment.c",
        "#elif defined(_WIN32)\n",   # o ramo do Switch entra logo antes do ramo do Windows
        USRSCTP_RNG,
        "defined(__SWITCH__)",       # marca de que já foi aplicado
    ),
    (
        "deps/libjuice/src/random.c",
        # o ramo do Switch entra antes do ramo genérico que devolve -1
        "#else\nstatic int random_bytes(void *buf, size_t size) {\n\t(void)buf;\n",
        LIBJUICE_RNG,
        "defined(__SWITCH__)",
    ),
]

erros = 0
for arquivo, ancora, inserir, marca in ALTERACOES:
    caminho = ldc / arquivo
    texto = caminho.read_text()
    if marca in texto:
        print(f"ja aplicado: {arquivo}")
        continue
    if texto.count(ancora) < 1:
        print(f"ERRO: trecho esperado nao encontrado em {arquivo}: {ancora!r}")
        erros += 1
        continue
    # Insere antes da PRIMEIRA ocorrência da âncora
    caminho.write_text(texto.replace(ancora, inserir + ancora, 1))
    print(f"aplicado: {arquivo}")

sys.exit(1 if erros else 0)
