// Montador de quadros H.264 a partir de RTP: veja montador.h
#include "montador.h"

#include <stdlib.h>
#include <string.h>

#define JANELA    1024   // pacotes guardados (potência de 2)
#define CARGA_MAX 1500   // maior carga útil RTP aceita
#define FCI_MAX   64     // entradas por NACK

typedef struct {
    bool presente;
    bool faltando;       // detectado como perdido e ainda não chegou
    bool marcador;
    uint16_t seq;
    uint16_t tam;
    uint8_t nacks;
    uint32_t ts;
    double t_falta, t_nack;
    uint8_t carga[CARGA_MAX];
} Slot;

struct Montador {
    MontadorSaida saida;
    Slot *slots;
    bool iniciado;
    uint16_t proximo;    // 1º número de sequência do próximo quadro a entregar
    uint16_t maior;      // maior número de sequência visto
    uint32_t ssrc;
    bool esperando_chave;
    bool ressincronizar; // perdeu o começo de um quadro: espera um marcador
    int faltando;        // slots marcados como faltando (atalho para o NACK)
    double t_pli;
    uint8_t *quadro;
    size_t cap;
    MontadorEstat est;
};

static inline int16_t dif(uint16_t a, uint16_t b) {
    return (int16_t)(uint16_t)(a - b);
}

static inline Slot *slot(Montador *m, uint16_t seq) {
    return &m->slots[seq & (JANELA - 1)];
}

Montador *montador_criar(MontadorSaida saida) {
    Montador *m = calloc(1, sizeof *m);
    if (!m)
        return NULL;
    m->slots = calloc(JANELA, sizeof *m->slots);
    m->cap = 512 * 1024;
    m->quadro = malloc(m->cap);
    if (!m->slots || !m->quadro) {
        montador_liberar(m);
        return NULL;
    }
    m->saida = saida;
    m->t_pli = -1e9;
    return m;
}

void montador_liberar(Montador *m) {
    if (!m)
        return;
    free(m->slots);
    free(m->quadro);
    free(m);
}

void montador_estat(const Montador *m, MontadorEstat *e) {
    *e = m->est;
}

static void limpar(Montador *m, uint16_t seq) {
    Slot *s = slot(m, seq);
    if (s->seq == seq) {
        if (s->faltando)
            m->faltando--;
        s->presente = s->faltando = false;
    }
}

// Descarta [de, ate] (inclusive)
static void descartar(Montador *m, uint16_t de, uint16_t ate) {
    for (uint16_t s = de; dif(s, ate) <= 0; s++)
        limpar(m, s);
}

static void pedir_chave(Montador *m, double agora) {
    if (agora - m->t_pli < MONTADOR_PLI_INTERVALO)
        return;
    m->t_pli = agora;
    m->est.pedidos_chave++;
    if (m->saida.pedir_chave)
        m->saida.pedir_chave(m->saida.u);
}

static bool garantir(Montador *m, size_t extra, size_t usado) {
    if (usado + extra <= m->cap)
        return true;
    size_t nova = m->cap * 2;
    while (nova < usado + extra)
        nova *= 2;
    uint8_t *q = realloc(m->quadro, nova);
    if (!q)
        return false;
    m->quadro = q;
    m->cap = nova;
    return true;
}

static size_t por_nal(Montador *m, size_t n, const uint8_t *nal, size_t tam, bool *idr) {
    static const uint8_t inicio[4] = {0, 0, 0, 1};
    if (!tam || !garantir(m, tam + 4, n))
        return n;
    int tipo = nal[0] & 0x1F;
    if (tipo == 5 || tipo == 7)
        *idr = true;
    memcpy(m->quadro + n, inicio, 4);
    memcpy(m->quadro + n + 4, nal, tam);
    return n + 4 + tam;
}

