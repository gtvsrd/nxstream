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
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_NO_GIF
#define STBI_NO_STDIO
#include "../../third_party/stb/stb_image.h"
#pragma GCC diagnostic pop

#include "ui/logo_png.h"

#ifndef APP_VERSION
#define APP_VERSION "?"
#endif

// ---------------------------------------------------------------------------
// Cores (formato do framebuffer RGBA8888: 0xAABBGGRR)
// ---------------------------------------------------------------------------
#define RGB(r, g, b) (0xFF000000u | ((uint32_t)(b) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(r))
#define COR_FUNDO_TOPO  RGB(0x1C, 0x1C, 0x1C)
#define COR_FUNDO_BASE  RGB(0x0C, 0x0C, 0x0C)
#define COR_VERDE       RGB(0x2B, 0xD4, 0x6A)  // verde do logo
#define COR_VERDE_CLARO RGB(0x2B, 0xD4, 0x6A)
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
// ---------------------------------------------------------------------------
// Logo: o próprio arquivo assets/logo/NXStream-logo-512.png, embutido no app
// (logo_png.h, gerado por scripts/gerar-logo.sh). É decodificado uma vez e
// reduzido, por média de área, para cada tamanho pedido; cada tamanho fica
// guardado para não refazer a conta a cada quadro.
// (Até a v0.10.3 o logo era redesenhado por código com a geometria do SVG.)
// ---------------------------------------------------------------------------
static unsigned char *g_logo_px; // RGBA, g_logo_lado x g_logo_lado
static int g_logo_lado;

typedef struct {
    int tam;            // lado em pixels (0 = livre)
    unsigned char *px;  // RGBA com alfa pré-multiplicado
} LogoReduzido;

#define LOGO_CACHE 12
static LogoReduzido g_logos[LOGO_CACHE];

static const LogoReduzido *logo_reduzido(int tam) {
    for (int i = 0; i < LOGO_CACHE; i++)
        if (g_logos[i].tam == tam)
            return &g_logos[i];
    if (!g_logo_px) {
        int w, h, canais;
        g_logo_px = stbi_load_from_memory(LOGO_PNG, (int)sizeof LOGO_PNG, &w, &h, &canais, 4);
        if (!g_logo_px || w != h)
            return NULL;
        g_logo_lado = w;
    }
    LogoReduzido *r = NULL;
    for (int i = 0; i < LOGO_CACHE && !r; i++)
        if (!g_logos[i].tam)
            r = &g_logos[i];
    if (!r || tam < 8 || tam > g_logo_lado)
        return NULL;
    r->px = malloc((size_t)tam * tam * 4);
    if (!r->px)
        return NULL;
    // Média de área: cada pixel de destino cobre um quadrado de `passo` pixels da origem
    const float passo = (float)g_logo_lado / tam;
    for (int dy = 0; dy < tam; dy++)
        for (int dx = 0; dx < tam; dx++) {
            float x0 = dx * passo, x1 = x0 + passo, y0 = dy * passo, y1 = y0 + passo;
            float sr = 0, sg = 0, sb = 0, sa = 0, peso = 0;
            for (int sy = (int)y0; sy < g_logo_lado && sy < y1; sy++) {
                float py = (sy + 1 < y1 ? sy + 1 : y1) - (sy > y0 ? sy : y0);
                for (int sx = (int)x0; sx < g_logo_lado && sx < x1; sx++) {
                    float pxl = (sx + 1 < x1 ? sx + 1 : x1) - (sx > x0 ? sx : x0);
                    const unsigned char *o = &g_logo_px[(sy * g_logo_lado + sx) * 4];
                    float p = pxl * py, a = o[3] / 255.0f;
                    sr += o[0] * a * p;
                    sg += o[1] * a * p;
                    sb += o[2] * a * p;
                    sa += o[3] * p;
                    peso += p;
                }
            }
            unsigned char *d = &r->px[(dy * tam + dx) * 4];
            d[0] = (unsigned char)(sr / peso + 0.5f);
            d[1] = (unsigned char)(sg / peso + 0.5f);
            d[2] = (unsigned char)(sb / peso + 0.5f);
            d[3] = (unsigned char)(sa / peso + 0.5f);
        }
    r->tam = tam;
    return r;
}

