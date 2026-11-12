// NXStream - controle do Switch -> controle do Xbox (fase 3)
//
// Mapeamento por POSIÇÃO (como os botões ficam no controle do Xbox):
//   Switch B (embaixo) -> Xbox A     Switch A (direita) -> Xbox B
//   Switch Y (esquerda) -> Xbox X    Switch X (em cima) -> Xbox Y
//   L/R -> LB/RB, ZL/ZR -> LT/RT (digitais: 0 ou 100%), + -> Menu, - -> View,
//   clique dos analógicos -> LS/RS, direcional -> direcional.
// O botão Xbox (guia): + segurado e clique no - (tratado em main.c), ou pelo menu.
#pragma once
#include <switch.h>
#include "input/pacote_input.h"

void controle_ler(PadState *pad, EstadoControle *saida);
