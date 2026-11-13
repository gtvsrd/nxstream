// NXStream - interface gráfica simples (ver tela.h)
#include "ui/tela.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "../../third_party/stb/stb_truetype.h"
#pragma GCC diagnostic pop

#ifndef APP_VERSION
#define APP_VERSION "?"
#endif

// ---------------------------------------------------------------------------
// Cores (formato do framebuffer RGBA8888: 0xAABBGGRR)
// ---------------------------------------------------------------------------
#define RGB(r, g, b) (0xFF000000u | ((uint32_t)(b) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(r))
#define COR_FUNDO_TOPO  RGB(0x1C, 0x1C, 0x1C)
#define COR_FUNDO_BASE  RGB(0x0C, 0x0C, 0x0C)
#define COR_VERDE       RGB(0x10, 0x7C, 0x10)  // verde do Xbox
#define COR_VERDE_CLARO RGB(0x5D, 0xC2, 0x1E)  // verde para texto no fundo escuro
#define COR_TEXTO       RGB(0xFF, 0xFF, 0xFF)
#define COR_TEXTO2      RGB(0xB8, 0xB8, 0xB8)
#define COR_TEXTO3      RGB(0x80, 0x80, 0x80)
#define COR_CARTAO      RGB(0x22, 0x22, 0x22)
#define COR_CARTAO_SEL  RGB(0x2C, 0x2C, 0x2C)
#define COR_LINHA       RGB(0x30, 0x30, 0x30)
#define COR_ERRO        RGB(0xE8, 0x11, 0x23)
#define COR_AVISO       RGB(0xFF, 0xB9, 0x00)
#define COR_BOTAO       RGB(0xE6, 0xE6, 0xE6)
#define COR_ESCURO      RGB(0x10, 0x10, 0x10)

// ---------------------------------------------------------------------------
// Tela atual (o que ui_desenhar mostra)
// ---------------------------------------------------------------------------
typedef enum { T_NADA, T_STATUS, T_CODIGO, T_MENSAGEM, T_LISTA } TipoTela;

static struct {
    TipoTela tipo;
    UiTipo   icone;
    char titulo[160], status[160], detalhe[512], rodape[160];
    char aviso[160], endereco[96], codigo[32];
    UiItem itens[UI_MAX_ITENS];
    int total, selecionado;
} g_t;

static void copiar(char *dst, size_t tam, const char *src) {
    snprintf(dst, tam, "%s", src ? src : "");
}

void ui_status(const char *titulo, const char *status, const char *detalhe, const char *rodape) {
    g_t.tipo = T_STATUS;
    copiar(g_t.titulo, sizeof g_t.titulo, titulo);
    copiar(g_t.status, sizeof g_t.status, status);
    copiar(g_t.detalhe, sizeof g_t.detalhe, detalhe);
    copiar(g_t.rodape, sizeof g_t.rodape, rodape);
}

void ui_codigo(const char *aviso, const char *endereco, const char *codigo, const char *status,
               const char *rodape) {
    g_t.tipo = T_CODIGO;
    copiar(g_t.aviso, sizeof g_t.aviso, aviso);
    copiar(g_t.endereco, sizeof g_t.endereco, endereco);
    copiar(g_t.codigo, sizeof g_t.codigo, codigo);
    copiar(g_t.status, sizeof g_t.status, status);
    copiar(g_t.rodape, sizeof g_t.rodape, rodape);
}

void ui_mensagem(UiTipo tipo, const char *titulo, const char *detalhe, const char *rodape) {
    g_t.tipo = T_MENSAGEM;
    g_t.icone = tipo;
    copiar(g_t.titulo, sizeof g_t.titulo, titulo);
    copiar(g_t.detalhe, sizeof g_t.detalhe, detalhe);
    copiar(g_t.rodape, sizeof g_t.rodape, rodape);
}

void ui_lista(const char *titulo, const char *subtitulo, const UiItem *itens, int total, int selecionado,
              const char *mensagem, const char *rodape) {
    g_t.tipo = T_LISTA;
    copiar(g_t.titulo, sizeof g_t.titulo, titulo);
    copiar(g_t.status, sizeof g_t.status, subtitulo);
    copiar(g_t.detalhe, sizeof g_t.detalhe, mensagem);
    copiar(g_t.rodape, sizeof g_t.rodape, rodape);
    g_t.total = total < 0 ? 0 : total > UI_MAX_ITENS ? UI_MAX_ITENS : total;
    if (g_t.total)
        memcpy(g_t.itens, itens, sizeof(UiItem) * g_t.total);
    g_t.selecionado = selecionado;
}