// Junta as cargas de [de, ate] num quadro Annex B e entrega
static void entregar(Montador *m, uint16_t de, uint16_t ate) {
    size_t n = 0;
    bool idr = false;
    uint32_t ts = slot(m, ate)->ts;
    for (uint16_t seq = de; dif(seq, ate) <= 0; seq++) {
        Slot *s = slot(m, seq);
        const uint8_t *p = s->carga;
        size_t t = s->tam;
        if (t < 1)
            continue; // só preenchimento (sonda de banda)
        int tipo = p[0] & 0x1F;
        if (tipo >= 1 && tipo <= 23) {
            n = por_nal(m, n, p, t, &idr);
        } else if (tipo == 24) { // STAP-A
            size_t i = 1;
            while (i + 2 <= t) {
                size_t len = (size_t)p[i] << 8 | p[i + 1];
                i += 2;
                if (i + len > t)
                    break;
                n = por_nal(m, n, p + i, len, &idr);
                i += len;
            }
        } else if (tipo == 28 && t >= 2) { // FU-A
            if (p[1] & 0x80) { // começo: refaz o cabeçalho da NAL
                uint8_t cab = (uint8_t)((p[0] & 0xE0) | (p[1] & 0x1F));
                n = por_nal(m, n, &cab, 1, &idr);
            }
            if (garantir(m, t - 2, n)) {
                memcpy(m->quadro + n, p + 2, t - 2);
                n += t - 2;
            }
        }
    }
    descartar(m, de, ate);
    m->proximo = (uint16_t)(ate + 1);

    if (m->esperando_chave && !idr) {
        m->est.esperando_chave++;
        return;
    }
    m->esperando_chave = false;
    if (n && m->saida.quadro) {
        m->est.quadros++;
        m->saida.quadro(m->quadro, n, ts, idr, m->saida.u);
    }
}

static void enviar_nacks(Montador *m, double agora) {
    if (!m->faltando || !m->saida.rtcp)
        return;
    uint8_t pacote[12 + 4 * FCI_MAX];
    int fci = 0;
    int32_t pid = -1; // PID atual da entrada em montagem
    uint16_t blp = 0;
    for (uint16_t seq = m->proximo; dif(seq, m->maior) <= 0; seq++) {
        Slot *s = slot(m, seq);
        if (!(s->faltando && s->seq == seq) || s->nacks >= MONTADOR_NACK_MAX)
            continue;
        double espera = s->nacks ? MONTADOR_NACK_REPETIR : MONTADOR_NACK_PRIMEIRO;
        if (agora - (s->nacks ? s->t_nack : s->t_falta) < espera)
            continue;
        s->nacks++;
        s->t_nack = agora;
        if (pid >= 0 && dif(seq, (uint16_t)pid) >= 1 && dif(seq, (uint16_t)pid) <= 16) {
            blp |= (uint16_t)(1u << (dif(seq, (uint16_t)pid) - 1));
            continue;
        }
        if (pid >= 0) { // fecha a entrada anterior
            uint8_t *f = pacote + 12 + 4 * fci++;
            f[0] = (uint8_t)(pid >> 8); f[1] = (uint8_t)pid; f[2] = (uint8_t)(blp >> 8); f[3] = (uint8_t)blp;
            if (fci == FCI_MAX)
                break;
        }
        pid = seq;
        blp = 0;
    }
    if (pid >= 0 && fci < FCI_MAX) {
        uint8_t *f = pacote + 12 + 4 * fci++;
        f[0] = (uint8_t)(pid >> 8); f[1] = (uint8_t)pid; f[2] = (uint8_t)(blp >> 8); f[3] = (uint8_t)blp;
    }
    if (!fci)
        return;
    // RTPFB (205), FMT 1 = NACK genérico. Tamanho em palavras de 32 bits - 1.
    uint16_t palavras = (uint16_t)(2 + fci);
    pacote[0] = 0x81;
    pacote[1] = 205;
    pacote[2] = (uint8_t)(palavras >> 8);
    pacote[3] = (uint8_t)palavras;
    memset(pacote + 4, 0, 4); // SSRC de quem envia: não temos trilha de envio de vídeo
    pacote[4 + 3] = 1;
    pacote[8] = (uint8_t)(m->ssrc >> 24); pacote[9] = (uint8_t)(m->ssrc >> 16);
    pacote[10] = (uint8_t)(m->ssrc >> 8); pacote[11] = (uint8_t)m->ssrc;
    m->est.nacks++;
    m->saida.rtcp(pacote, 12 + 4 * (size_t)fci, m->saida.u);
}

