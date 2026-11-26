// NXStream - capas dos jogos da nuvem (v0.13.0)
//
// A tela pede a capa de cada jogo visível a cada quadro (capas_pegar). Se ela
// já estiver na memória, volta na hora; senão, duas threads buscam no SD
// (NX_PASTA_CAPAS) ou baixam do catálogo já no tamanho certo (~15 KB em JPEG),
// guardam no SD e decodificam. As mais pedidas recentemente vêm primeiro; as
// que saíram da tela há mais de meio segundo deixam de ser buscadas.
//
// Tudo é chamado pela thread principal, exceto o trabalho interno das threads.
// Um ponteiro devolvido vale até o próximo capas_novo_quadro() + 1 quadro.
#pragma once
#include <stdint.h>

void capas_iniciar(void);
void capas_encerrar(void);
// Uma vez por quadro, antes dos capas_pegar desse quadro
void capas_novo_quadro(void);
// RGBA UI_CAPA_L x UI_CAPA_A, ou NULL (ainda não chegou ou não existe)
const uint32_t *capas_pegar(const char *produto_id, const char *url);