// ---------------------------------------------------------------------------
// Desenho básico
// ---------------------------------------------------------------------------
static uint32_t *g_px;
static int g_passo;

static inline void misturar(int x, int y, uint32_t cor, float a) {
    if (x < 0 || y < 0 || x >= UI_LARGURA || y >= UI_ALTURA || a <= 0.0f)
        return;
    uint32_t *p = &g_px[y * g_passo + x];
    if (a >= 1.0f) {
        *p = cor;
        return;
    }
    uint32_t d = *p;
    int ia = (int)(a * 256.0f);
    uint32_t r = ((cor & 0xFF) * ia + (d & 0xFF) * (256 - ia)) >> 8;
    uint32_t g = (((cor >> 8) & 0xFF) * ia + ((d >> 8) & 0xFF) * (256 - ia)) >> 8;
    uint32_t b = (((cor >> 16) & 0xFF) * ia + ((d >> 16) & 0xFF) * (256 - ia)) >> 8;
    *p = 0xFF000000u | (b << 16) | (g << 8) | r;
}

static uint32_t interpolar(uint32_t c1, uint32_t c2, float t) {
    uint32_t r = (uint32_t)((c1 & 0xFF) * (1 - t) + (c2 & 0xFF) * t);
    uint32_t g = (uint32_t)(((c1 >> 8) & 0xFF) * (1 - t) + ((c2 >> 8) & 0xFF) * t);
    uint32_t b = (uint32_t)(((c1 >> 16) & 0xFF) * (1 - t) + ((c2 >> 16) & 0xFF) * t);
    return 0xFF000000u | (b << 16) | (g << 8) | r;
}

static void fundo(void) {
    for (int y = 0; y < UI_ALTURA; y++) {
        uint32_t c = interpolar(COR_FUNDO_TOPO, COR_FUNDO_BASE, (float)y / UI_ALTURA);
        uint32_t *l = &g_px[y * g_passo];
        for (int x = 0; x < UI_LARGURA; x++)
            l[x] = c;
    }
}

static void preencher(int x, int y, int w, int h, uint32_t cor) {
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            misturar(i, j, cor, 1.0f);
}

static float limitar(float v) {
    return v < 0 ? 0 : v > 1 ? 1 : v;
}

// Círculo cheio com borda suave
static void circulo(float cx, float cy, float r, uint32_t cor, float alfa) {
    int x0 = (int)(cx - r - 1), x1 = (int)(cx + r + 2), y0 = (int)(cy - r - 1), y1 = (int)(cy + r + 2);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float cobre = limitar(r + 0.5f - sqrtf(dx * dx + dy * dy));
            if (cobre > 0)
                misturar(x, y, cor, cobre * alfa);
        }
}

static void anel(float cx, float cy, float r, float espessura, uint32_t cor, float alfa) {
    float ext = r + espessura / 2, in = r - espessura / 2;
    int x0 = (int)(cx - ext - 1), x1 = (int)(cx + ext + 2), y0 = (int)(cy - ext - 1), y1 = (int)(cy + ext + 2);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float d = sqrtf(dx * dx + dy * dy);
            float cobre = limitar(ext + 0.5f - d) * limitar(d - in + 0.5f);
            if (cobre > 0)
                misturar(x, y, cor, cobre * alfa);
        }
}

static void retangulo(int x, int y, int w, int h, float raio, uint32_t cor) {
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++) {
            // distância até o canto arredondado mais próximo
            float px = i + 0.5f, py = j + 0.5f;
            float cx = px < x + raio ? x + raio : px > x + w - raio ? x + w - raio : px;
            float cy = py < y + raio ? y + raio : py > y + h - raio ? y + h - raio : py;
            float dx = px - cx, dy = py - cy;
            float cobre = limitar(raio + 0.5f - sqrtf(dx * dx + dy * dy));
            if (cobre > 0)
                misturar(i, j, cor, cobre);
        }
}

