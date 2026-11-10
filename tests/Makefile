# Testes que rodam no PC (sem Switch e sem rede)
CC      ?= gcc
CFLAGS  := -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -g -fsanitize=address,undefined \
           -I../source -I../third_party/cjson
FONTES  := test_parse.c \
           ../source/auth/auth_parse.c \
           ../source/auth/auth_store.c \
           ../source/api/xapi_parse.c \
           ../third_party/cjson/cJSON.c

.PHONY: all run clean
all: run

test_parse: $(FONTES)
	$(CC) $(CFLAGS) -o $@ $(FONTES)

run: test_parse
	./test_parse

clean:
	rm -f test_parse teste_token.json teste_token.json.tmp
