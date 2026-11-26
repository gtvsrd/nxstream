# Testes que rodam no PC (sem Switch e sem rede)
CC      ?= gcc
CFLAGS  := -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -g -fsanitize=address,undefined \
           -I../source -I../third_party/cjson
FONTES  := test_parse.c \
           ../source/auth/auth_parse.c \
           ../source/auth/auth_store.c \
           ../source/api/xapi_parse.c \
           ../source/api/xnuvem_parse.c \
           ../source/media/h264_info.c \
           ../third_party/cjson/cJSON.c

.PHONY: all run clean
all: run

test_parse: $(FONTES)
	$(CC) $(CFLAGS) -o $@ $(FONTES)

test_input: test_input.c ../source/input/pacote_input.c
	$(CC) $(CFLAGS) -o $@ test_input.c ../source/input/pacote_input.c

test_montador: test_montador.c ../source/net/montador.c ../source/net/montador.h
	$(CC) $(CFLAGS) -o $@ test_montador.c ../source/net/montador.c

run: test_parse test_input test_montador
	./test_parse
	./test_input
	./test_montador

clean:
	rm -f test_parse test_input test_montador teste_token.json teste_token.json.tmp
