# Gera imagens das telas da interface no PC: make -C tests/ui
# (a fonte do PC substitui a do Switch, então as letras ficam um pouco diferentes)
FONTE ?= /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf
VERSAO := $(shell sed -n 's/^APP_VERSION := //p' ../../Makefile)

saida: preview
	mkdir -p saida && ./preview $(FONTE)

FORCE:
preview: FORCE

preview: preview.c ../../source/ui/tela.c ../../source/ui/tela.h
	$(CC) -O2 -Wall -I../../source -DAPP_VERSION='"$(VERSAO)"' preview.c ../../source/ui/tela.c -lm -o $@
