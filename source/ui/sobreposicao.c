// NXStream - texto por cima do vídeo (ver sobreposicao.h)
#include "ui/sobreposicao.h"
#include "ui/tela.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <switch.h>

typedef struct {
    uint8_t c;     // caractere (fonte do libnx: 256 caracteres)
    uint8_t cor;   // índice na tabela de cores (0-15)
    bool    usado; // a célula recebeu algo (ganha fundo escuro)
} Celula;

static Celula g_tela[SOBRE_LINHAS][SOBRE_COLUNAS];
static Celula g_desenhada[SOBRE_LINHAS][SOBRE_COLUNAS]; // última renderizada
static uint8_t g_fundo, g_fundo_desenhado = 255;
static bool g_algum_texto;
static int g_x, g_y;
static uint8_t g_cor = 7;   // branco normal, como o console
static bool g_negrito;
static bool g_nunca_renderizou = true;

// Menu gráfico (v0.10.3): desenhado pela ui/tela por cima do texto
typedef struct {
    bool ativo;
    char titulo[48], subtitulo[96], rodape[96];
    char itens[SOBRE_MENU_MAX][64];
    int  total, selecionado;
} Menu;
static Menu g_menu, g_menu_desenhado;

// Cartão de estatísticas (v0.10.6)
static bool g_est_ativo, g_est_desenhado_ativo;
static UiEstatisticas g_est, g_est_desenhado;

// Mesmas cores do console do libnx (console_sw.c): 8 normais e 8 fortes
static const uint32_t CORES[16] = {
    0x000000, 0x800000, 0x008000, 0x808000, 0x000080, 0x800080, 0x008080, 0xC0C0C0,
    0x808080, 0xFF0000, 0x00FF00, 0xFFFF00, 0x0000FF, 0xFF00FF, 0x00FFFF, 0xFFFFFF,
};

void sobre_limpar(uint8_t fundo) {
    memset(g_tela, 0, sizeof g_tela);
    g_fundo = fundo;
    g_algum_texto = false;
    g_x = g_y = 0;
    g_cor = 7;
    g_negrito = false;
    memset(&g_menu, 0, sizeof g_menu);
    g_est_ativo = false;
}

void sobre_estatisticas(const UiEstatisticas *e) {
    g_est = *e;
    g_est_ativo = true;
}

void sobre_menu(const char *titulo, const char *subtitulo, const char *const *itens, int total,
                int selecionado, const char *rodape) {
    memset(&g_menu, 0, sizeof g_menu); // zera tudo: a comparação em sobre_mudou() é por memcmp
    g_menu.ativo = true;
    snprintf(g_menu.titulo, sizeof g_menu.titulo, "%s", titulo ? titulo : "");
    snprintf(g_menu.subtitulo, sizeof g_menu.subtitulo, "%s", subtitulo ? subtitulo : "");
    snprintf(g_menu.rodape, sizeof g_menu.rodape, "%s", rodape ? rodape : "");
    g_menu.total = total > SOBRE_MENU_MAX ? SOBRE_MENU_MAX : total;
    for (int i = 0; i < g_menu.total; i++)
        snprintf(g_menu.itens[i], sizeof g_menu.itens[i], "%s", itens[i]);
    g_menu.selecionado = selecionado;
}

void sobre_cursor(int coluna, int linha) {
    g_x = coluna < 0 ? 0 : coluna >= SOBRE_COLUNAS ? SOBRE_COLUNAS - 1 : coluna;
    g_y = linha < 0 ? 0 : linha >= SOBRE_LINHAS ? SOBRE_LINHAS - 1 : linha;
}

// Interpreta "\x1b[a;b;...m" a partir de s (que aponta para o '['). Retorna o fim.
static const char *ler_cor(const char *s) {
    int valores[4], n = 0, v = 0;
    bool tem = false;
    s++; // '['
    for (; *s && *s != 'm'; s++) {
        if (*s >= '0' && *s <= '9') {
            v = v * 10 + (*s - '0');
            tem = true;
        } else if (*s == ';') {
            if (n < 4) valores[n++] = tem ? v : 0;
            v = 0;
            tem = false;
        } else {
            return s; // sequência que não é de cor: ignora
        }
    }
    if (n < 4) valores[n++] = tem ? v : 0;
    for (int i = 0; i < n; i++) {
        int c = valores[i];
        if (c == 0) {
            g_cor = 7;
            g_negrito = false;
        } else if (c == 1) {
            g_negrito = true;
        } else if (c >= 30 && c <= 37) {
            g_cor = (uint8_t)(c - 30);
        }
    }
    return *s ? s : s - 1;
}

