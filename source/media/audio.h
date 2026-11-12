// NXStream - áudio do console (fase 3)
//
// O áudio chega como pacotes RTP com Opus (48 kHz) na trilha de áudio. Cada
// pacote é decodificado na thread de rede (rápido: ~0,5 ms) e o som vai para
// uma fila curta; uma thread própria entrega blocos de 20 ms à saída de áudio
// do Switch (audout). Se a fila crescer demais (atraso), o excesso é jogado fora;
// se esvaziar, toca silêncio até chegar mais.
//
// Decodificador: libopus quando o build encontra a biblioteca; senão o Opus
// nativo do FFmpeg.
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool     ativo;
    char     descricao[64];     // ex.: "libopus 1.4" ou "FFmpeg opus"
    unsigned long pacotes;      // pacotes RTP de áudio recebidos
    unsigned long perdidos;     // pelos números de sequência
    unsigned long erros;        // falhas do decodificador
    unsigned long faltas;       // blocos tocados em silêncio (fila vazia)
    unsigned long descartados_ms; // áudio jogado fora para não atrasar (só acúmulo grande)
    unsigned long acelerados;   // blocos tocados 1% mais rápido (fila acima do alvo)
    unsigned long desacelerados;// blocos tocados 1% mais devagar (fila abaixo do alvo)
    int      fila_ms;           // áudio esperando para tocar
    int      canais;            // canais no fluxo (1 ou 2)
} AudioEstado;

bool audio_iniciar(void);
// Thread de rede: um pacote RTP (com cabeçalho) da trilha de áudio
void audio_receber_rtp(const uint8_t *dados, int tamanho);
void audio_ler(AudioEstado *copia);
void audio_encerrar(void);