// Entrega o que estiver completo; desiste de pacotes que não vieram a tempo
static void avancar(Montador *m, double agora) {
    for (;;) {
        if (dif(m->maior, m->proximo) < 0)
            return; // nada guardado

        if (m->ressincronizar) { // o próximo quadro começa depois de um marcador
            bool achou = false;
            for (uint16_t seq = m->proximo; dif(seq, m->maior) <= 0; seq++) {
                Slot *s = slot(m, seq);
                if (s->presente && s->seq == seq && s->marcador) {
                    descartar(m, m->proximo, seq);
                    m->proximo = (uint16_t)(seq + 1);
                    m->ressincronizar = false;
                    achou = true;
                    break;
                }
            }
            if (!achou)
                return;
            continue;
        }

        bool entregou = false;
        for (uint16_t seq = m->proximo; dif(seq, m->maior) <= 0; seq++) {
            Slot *s = slot(m, seq);
            if (s->presente && s->seq == seq) {
                if (s->marcador) {
                    entregar(m, m->proximo, seq);
                    entregou = true;
                    break;
                }
                continue;
            }
            // Falta este pacote
            bool vencido = s->faltando && s->seq == seq && agora - s->t_falta >= MONTADOR_DESISTIR;
            bool cheio = dif(m->maior, m->proximo) >= JANELA - 64;
            if (!vencido && !cheio)
                return; // ainda pode chegar
            m->est.quadros_perdidos++;
            m->esperando_chave = true;
            pedir_chave(m, agora);
            // Pula até depois do próximo marcador presente (fim do quadro quebrado)
            m->ressincronizar = true;
            descartar(m, m->proximo, seq);
            m->proximo = (uint16_t)(seq + 1);
            entregou = true; // volta ao laço externo, agora ressincronizando
            break;
        }
        if (!entregou)
            return;
    }
}

void montador_pacote(Montador *m, const uint8_t *p, size_t n, double agora) {
    if (n < 12 || (p[0] >> 6) != 2)
        return;
    if ((p[1] & 0x7F) >= 64 && (p[1] & 0x7F) <= 95)
        return; // RTCP (SR/RR...), não RTP
    size_t off = 12 + 4 * (size_t)(p[0] & 0x0F);
    if (p[0] & 0x10) { // extensão de cabeçalho
        if (off + 4 > n)
            return;
        off += 4 + 4 * ((size_t)p[off + 2] << 8 | p[off + 3]);
    }
    size_t preench = (p[0] & 0x20) ? p[n - 1] : 0;
    if (off + preench > n)
        return;
    size_t carga = n - off - preench;
    if (carga > CARGA_MAX)
        return;
    bool marcador = p[1] & 0x80;
    uint16_t seq = (uint16_t)(p[2] << 8 | p[3]);
    uint32_t ts = (uint32_t)p[4] << 24 | (uint32_t)p[5] << 16 | (uint32_t)p[6] << 8 | p[7];
    uint32_t ssrc = (uint32_t)p[8] << 24 | (uint32_t)p[9] << 16 | (uint32_t)p[10] << 8 | p[11];
    m->est.pacotes++;

    if (!m->iniciado) {
        m->iniciado = true;
        m->proximo = seq;
        m->maior = (uint16_t)(seq - 1);
        m->esperando_chave = true; // sem quadro-chave o decodificador não começa
    }
    m->ssrc = ssrc;

    int16_t d = dif(seq, m->proximo);
    if (d < 0) { // já entregue ou descartado
        m->est.atrasados++;
        return;
    }
    if (d >= JANELA - 1) { // salto grande (servidor recomeçou a sequência?): recomeça
        descartar(m, m->proximo, m->maior);
        m->proximo = seq;
        m->maior = (uint16_t)(seq - 1);
        m->esperando_chave = true;
        m->ressincronizar = false;
        pedir_chave(m, agora);
    }

    Slot *s = slot(m, seq);
    if (s->presente && s->seq == seq) {
        m->est.duplicados++;
        return;
    }
    if (s->faltando && s->seq == seq) {
        m->faltando--;
        if (s->nacks)
            m->est.recuperados++;
        else
            m->est.fora_de_ordem++;
    } else if (dif(seq, m->maior) < 0) {
        m->est.fora_de_ordem++;
    }
    s->presente = true;
    s->faltando = false;
    s->marcador = marcador;
    s->seq = seq;
    s->ts = ts;
    s->tam = (uint16_t)carga;
    s->nacks = 0;
    memcpy(s->carga, p + off, carga);

    int16_t dm = dif(seq, m->maior);
    if (dm > 1) { // buraco: marca os que faltam
        for (uint16_t f = (uint16_t)(m->maior + 1); f != seq; f++) {
            Slot *x = slot(m, f);
            x->presente = false;
            x->faltando = true;
            x->seq = f;
            x->nacks = 0;
            x->t_falta = agora;
            x->t_nack = 0;
            m->faltando++;
            m->est.perdidos++;
        }
    }
    if (dm > 0)
        m->maior = seq;

    enviar_nacks(m, agora);
    avancar(m, agora);
}