// Logo com `tam` pixels de lado, centrado em (cx, cy)
static void logo(int cx, int cy, int tam) {
    const LogoReduzido *r = logo_reduzido(tam);
    if (!r)
        return;
    int x0 = cx - tam / 2, y0 = cy - tam / 2;
    for (int y = 0; y < tam; y++) {
        if (y0 + y < 0 || y0 + y >= UI_ALTURA)
            continue;
        for (int x = 0; x < tam; x++) {
            const unsigned char *o = &r->px[(y * tam + x) * 4];
            if (!o[3] || x0 + x < 0 || x0 + x >= UI_LARGURA)
                continue;
            uint32_t *p = &g_px[(y0 + y) * g_passo + x0 + x];
            uint32_t d = *p;
            int ia = 255 - o[3]; // cores já multiplicadas pelo alfa
            uint32_t rr = o[0] + ((d & 0xFF) * ia + 127) / 255;
            uint32_t gg = o[1] + (((d >> 8) & 0xFF) * ia + 127) / 255;
            uint32_t bb = o[2] + (((d >> 16) & 0xFF) * ia + 127) / 255;
            *p = 0xFF000000u | (bb << 16) | (gg << 8) | rr;
        }
    }
}

// Logo em tamanho fracionário (para a animação ficar contínua): amostragem
// bilinear da versão de 256 px. Crescer 0,3 px por quadro fica suave; os
// tamanhos inteiros da v0.10.4 pulavam de 2 em 2 px e davam trancos.
static void logo_suave(float cx, float cy, float tam) {
    const LogoReduzido *b = logo_reduzido(256);
    if (!b)
        return;
    const int L = 256;
    float x0 = cx - tam / 2, y0 = cy - tam / 2, esc = L / tam;
    int ix0 = (int)floorf(x0), iy0 = (int)floorf(y0), ix1 = (int)ceilf(x0 + tam), iy1 = (int)ceilf(y0 + tam);
    for (int y = iy0; y < iy1; y++) {
        if (y < 0 || y >= UI_ALTURA)
            continue;
        float sy = (y + 0.5f - y0) * esc - 0.5f;
        int ya = (int)floorf(sy);
        float fy = sy - ya;
        for (int x = ix0; x < ix1; x++) {
            if (x < 0 || x >= UI_LARGURA)
                continue;
            float sx = (x + 0.5f - x0) * esc - 0.5f;
            int xa = (int)floorf(sx);
            float fx = sx - xa;
            float acc[4] = {0, 0, 0, 0};
            for (int k = 0; k < 4; k++) {
                int xx = xa + (k & 1), yy = ya + (k >> 1);
                if (xx < 0 || yy < 0 || xx >= L || yy >= L)
                    continue;
                float w = ((k & 1) ? fx : 1 - fx) * ((k >> 1) ? fy : 1 - fy);
                const unsigned char *o = &b->px[(yy * L + xx) * 4];
                acc[0] += o[0] * w;
                acc[1] += o[1] * w;
                acc[2] += o[2] * w;
                acc[3] += o[3] * w;
            }
            if (acc[3] < 0.5f)
                continue;
            uint32_t *p = &g_px[y * g_passo + x];
            uint32_t d = *p;
            float ia = 1.0f - acc[3] / 255.0f; // cores já multiplicadas pelo alfa
            uint32_t rr = (uint32_t)(acc[0] + (d & 0xFF) * ia + 0.5f);
            uint32_t gg = (uint32_t)(acc[1] + ((d >> 8) & 0xFF) * ia + 0.5f);
            uint32_t bb = (uint32_t)(acc[2] + ((d >> 16) & 0xFF) * ia + 0.5f);
            *p = 0xFF000000u | ((bb > 255 ? 255 : bb) << 16) | ((gg > 255 ? 255 : gg) << 8) | (rr > 255 ? 255 : rr);
        }
    }
}