// Triângulo de "play" apontando para a direita, centrado em (cx, cy)
static void triangulo(float cx, float cy, float tam, uint32_t cor, float alfa) {
    float ax = cx - tam * 0.38f, ay = cy - tam * 0.5f;
    float bx = cx - tam * 0.38f, by = cy + tam * 0.5f;
    float qx = cx + tam * 0.5f, qy = cy;
    float vx[3] = {ax, bx, qx}, vy[3] = {ay, by, qy};
    for (int y = (int)(cy - tam * 0.5f) - 1; y <= (int)(cy + tam * 0.5f) + 1; y++)
        for (int x = (int)(ax) - 1; x <= (int)(qx) + 1; x++) {
            float px = x + 0.5f, py = y + 0.5f, menor = 1e9f;
            for (int k = 0; k < 3; k++) {
                float x1 = vx[k], y1 = vy[k], x2 = vx[(k + 1) % 3], y2 = vy[(k + 1) % 3];
                float ex = x2 - x1, ey = y2 - y1, len = sqrtf(ex * ex + ey * ey);
                // distância com sinal até a aresta (positiva do lado de dentro)
                float d = ((px - x1) * ey - (py - y1) * ex) / len;
                if (d < menor)
                    menor = d;
            }
            float cobre = limitar(menor + 0.5f);
            if (cobre > 0)
                misturar(x, y, cor, cobre * alfa);
        }
}

// ---------------------------------------------------------------------------
// Texto (stb_truetype + cache de glifos)
// ---------------------------------------------------------------------------
static stbtt_fontinfo g_fonte;
static bool g_tem_fonte = false;

void ui_definir_fonte(const unsigned char *ttf) {
    g_tem_fonte = ttf && stbtt_InitFont(&g_fonte, ttf, stbtt_GetFontOffsetForIndex(ttf, 0));
}

typedef struct {
    int cp, tam;
    int w, h, xoff, yoff, avanco;
    unsigned char *bmp;
} Glifo;

#define CACHE 1024
static Glifo g_cache[CACHE];

static const Glifo *glifo(int cp, int tam) {
    unsigned idx = ((unsigned)cp * 31u + (unsigned)tam * 131u) % CACHE;
    for (int n = 0; n < CACHE; n++, idx = (idx + 1) % CACHE) {
        Glifo *g = &g_cache[idx];
        if (g->tam == tam && g->cp == cp)
            return g;
        if (g->tam == 0) {
            float esc = stbtt_ScaleForPixelHeight(&g_fonte, (float)tam);
            int adv, lsb;
            stbtt_GetCodepointHMetrics(&g_fonte, cp, &adv, &lsb);
            g->cp = cp;
            g->tam = tam;
            g->avanco = (int)lroundf(adv * esc);
            g->bmp = stbtt_GetCodepointBitmap(&g_fonte, esc, esc, cp, &g->w, &g->h, &g->xoff, &g->yoff);
            return g;
        }
    }
    return NULL; // cache cheio (não deve acontecer com poucos tamanhos)
}

static int proximo_cp(const char **s) {
    const unsigned char *p = (const unsigned char *)*s;
    int cp;
    if (p[0] < 0x80) { cp = p[0]; *s += 1; }
    else if ((p[0] & 0xE0) == 0xC0 && p[1]) { cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F); *s += 2; }
    else if ((p[0] & 0xF0) == 0xE0 && p[1] && p[2]) {
        cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); *s += 3;
    } else { cp = '?'; *s += 1; }
    return cp;
}

static int subida(int tam) { // da linha de cima até a base do texto
    if (!g_tem_fonte)
        return tam;
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&g_fonte, &asc, &desc, &gap);
    return (int)lroundf(asc * stbtt_ScaleForPixelHeight(&g_fonte, (float)tam));
}

// Largura de `n` bytes do texto (n < 0: até o fim ou a quebra de linha)
static int largura_n(int tam, const char *s, int n, int espaco) {
    if (!g_tem_fonte)
        return 0;
    const char *fim = n < 0 ? s + strlen(s) : s + n;
    int w = 0;
    while (s < fim && *s && *s != '\n') {
        const Glifo *g = glifo(proximo_cp(&s), tam);
        if (g)
            w += g->avanco + espaco;
    }
    return w;
}

static int largura(int tam, const char *s) {
    return largura_n(tam, s, -1, 0);
}

// Escreve uma linha. alinhar: 0 = esquerda em x, 1 = centro em x, 2 = direita em x
static int texto_n(int x, int y, int tam, uint32_t cor, const char *s, int n, int alinhar, int espaco) {
    if (!g_tem_fonte)
        return 0;
    int w = largura_n(tam, s, n, espaco);
    if (alinhar == 1) x -= w / 2;
    else if (alinhar == 2) x -= w;
    int base = y + subida(tam);
    const char *fim = n < 0 ? s + strlen(s) : s + n;
    while (s < fim && *s && *s != '\n') {
        const Glifo *g = glifo(proximo_cp(&s), tam);
        if (!g)
            continue;
        for (int j = 0; j < g->h; j++)
            for (int i = 0; i < g->w; i++) {
                unsigned char a = g->bmp[j * g->w + i];
                if (a)
                    misturar(x + g->xoff + i, base + g->yoff + j, cor, a / 255.0f);
            }
        x += g->avanco + espaco;
    }
    return w;
}

