// NXStream - desenho do vídeo na tela pela GPU (fase 2c.3)
//
// Usa o deko3d (API de GPU do Switch). Os quadros do NVDEC são lidos direto
// pela GPU, sem cópia: a memória de cada quadro decodificado vira um bloco de
// memória da GPU (luma R8 e croma RG8 no layout de vídeo) e um shader converte
// de YUV para RGB. Por cima, a camada de texto (ui/sobreposicao.h).
//
// Enquanto o vídeo está ativo o console de texto do libnx fica desligado: os dois
// usam a mesma janela. Tudo roda na thread principal.
#pragma once
#include <stdbool.h>

struct AVFrame;

// Liga a GPU e assume a tela (o console de texto precisa estar desligado)
bool video_tela_iniciar(void);
// Desenha um quadro de tela e espera a GPU terminar. `quadro`: novo quadro
// decodificado (a função fica com ele) ou NULL para repetir o último.
// Espera o próximo vsync, então ritma o laço principal em ~60 vezes por segundo.
void video_tela_desenhar(struct AVFrame *quadro);
// Solta os quadros e a GPU (devolve a janela para o console de texto)
void video_tela_encerrar(void);
bool video_tela_ativa(void);
// Nível de preto (teste): 0 = padrão (16), 1 = 24, 2 = 32. Vale a partir do próximo quadro.
void video_tela_ajuste_preto(int nivel);
int video_tela_nivel_preto(void);
// Último erro (para o log e a tela de erro)
const char *video_tela_erro(void);
