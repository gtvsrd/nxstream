#!/usr/bin/env python3
"""Aplica as alterações para o Switch no código das dependências.

Uso: python3 compat/patches/aplicar.py <pasta da libdatachannel>

Cada alteração procura um trecho exato do código original e insere texto
antes dele ou o substitui. Se o trecho não for encontrado (a dependência
mudou), o script para com erro em vez de aplicar algo às cegas. Rodar duas
vezes não duplica nada.
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

# libjuice: pede 1 MB de buffer de recepção E 1 MB de envio para o socket UDP.
# Na libnx, todos os sockets do app dividem um poço de memória de ~2,25 MB
# (padrão do socketInitialize): sobra pouco ou nada para o socket TCP do curl,
# e as chamadas HTTPS falham com "Couldn't connect" enquanto a conexão WebRTC
# existe. No Switch: 512 KB para receber (rajadas de vídeo) e 128 KB para enviar.
LIBJUICE_BUFFERS_ORIGINAL = '''\tconst sockopt_t buffer_size = 1 * 1024 * 1024;
\tsetsockopt(sock, SOL_SOCKET, SO_RCVBUF, (const char *)&buffer_size, sizeof(buffer_size));
\tsetsockopt(sock, SOL_SOCKET, SO_SNDBUF, (const char *)&buffer_size, sizeof(buffer_size));
'''
LIBJUICE_BUFFERS_SWITCH = '''#ifdef __SWITCH__
\t/* NXStream: buffers menores, o poco de memoria de sockets da libnx e compartilhado */
\tconst sockopt_t buffer_recv = 512 * 1024;
\tconst sockopt_t buffer_send = 128 * 1024;
\tsetsockopt(sock, SOL_SOCKET, SO_RCVBUF, (const char *)&buffer_recv, sizeof(buffer_recv));
\tsetsockopt(sock, SOL_SOCKET, SO_SNDBUF, (const char *)&buffer_send, sizeof(buffer_send));
#else
''' + LIBJUICE_BUFFERS_ORIGINAL + '''#endif
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
    (
        "deps/libjuice/src/udp.c",
        LIBJUICE_BUFFERS_ORIGINAL,
        LIBJUICE_BUFFERS_SWITCH,
        "NXStream: buffers menores",
        "substituir",
    ),
]

erros = 0
for entrada in ALTERACOES:
    arquivo, ancora, texto_novo, marca = entrada[:4]
    modo = entrada[4] if len(entrada) > 4 else "inserir"
    caminho = ldc / arquivo
    texto = caminho.read_text()
    if marca in texto:
        print(f"ja aplicado: {arquivo}")
        continue
    if texto.count(ancora) < 1:
        print(f"ERRO: trecho esperado nao encontrado em {arquivo}: {ancora!r}")
        erros += 1
        continue
    if modo == "substituir":
        caminho.write_text(texto.replace(ancora, texto_novo, 1))
    else:  # insere antes da PRIMEIRA ocorrência da âncora
        caminho.write_text(texto.replace(ancora, texto_novo + ancora, 1))
    print(f"aplicado: {arquivo}")

sys.exit(1 if erros else 0)