static int texto(int x, int y, int tam, uint32_t cor, const char *s, int alinhar) {
    return texto_n(x, y, tam, cor, s, -1, alinhar, 0);
}

// Texto com quebra de linha automática (e nos \n). Retorna a altura usada.
static int paragrafo(int x, int y, int largura_max, int tam, uint32_t cor, const char *s, int alinhar) {
    int altura_linha = tam * 135 / 100, linhas = 0;
    while (*s) {
        // pega o maior trecho que cabe, quebrando em espaço
        const char *p = s, *ultimo_espaco = NULL;
        while (*p && *p != '\n') {
            const char *q = p;
            proximo_cp(&q);
            if (largura_n(tam, s, (int)(q - s), 0) > largura_max && ultimo_espaco)
                break;
            if (*p == ' ')
                ultimo_espaco = p;
            p = q;
        }
        const char *fim = (*p && *p != '\n' && ultimo_espaco) ? ultimo_espaco : p;
        texto_n(x, y + linhas * altura_linha, tam, cor, s, (int)(fim - s), alinhar, 0);
        linhas++;
        s = fim;
        if (*s == ' ' || *s == '\n')
            s++;
    }
    return linhas * altura_linha;
}

static int altura_paragrafo(int largura_max, int tam, const char *s) {
    // mesma conta de paragrafo(), sem desenhar
    int linhas = 0;
    while (*s) {
        const char *p = s, *ultimo_espaco = NULL;
        while (*p && *p != '\n') {
            const char *q = p;
            proximo_cp(&q);
            if (largura_n(tam, s, (int)(q - s), 0) > largura_max && ultimo_espaco)
                break;
            if (*p == ' ')
                ultimo_espaco = p;
            p = q;
        }
        const char *fim = (*p && *p != '\n' && ultimo_espaco) ? ultimo_espaco : p;
        linhas++;
        s = fim;
        if (*s == ' ' || *s == '\n')
            s++;
    }
    return linhas * (tam * 135 / 100);
}

// ---------------------------------------------------------------------------
// Partes comuns
// ---------------------------------------------------------------------------
static void simbolo(float cx, float cy, float r) {
    circulo(cx, cy, r, COR_VERDE, 1.0f);
    triangulo(cx + r * 0.08f, cy, r * 0.9f, COR_TEXTO, 1.0f);
}

static void cabecalho(void) {
    simbolo(66, 50, 17);
    texto(94, 33, 28, COR_TEXTO, "NXStream", 0);
    texto(1216, 39, 18, COR_TEXTO3, "v" APP_VERSION, 2);
}

static void botao(int cx, int cy, const char *nome) {
    circulo((float)cx, (float)cy, 15, COR_BOTAO, 1.0f);
    if (!strcmp(nome, "+") || !strcmp(nome, "-")) { // desenhados: a fonte varia muito nesses
        preencher(cx - 7, cy - 1, 14, 3, COR_ESCURO);
        if (nome[0] == '+')
            preencher(cx - 1, cy - 7, 3, 14, COR_ESCURO);
    } else {
        texto(cx, cy - 11, 19, COR_ESCURO, nome, 1);
    }
}

// "A=Conectar|B=Voltar": botões alinhados à direita, como no Switch
static void rodape(const char *r) {
    if (!r || !r[0])
        return;
    preencher(48, 648, UI_LARGURA - 96, 1, COR_LINHA);
    char copia[160];
    copiar(copia, sizeof copia, r);
    char *partes[8];
    int n = 0;
    for (char *p = strtok(copia, "|"); p && n < 8; p = strtok(NULL, "|"))
        partes[n++] = p;
    int x = UI_LARGURA - 64;
    for (int i = n - 1; i >= 0; i--) {
        char *igual = strchr(partes[i], '=');
        if (!igual)
            continue;
        *igual = '\0';
        const char *rotulo = igual + 1;
        int w = largura(22, rotulo);
        texto(x - w, 670, 22, COR_TEXTO2, rotulo, 0);
        botao(x - w - 26, 684, partes[i]);
        x -= w + 26 + 15 + 36;
    }
}

