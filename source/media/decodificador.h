// NXStream - decodificação H.264 por hardware (fase 2c.2)
//
// Os quadros chegam da thread de rede (conexao.c) e vão para uma fila; uma
// thread própria entrega cada um ao decodificador do FFmpeg com o chip de
// vídeo do Switch (NVDEC, via AV_HWDEVICE_TYPE_NVTEGRA). Assim um quadro mais
// lento de decodificar não atrasa a recepção.
//
// Nesta etapa os quadros decodificados são só contados e medidos; o desenho
// na tela é a fase 2c.3.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool   ativo;               // decodificador aberto e thread rodando
    bool   hardware;            // usando o NVDEC (false = falhou e ficou sem)
    char   descricao[96];       // ex.: "h264 + nvtegra"

    unsigned long enviados;     // quadros entregues ao decodificador
    unsigned long decodificados;// quadros que saíram prontos
    unsigned long erros;        // erros do decodificador
    unsigned long descartados;  // quadros jogados fora (fila cheia ou antes do 1º quadro-chave)
    unsigned long esperando_chave; // quadros descartados esperando quadro-chave

    double ms_ultimo, ms_medio, ms_maximo; // tempo de decodificação (janela recente)
    int    largura, altura;     // do último quadro decodificado
    char   formato[24];         // formato de pixel do quadro decodificado
    int    fila;                // quadros esperando na fila agora
    bool   precisa_chave;       // perdeu quadros: pedir quadro-chave ao console
} DecodEstado;

bool decod_iniciar(void);
// Chamado pela thread de rede para cada quadro H.264 completo (Annex B)
void decod_enviar(const uint8_t *dados, size_t tamanho, bool quadro_chave);
void decod_ler(DecodEstado *copia);
void decod_limpar_pedido_chave(void);
void decod_encerrar(void);