static char g_conta[64]; // gamertag mostrada no cabeçalho ("" = nenhuma)

void ui_definir_conta(const char *gamertag) {
    copiar(g_conta, sizeof g_conta, gamertag);
}

static void cabecalho(void) {
    logo(72, 52, 56);
    texto(110, 35, 28, COR_TEXTO, "NXStream", 0);
    if (g_conta[0]) { // gamertag com a inicial num círculo, como um avatar
        int w = largura(22, g_conta);
        texto(1232, 39, 22, COR_TEXTO, g_conta, 2);
        int cx = 1232 - w - 28;
        circulo((float)cx, 52, 17, COR_VERDE, 1.0f);
        const char *p = g_conta;
        int cp = proximo_cp(&p);
        char inicial[8] = {0};
        if (cp < 0x80) {
            inicial[0] = (char)((cp >= 'a' && cp <= 'z') ? cp - 32 : cp);
        } else {
            memcpy(inicial, g_conta, (size_t)(p - g_conta));
        }
        texto(cx, 40, 21, COR_ESCURO, inicial, 1);
    }
    texto(48, 664, 16, COR_TEXTO3, "v" APP_VERSION, 0);
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
static void rodape_em(const char *r, int x_direita, int y) {
    if (!r || !r[0])
        return;
    char copia[160];
    copiar(copia, sizeof copia, r);
    char *partes[8];
    int n = 0;
    for (char *p = strtok(copia, "|"); p && n < 8; p = strtok(NULL, "|"))
        partes[n++] = p;
    int x = x_direita;
    for (int i = n - 1; i >= 0; i--) {
        char *igual = strchr(partes[i], '=');
        if (!igual)
            continue;
        *igual = '\0';
        const char *rotulo = igual + 1;
        int w = largura(22, rotulo);
        texto(x - w, y, 22, COR_TEXTO2, rotulo, 0);
        botao(x - w - 26, y + 14, partes[i]);
        x -= w + 26 + 15 + 36;
    }
}

static void rodape(const char *r) {
    if (!r || !r[0])
        return;
    preencher(48, 648, UI_LARGURA - 96, 1, COR_LINHA);
    rodape_em(r, UI_LARGURA - 64, 670);
}

// ---------------------------------------------------------------------------
// Telas
// ---------------------------------------------------------------------------
static void tela_status(double t) {
    const float cx = UI_LARGURA / 2.0f, cy = 262;
    // O logo "respira": cresce e encolhe alguns pixels. Nada é desenhado em
    // volta dele (os anéis da v0.10.3 pareciam fazer parte do logo).
    float tam = 200.0f + 8.0f * (float)sin(t * 2 * M_PI / 2.4);
    logo_suave(cx, cy, tam);

    int y = 400;
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

// Menu por cima do vídeo: um cartão no centro da imagem RGBA da sobreposição.
// O véu escuro em volta já foi pintado por quem chama; aqui tudo é opaco.
void ui_menu_video(uint32_t *pixels, int passo, const char *titulo, const char *subtitulo,
                   const char *const *itens, int total, int selecionado, const char *rodape_txt) {
    g_px = pixels;
    g_passo = passo;
    const int w = 600, alt_item = 58, topo = 118, base = 70;
    const int h = topo + total * alt_item + base;
    const int x = (UI_LARGURA - w) / 2, y = (UI_ALTURA - h) / 2;
    retangulo(x - 2, y - 2, w + 4, h + 4, 22, COR_LINHA);
    retangulo(x, y, w, h, 20, RGB(0x16, 0x16, 0x16));
    logo(x + 56, y + 56, 64);
    texto(x + 102, y + 26, 28, COR_TEXTO, titulo ? titulo : "", 0);
    if (subtitulo && subtitulo[0])
        texto(x + 102, y + 62, 19, COR_TEXTO2, subtitulo, 0);
    preencher(x + 28, y + topo - 14, w - 56, 1, COR_LINHA);
    for (int i = 0; i < total; i++) {
        int iy = y + topo + i * alt_item;
        if (i == selecionado) {
            retangulo(x + 20, iy, w - 40, alt_item - 8, 12, COR_VERDE);
            retangulo(x + 23, iy + 3, w - 46, alt_item - 14, 9, COR_CARTAO_SEL);
        }
        texto(x + 48, iy + 11, 24, i == selecionado ? COR_TEXTO : COR_TEXTO2, itens[i], 0);
    }
    preencher(x + 28, y + h - base + 4, w - 56, 1, COR_LINHA);
    rodape_em(rodape_txt, x + w - 36, y + h - base + 24);
}

// Número com vírgula decimal ("5,4")
static void num(char *buf, size_t tam, const char *fmt, double v) {
    snprintf(buf, tam, fmt, v);
    for (char *p = buf; *p; p++)
        if (*p == '.')
            *p = ',';
}

static void linha_est(int x, int y, int w, const char *rotulo, const char *valor, uint32_t cor_ponto) {
    circulo((float)(x + 6), (float)(y + 12), 5, cor_ponto, 1.0f);
    texto(x + 20, y, 19, COR_TEXTO2, rotulo, 0);
    texto(x + w, y, 19, COR_TEXTO, valor, 2);
}

void ui_estatisticas_video(uint32_t *pixels, int passo, const UiEstatisticas *e) {
    g_px = pixels;
    g_passo = passo;
    const int x = 24, y = 24, w = 440, pad = 22;
    const int h = 398;
    retangulo(x - 1, y - 1, w + 2, h + 2, 19, COR_LINHA);
    retangulo(x, y, w, h, 18, RGB(0x14, 0x14, 0x14));

    // Cabeçalho
    logo(x + pad + 16, y + 32, 32);
    texto(x + pad + 42, y + 18, 22, COR_TEXTO, "Estatísticas", 0);
    char buf[96];
    int s = e->conectado_s > 0 ? e->conectado_s : 0;
    if (s >= 3600) snprintf(buf, sizeof buf, "há %d h %02d min", s / 3600, s % 3600 / 60);
    else if (s >= 60) snprintf(buf, sizeof buf, "há %d min", s / 60);
    else snprintf(buf, sizeof buf, "há %d s", s);
    texto(x + w - pad, y + 21, 18, COR_TEXTO3, buf, 2);
    preencher(x + pad, y + 60, w - 2 * pad, 1, COR_LINHA);

    const int lx = x + pad, lw = w - 2 * pad;
    int ly = y + 74;
    char a[32], b[32];

    // Vídeo
    snprintf(buf, sizeof buf, "%d×%d · %.0f quadros/s", e->largura, e->altura, e->fps);
    linha_est(lx, ly, lw, "Vídeo", buf, e->fps >= 55 ? COR_VERDE : e->fps >= 30 ? COR_AVISO : COR_ERRO);
    ly += 32;

    // Taxa + gráfico dos últimos 60 s
    num(a, sizeof a, "%.1f Mbps", e->mbps);
    linha_est(lx, ly, lw, "Taxa", a, e->mbps > 0.3f ? COR_VERDE : COR_AVISO);
    ly += 32;
    const int gx = lx, gy = ly, gw = lw, gh = 54;
    retangulo(gx, gy, gw, gh, 8, RGB(0x1C, 0x1C, 0x1C));
    float maximo = 1.0f;
    for (int i = 0; i < e->hist_total; i++)
        if (e->hist_mbps[i] > maximo)
            maximo = e->hist_mbps[i];
    const float escala = maximo * 1.15f;
    if (e->hist_total >= 2) {
        const int ax = gx + 8, aw = gw - 16, base_y = gy + gh - 6, alt = gh - 12;
        const float passo_x = (float)aw / (UI_HIST - 1);
        const int primeiro = UI_HIST - e->hist_total; // alinha à direita: o mais novo na borda
        float ant_y = -1;
        for (int xx = ax; xx <= ax + aw; xx++) {
            float f = (xx - ax) / passo_x - primeiro; // posição em amostras
            if (f < 0)
                continue;
            int k = (int)f;
            float fr = f - k;
            float v0 = e->hist_mbps[k < e->hist_total ? k : e->hist_total - 1];
            float v1 = e->hist_mbps[k + 1 < e->hist_total ? k + 1 : e->hist_total - 1];
            float vy = base_y - alt * ((v0 + (v1 - v0) * fr) / escala);
            for (int yy = (int)vy + 1; yy < base_y; yy++) // área
                misturar(xx, yy, COR_VERDE, 0.20f);
            // linha: preenche o vão vertical até o ponto da coluna anterior
            float y_de = ant_y < 0 ? vy : ant_y;
            float lo = y_de < vy ? y_de : vy, hi = y_de < vy ? vy : y_de;
            for (float yy = lo - 1.0f; yy <= hi + 1.0f; yy += 1.0f) {
                float d = yy < lo ? lo - yy : yy > hi ? yy - hi : 0;
                misturar(xx, (int)yy, COR_VERDE, d >= 1.0f ? 0.0f : 1.0f - d * 0.6f);
            }
            ant_y = vy;
        }
    }
    ly += gh + 6;
    texto(gx + 2, ly, 14, COR_TEXTO3, "últimos 60 s", 0);
    num(a, sizeof a, "máx %.1f Mbps", maximo);
    texto(gx + gw - 2, ly, 14, COR_TEXTO3, a, 2);
    ly += 28;

    // Decodificação
    num(a, sizeof a, "%.1f ms", e->decod_ms);
    num(b, sizeof b, "%.1f", e->decod_ms_max);
    snprintf(buf, sizeof buf, "%s · máx %s · %s", a, b, e->hardware ? "NVDEC" : "CPU");
    linha_est(lx, ly, lw, "Decodificação", buf,
              e->decod_ms < 12 ? COR_VERDE : e->decod_ms < 16.7f ? COR_AVISO : COR_ERRO);
    ly += 32;

    // Tela
    snprintf(buf, sizeof buf, "%lu pulados · %lu repetidos", e->pulados, e->repetidos);
    linha_est(lx, ly, lw, "Tela", buf, COR_VERDE);
    ly += 32;

    // Áudio
    if (e->audio_ativo)
        snprintf(buf, sizeof buf, "fila %d ms · %lu perdidos", e->audio_fila_ms, e->audio_perdidos);
    else
        snprintf(buf, sizeof buf, "desligado");
    linha_est(lx, ly, lw, "Áudio", buf, !e->audio_ativo ? COR_ERRO : e->audio_perdidos ? COR_AVISO : COR_VERDE);
    ly += 32;

    // Conexão
    bool ka_ok = e->keepalive_status >= 200 && e->keepalive_status < 300;
    if (e->keepalive_status < 0)
        snprintf(buf, sizeof buf, "aguardando o 1º keepalive");
    else if (ka_ok)
        snprintf(buf, sizeof buf, "ativa · keepalive há %d s", e->keepalive_ha_s);
    else
        snprintf(buf, sizeof buf, "keepalive falhou (HTTP %ld)", e->keepalive_status);
    linha_est(lx, ly, lw, "Sessão", buf, e->keepalive_status < 0 ? COR_TEXTO3 : ka_ok ? COR_VERDE : COR_ERRO);
    ly += 36;

    preencher(x + pad, ly, w - 2 * pad, 1, COR_LINHA);
    texto(x + w / 2, ly + 10, 16, COR_TEXTO3, "Segure + e − por 1 s para o menu", 1);
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