// ---------------------------------------------------------------------------
// Telas
// ---------------------------------------------------------------------------
static void tela_status(double t) {
    const float cx = UI_LARGURA / 2.0f, cy = 270, r = 62;
    // Duas ondas saindo do símbolo e o símbolo "respirando"
    for (int k = 0; k < 2; k++) {
        float p = (float)fmod(t / 2.0 + k * 0.5, 1.0);
        anel(cx, cy, r * (1.0f + 1.1f * p), 3.0f, COR_VERDE_CLARO, 0.45f * (1 - p) * (1 - p));
    }
    float pulso = 1.0f + 0.045f * (float)sin(t * 2 * M_PI / 1.6);
    circulo(cx, cy, r * pulso + 10, COR_VERDE, 0.18f);
    simbolo(cx, cy, r * pulso);

    int y = 392;
    if (g_t.titulo[0]) {
        texto((int)cx, y, 40, COR_TEXTO, g_t.titulo, 1);
        y += 58;
    }
    if (g_t.status[0]) {
        texto((int)cx, y, 28, COR_VERDE_CLARO, g_t.status, 1);
        y += 48;
    }
    if (g_t.detalhe[0])
        paragrafo((int)cx, y, 900, 22, COR_TEXTO2, g_t.detalhe, 1);
}

static void tela_codigo(double t) {
    const int cx = UI_LARGURA / 2;
    int y = 108;
    if (g_t.aviso[0]) {
        texto(cx, y, 22, COR_AVISO, g_t.aviso, 1);
    }
    texto(cx, 140, 40, COR_TEXTO, "Entre com sua conta Microsoft", 1);
    texto(cx, 214, 24, COR_TEXTO2, "No celular ou no computador, acesse", 1);
    texto(cx, 250, 46, COR_TEXTO, g_t.endereco, 1);
    texto(cx, 326, 24, COR_TEXTO2, "e digite o código", 1);

    const int w = 540, h = 116, x = cx - w / 2, yc = 368;
    retangulo(x, yc, w, h, 18, COR_VERDE);
    retangulo(x + 3, yc + 3, w - 6, h - 6, 15, COR_CARTAO);
    texto_n(cx + 4, yc + 22, 64, COR_TEXTO, g_t.codigo, -1, 1, 8);

    texto(cx, 508, 22, COR_TEXTO2, "Depois, entre com a conta do seu Xbox e confirme.", 1);

    if (g_t.status[0]) {
        int ws = largura(22, g_t.status);
        float a = 0.55f + 0.45f * (float)sin(t * 2 * M_PI / 1.4);
        circulo((float)(cx - ws / 2 - 18), 578, 6, COR_VERDE_CLARO, a);
        texto(cx + 0, 566, 22, COR_TEXTO3, g_t.status, 1);
    }
}

static void tela_mensagem(void) {
    const int cx = UI_LARGURA / 2;
    uint32_t cor = g_t.icone == UI_ERRO ? COR_ERRO : g_t.icone == UI_AVISO ? COR_AVISO : COR_VERDE;
    // Bloco centrado na vertical entre o cabeçalho e o rodapé
    int h_titulo = altura_paragrafo(1000, 34, g_t.titulo);
    int h_detalhe = g_t.detalhe[0] ? altura_paragrafo(920, 24, g_t.detalhe) + 22 : 0;
    int total = 96 + 34 + h_titulo + h_detalhe;
    int y = 90 + (560 - total) / 2;
    circulo((float)cx, (float)(y + 48), 46, cor, 1.0f);
    uint32_t cor_simbolo = g_t.icone == UI_AVISO ? COR_ESCURO : COR_TEXTO;
    if (g_t.icone == UI_INFO) {
        texto(cx, y + 20, 52, cor_simbolo, "?", 1);
    } else { // "!" desenhado
        retangulo(cx - 5, y + 20, 10, 36, 4, cor_simbolo);
        circulo((float)cx, (float)(y + 68), 6, cor_simbolo, 1.0f);
    }
    y += 96 + 34;
    paragrafo(cx, y, 1000, 34, COR_TEXTO, g_t.titulo, 1);
    y += h_titulo + 22;
    if (g_t.detalhe[0])
        paragrafo(cx, y, 920, 24, COR_TEXTO2, g_t.detalhe, 1);
}

