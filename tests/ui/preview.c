// Gera imagens das telas da interface no PC (para conferir o visual sem o Switch).
// Uso: make -C tests/ui FONTE=/caminho/fonte.ttf  -> tests/ui/saida/*.ppm
#include "ui/tela.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t px[UI_LARGURA * UI_ALTURA];

static void salvar(const char *nome) {
    char caminho[256];
    snprintf(caminho, sizeof caminho, "saida/%s.ppm", nome);
    FILE *f = fopen(caminho, "wb");
    fprintf(f, "P6\n%d %d\n255\n", UI_LARGURA, UI_ALTURA);
    for (int i = 0; i < UI_LARGURA * UI_ALTURA; i++) {
        unsigned char c[3] = {px[i] & 0xFF, (px[i] >> 8) & 0xFF, (px[i] >> 16) & 0xFF};
        fwrite(c, 1, 3, f);
    }
    fclose(f);
}

int main(int argc, char **argv) {
    FILE *f = fopen(argc > 1 ? argv[1] : "", "rb");
    if (!f) { fprintf(stderr, "fonte?\n"); return 1; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *ttf = malloc(n); if (fread(ttf, 1, n, f) != (size_t)n) return 1; fclose(f);
    ui_definir_fonte(ttf);

    ui_definir_conta("Gamertag123");
    ui_status("Conectando a XBOX", "Preparando o console",
              "Xbox Series X · em repouso · 6 s\nSe o console estava em repouso, ele acorda sem ligar a TV.",
              "B=Cancelar");
    ui_renderizar(px, UI_LARGURA, 0.0); salvar("1_status");

    ui_status("Reconectando a XBOX", "Nova tentativa em 2 s", "Tentativa 2 de 5", "B=Voltar para a lista");
    ui_renderizar(px, UI_LARGURA, 1.1); salvar("2_reconectando");

    ui_status("Conectando a XBOX", "O Xbox está desligado",
              "Ligue o console pelo botão: o NXStream conecta sozinho quando ele ligar (12 s).\n"
              "Para ligar de longe: Configurações > Geral > Opções de energia > Suspensão.",
              "B=Voltar para a lista|R=Jogos na nuvem|+=Fechar");
    ui_renderizar(px, UI_LARGURA, 0.3); salvar("11_ligue_o_xbox");

    ui_definir_conta(NULL); // no login ainda não há conta
    ui_codigo(NULL, "microsoft.com/link", "ABCD1234", "Aguardando login · o código expira em 14:32",
              "A=Abrir a página no Switch|+=Fechar");
    ui_renderizar(px, UI_LARGURA, 0.5); salvar("3_codigo");
    ui_definir_conta("Gamertag123");

    ui_mensagem(UI_ERRO, "Não foi possível abrir a sessão.",
                "xapi: criar sessao respondeu HTTP 503: {\"code\":\"ServiceUnavailable\"} Confira se o console "
                "está ligado ou em repouso e conectado à internet.",
                "A=Tentar de novo|B=Voltar para a lista|+=Fechar");
    ui_renderizar(px, UI_LARGURA, 0); salvar("4_erro");

    ui_mensagem(UI_AVISO, "Sair da conta?", "Na próxima vez será preciso fazer login de novo.",
                "A=Sair da conta|B=Cancelar");
    ui_renderizar(px, UI_LARGURA, 0); salvar("5_aviso");

    UiItem itens[2] = {{"XBOX", "Xbox Series X · Em repouso", 2}, {"Sala", "Xbox One S · Desligado", 0}};
    ui_definir_abas(0);
    ui_lista("Seus consoles", "Conectado como Gamertag123", itens, 2, 0, NULL,
             "A=Conectar|Y=Atualizar|X=Sair da conta|-=Diagnóstico|+=Fechar");
    ui_renderizar(px, UI_LARGURA, 0); salvar("6_lista");

    ui_lista("Seus consoles", "Conectado como Gamertag123", NULL, 0, 0,
             "Nenhum console encontrado nesta conta.\nNo Xbox, ative em: Configurações > Dispositivos e conexões > "
             "Recursos remotos > Habilitar recursos remotos.",
             "Y=Atualizar|X=Sair da conta|+=Fechar");
    ui_renderizar(px, UI_LARGURA, 0); salvar("7_lista_vazia");

    // xCloud (fase X3): faixa de recentes + grade de capas (capas falsas)
    {
        static uint32_t capas[8][UI_CAPA_L * UI_CAPA_A];
        const uint32_t cores[8][2] = {{0xFF2A4A8A, 0xFF101828}, {0xFF2050C0, 0xFF0A1530}, {0xFF309040, 0xFF0A2010},
                                      {0xFF8040A0, 0xFF200A30}, {0xFF20A0D0, 0xFF082030}, {0xFFC06030, 0xFF301008},
                                      {0xFF3070E0, 0xFF101C40}, {0xFF608060, 0xFF101810}};
        for (int k = 0; k < 8; k++)
            for (int j = 0; j < UI_CAPA_A; j++)
                for (int i = 0; i < UI_CAPA_L; i++) {
                    float t = (float)j / UI_CAPA_A;
                    uint32_t a = cores[k][0], c = cores[k][1], r = 0xFF000000u;
                    for (int sh = 0; sh < 24; sh += 8) {
                        int va = (a >> sh) & 0xFF, vc = (c >> sh) & 0xFF;
                        r |= (uint32_t)(va + (vc - va) * t) << sh;
                    }
                    if ((i / 20 + j / 20) % 5 == 0) r = (r & 0xFF000000u) | ((r & 0xFEFEFE) >> 1);
                    capas[k][j * UI_CAPA_L + i] = r;
                }
        // Capa de verdade (JPEG 180x270 do catálogo), reduzida para 140x210
        uint32_t *real = NULL;
        FILE *fj = fopen("capa_teste.jpg", "rb");
        if (fj) {
            static unsigned char jpg[200000];
            size_t nj = fread(jpg, 1, sizeof jpg, fj);
            fclose(fj);
            real = ui_decodificar_capa(jpg, nj, UI_CAPA_L, UI_CAPA_A);
        }
        static const char *nomes[] = {"Gears Tactics", "FINAL FANTASY VII REMAKE INTERGRADE", "Minecraft Dungeons II",
                                      "Balatro", "Minecraft Dungeons", "Forza Horizon 5", "Hollow Knight", "Hades II",
                                      "A Plague Tale: Requiem", "Age of Empires IV", "Among Us", "Ara: History Untold",
                                      "Avowed", "Back 4 Blood"};
        UiNuvem n;
        memset(&n, 0, sizeof n);
        n.clip_topo = 100;
        n.clip_base = 592;
        snprintf(n.rotulos[0].texto, sizeof n.rotulos[0].texto, "Jogados recentemente");
        n.rotulos[0].y = 100;
        snprintf(n.rotulos[1].texto, sizeof n.rotulos[1].texto, "Todos os jogos (622)");
        n.rotulos[1].y = 370;
        n.n_rotulos = 2;
        for (int i = 0; i < 7; i++) {
            UiCartao *c = &n.cartoes[n.n_cartoes++];
            c->x = 102 + i * 156; c->y = 136;
            snprintf(c->nome, sizeof c->nome, "%s", nomes[i]);
            c->capa = i == 5 ? NULL : (i == 1 && real) ? real : capas[i % 8];
            c->selecionado = i == 1;
        }
        for (int i = 0; i < 7; i++) {
            UiCartao *c = &n.cartoes[n.n_cartoes++];
            c->x = 102 + i * 156; c->y = 406;
            snprintf(c->nome, sizeof c->nome, "%s", nomes[7 + i]);
            c->capa = i == 3 ? NULL : capas[(i + 3) % 8];
        }
        snprintf(n.nome, sizeof n.nome, "FINAL FANTASY VII REMAKE INTERGRADE");
        snprintf(n.info, sizeof n.info, "Square Enix · jogado recentemente");
        ui_definir_abas(1);
        ui_nuvem(&n, "A=Jogar|X=Detalhes|Y=Buscar|-=Atualizar|+=Fechar");
        ui_renderizar(px, UI_LARGURA, 0); salvar("10_nuvem");
        ui_definir_abas(-1);
        free(real);
    }
    // Menu por cima do vídeo: fundo simulando um quadro do jogo com o véu escuro
    for (int i = 0; i < UI_LARGURA * UI_ALTURA; i++) {
        int x = i % UI_LARGURA, y = i / UI_LARGURA;
        px[i] = 0xFF000000u | ((uint32_t)(40 + y / 12) << 16) | ((uint32_t)(30 + x / 20) << 8) | (uint32_t)(20 + x / 40);
    }
    const char *opcoes[] = {"Voltar ao jogo", "Botão Xbox (guia)", "Mostrar estatísticas", "Desconectar",
                           "Fechar o NXStream"};
    ui_menu_video(px, UI_LARGURA, "NXStream", "XBOX · conectado há 12 min", opcoes, 5, 0, "A=Escolher|B=Voltar");
    salvar("8_menu_video");

    // Estatísticas por cima do vídeo
    for (int i = 0; i < UI_LARGURA * UI_ALTURA; i++) {
        int x = i % UI_LARGURA, y = i / UI_LARGURA;
        px[i] = 0xFF000000u | ((uint32_t)(40 + y / 12) << 16) | ((uint32_t)(30 + x / 20) << 8) | (uint32_t)(20 + x / 40);
    }
    UiEstatisticas st = {0};
    snprintf(st.console, sizeof st.console, "XBOX");
    st.conectado_s = 754; st.largura = 1280; st.altura = 720; st.fps = 60; st.fps_decod = 60; st.mbps = 5.4f;
    st.decod_ms = 2.1f; st.decod_ms_max = 2.7f; st.hardware = true; st.pulados = 43; st.repetidos = 54;
    st.audio_ativo = true; st.audio_fila_ms = 40; st.keepalive_status = 200; st.keepalive_ha_s = 12;
    st.hist_total = 60;
    st.rede_pacotes = 331982; st.rede_fora_de_ordem = 89915; st.rede_recuperados = 67;
    for (int i = 0; i < 60; i++) st.hist_mbps[i] = 4.5f + 1.5f * (float)((i * 37 % 11) / 10.0) + (i > 40 ? 1.2f : 0);
    ui_estatisticas_video(px, UI_LARGURA, &st);
    salvar("9_estatisticas");
    return 0;
}
