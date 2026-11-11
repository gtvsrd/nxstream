// NXStream - diagnóstico do WebRTC no Switch (fase 2b, parte 1)
//
// Monta uma conexão WebRTC local, sem o Xbox, com as mesmas mídias que o
// streaming vai usar (vídeo H.264, áudio Opus e canal de dados), e mede:
//   - geração do certificado DTLS e da oferta SDP (mbedTLS 3 + entropia do Switch)
//   - coleta de candidatos de rede (libjuice + getifaddrs da camada compat/)
// Valida em execução o que até aqui só foi validado compilando.
#pragma once
#include <stdbool.h>

#define RTC_DIAG_MAX_CANDIDATOS 8

typedef struct {
    bool   iniciado;             // conexão criada
    double segundos;             // desde o início

    bool   oferta_pronta;
    double t_oferta;             // segundos até a oferta (inclui o certificado)
    int    tamanho_oferta;       // bytes do SDP
    bool   tem_fingerprint;      // certificado DTLS presente na oferta
    bool   tem_video, tem_audio, tem_dados;

    int    num_candidatos;
    char   candidatos[RTC_DIAG_MAX_CANDIDATOS][128]; // "IP porta tipo"
    bool   coleta_completa;
    double t_coleta;

    char   erro[160];
} RtcDiagEstado;

bool rtc_diag_iniciar(void);              // false se nem conseguiu criar a conexão
void rtc_diag_ler(RtcDiagEstado *copia);  // cópia segura do estado atual
void rtc_diag_encerrar(void);
void rtc_diag_finalizar_biblioteca(void); // ao fechar o app (só se o diagnóstico foi usado)