static void tela_lista(void) {
    const int x = 120, w = UI_LARGURA - 240, h = 92, gap = 16;
    texto(x, 104, 40, COR_TEXTO, g_t.titulo, 0);
    if (g_t.status[0])
        texto(x, 160, 22, COR_TEXTO2, g_t.status, 0);

    const int visiveis = 4;
    int primeiro = g_t.selecionado >= visiveis ? g_t.selecionado - visiveis + 1 : 0;
    int y = 214;
    for (int i = primeiro; i < g_t.total && i < primeiro + visiveis; i++) {
        const UiItem *it = &g_t.itens[i];
        bool sel = i == g_t.selecionado;
        if (sel) {
            retangulo(x, y, w, h, 12, COR_VERDE_CLARO);
            retangulo(x + 4, y + 4, w - 8, h - 8, 9, COR_CARTAO_SEL);
        } else {
            retangulo(x, y, w, h, 12, COR_CARTAO);
        }
        texto(x + 32, y + 14, 30, COR_TEXTO, it->nome, 0);
        texto(x + 32, y + 54, 20, COR_TEXTO2, it->detalhe, 0);
        uint32_t cor = it->estado == 1 ? COR_VERDE_CLARO : it->estado == 2 ? COR_AVISO : COR_TEXTO3;
        circulo((float)(x + w - 40), (float)(y + h / 2), 8, cor, 1.0f);
        y += h + gap;
    }
    if (g_t.detalhe[0])
        paragrafo(x, g_t.total ? y + 8 : 214, w, 22, COR_TEXTO2, g_t.detalhe, 0);
}

void ui_renderizar(uint32_t *pixels, int passo, double t) {
    g_px = pixels;
    g_passo = passo;
    fundo();
    cabecalho();
    switch (g_t.tipo) {
        case T_STATUS:   tela_status(t); break;
        case T_CODIGO:   tela_codigo(t); break;
        case T_MENSAGEM: tela_mensagem(); break;
        case T_LISTA:    tela_lista(); break;
        default: break;
    }
    rodape(g_t.rodape);
}

// ---------------------------------------------------------------------------
// Switch: framebuffer do libnx e fonte do sistema
// ---------------------------------------------------------------------------
#ifdef __SWITCH__
#include <switch.h>
#include "core/log.h"

static Framebuffer g_fb;
static bool g_ativa = false;
static bool g_pl_iniciado = false;
static u64 g_t0 = 0;

static void carregar_fonte(void) {
    if (g_pl_iniciado)
        return;
    Result rc = plInitialize(PlServiceType_User);
    if (R_FAILED(rc)) {
        LOG("ui: servico de fontes indisponivel (0x%x)", rc);
        return;
    }
    g_pl_iniciado = true;
    PlFontData fonte;
    rc = plGetSharedFontByType(&fonte, PlSharedFontType_Standard);
    if (R_FAILED(rc)) {
        LOG("ui: fonte do sistema indisponivel (0x%x)", rc);
        return;
    }
    ui_definir_fonte((const unsigned char *)fonte.address);
    LOG("ui: fonte do sistema %s (%u bytes)", g_tem_fonte ? "carregada" : "invalida", fonte.size);
}

bool ui_iniciar(void) {
    if (g_ativa)
        return true;
    carregar_fonte();
    Result rc = framebufferCreate(&g_fb, nwindowGetDefault(), UI_LARGURA, UI_ALTURA, PIXEL_FORMAT_RGBA_8888, 2);
    if (R_FAILED(rc)) {
        LOG("ui: framebuffer falhou (0x%x)", rc);
        return false;
    }
    framebufferMakeLinear(&g_fb);
    g_ativa = true;
    if (!g_t0)
        g_t0 = armGetSystemTick();
    return true;
}

void ui_encerrar(void) {
    if (!g_ativa)
        return;
    framebufferClose(&g_fb);
    g_ativa = false;
}

bool ui_ativa(void) {
    return g_ativa;
}

void ui_desenhar(void) {
    if (!g_ativa)
        return;
    u32 passo;
    uint32_t *px = framebufferBegin(&g_fb, &passo);
    double t = armTicksToNs(armGetSystemTick() - g_t0) / 1e9;
    ui_renderizar(px, (int)(passo / sizeof(uint32_t)), t);
    framebufferEnd(&g_fb);
}
#else
bool ui_iniciar(void) { return false; }
void ui_encerrar(void) {}
bool ui_ativa(void) { return false; }
void ui_desenhar(void) {}
#endif