static void escrever(const char *s) {
    for (; *s; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch == '\x1b' && s[1] == '[') {
            s = ler_cor(s + 1);
            continue;
        }
        if (ch == '\n') {
            g_x = 0;
            if (g_y < SOBRE_LINHAS - 1) g_y++;
            continue;
        }
        if (ch == '\r') {
            g_x = 0;
            continue;
        }
        if (g_x >= SOBRE_COLUNAS) { // quebra como o console
            g_x = 0;
            if (g_y < SOBRE_LINHAS - 1) g_y++;
        }
        Celula *c = &g_tela[g_y][g_x++];
        c->c = ch;
        c->cor = (uint8_t)(g_cor + (g_negrito ? 8 : 0));
        c->usado = true;
        g_algum_texto = true;
    }
}

void sobre_printf(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    escrever(buf);
}

bool sobre_visivel(void) {
    return g_algum_texto || g_fundo > 0 || g_menu.ativo || g_est_ativo;
}

bool sobre_mudou(void) {
    return g_nunca_renderizou || g_fundo != g_fundo_desenhado || memcmp(g_tela, g_desenhada, sizeof g_tela) != 0 ||
           memcmp(&g_menu, &g_menu_desenhado, sizeof g_menu) != 0 || g_est_ativo != g_est_desenhado_ativo ||
           (g_est_ativo && memcmp(&g_est, &g_est_desenhado, sizeof g_est) != 0);
}

void sobre_renderizar(uint32_t *pixels, int passo) {
    // Fonte do console do libnx: 16 u16 por caractere, linha r = bits de fonte[r],
    // coluna x = bit (0x8000 >> x) (ver ConsoleSwRenderer_drawChar)
    const uint16_t *fonte = (const uint16_t *)consoleGetDefault()->font.gfx;
    const uint32_t vazio = (uint32_t)g_fundo << 24;      // preto com o véu escolhido
    const uint32_t fundo_texto = (g_fundo > 190 ? g_fundo : 190u) << 24; // linhas com texto
    for (int ly = 0; ly < SOBRE_LINHAS; ly++) {
        for (int lx = 0; lx < SOBRE_COLUNAS; lx++) {
            const Celula *c = &g_tela[ly][lx];
            uint32_t *base = pixels + (ly * 16) * passo + lx * 16;
            uint32_t bg = c->usado ? fundo_texto : vazio;
            if (!c->usado || c->c == ' ' || c->c == 0) {
                for (int r = 0; r < 16; r++) {
                    uint32_t *p = base + r * passo;
                    for (int x = 0; x < 16; x++) p[x] = bg;
                }
                continue;
            }
            uint32_t rgb = CORES[c->cor & 15];
            // RGBA8 na memória (little endian): R no byte baixo
            uint32_t fg = 0xFF000000u | ((rgb & 0xFF) << 16) | (rgb & 0xFF00) | ((rgb >> 16) & 0xFF);
            const uint16_t *g = fonte + 16 * c->c;
            for (int r = 0; r < 16; r++) {
                uint32_t *p = base + r * passo;
                uint16_t bits = g[r];
                for (int x = 0; x < 16; x++) p[x] = (bits & (0x8000u >> x)) ? fg : bg;
            }
        }
    }
    if (g_est_ativo)
        ui_estatisticas_video(pixels, passo, &g_est);
    g_est_desenhado = g_est;
    g_est_desenhado_ativo = g_est_ativo;
    if (g_menu.ativo) {
        const char *itens[SOBRE_MENU_MAX];
        for (int i = 0; i < g_menu.total; i++)
            itens[i] = g_menu.itens[i];
        ui_menu_video(pixels, passo, g_menu.titulo, g_menu.subtitulo, itens, g_menu.total, g_menu.selecionado,
                      g_menu.rodape);
    }
    g_menu_desenhado = g_menu;
    memcpy(g_desenhada, g_tela, sizeof g_tela);
    g_fundo_desenhado = g_fundo;
    g_nunca_renderizou = false;
}
