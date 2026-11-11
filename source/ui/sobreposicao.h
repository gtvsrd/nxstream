// NXStream - texto por cima do vídeo (fase 2c.3)
//
// Um "terminal" de 80x45 caracteres, igual ao console do libnx (mesma fonte
// 16x16 e mesmas cores ANSI), que é desenhado numa imagem RGBA 1280x720 e
// colocado por cima do vídeo. Serve para as estatísticas, o menu e os avisos.
//
// Uso: sobre_limpar(); sobre_printf(...); depois sobre_renderizar() só quando
// sobre_mudou() for true (redesenhar 1280x720 a cada quadro seria desperdício).
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define SOBRE_COLUNAS 80
#define SOBRE_LINHAS  45
#define SOBRE_LARGURA (SOBRE_COLUNAS * 16)
#define SOBRE_ALTURA  (SOBRE_LINHAS * 16)

// Apaga tudo. `fundo` (0-255) escurece a tela inteira por trás do texto:
// 0 = só as linhas com texto ganham fundo escuro; >0 = véu sobre o vídeo todo.
void sobre_limpar(uint8_t fundo);
// Escreve como o printf do console: entende \n e as cores \x1b[...m usadas no app.
void sobre_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
// Move o cursor (coluna e linha a partir de 0)
void sobre_cursor(int coluna, int linha);
// Há algo para mostrar? (false = não desenhar a camada)
bool sobre_visivel(void);
// O conteúdo mudou desde a última renderização?
bool sobre_mudou(void);
// Desenha o conteúdo em pixels RGBA (0xAABBGGRR), `passo` em pixels por linha.
void sobre_renderizar(uint32_t *pixels, int passo);
