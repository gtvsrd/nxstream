// Testes das partes sem rede: interpretação de respostas e armazenamento.
// Rodam no PC e no CI:  make -C tests
#include "auth/auth.h"
#include "api/xapi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_falhas = 0, g_total = 0;

#define CHECAR(cond) do { \
    g_total++; \
    if (!(cond)) { g_falhas++; printf("  FALHOU (linha %d): %s\n", __LINE__, #cond); } \
} while (0)

static void teste_codigo(void) {
    puts("codigo de login");
    AuthCodigo c;
    CHECAR(auth_parse_codigo(
        "{\"user_code\":\"ABCD1234\",\"device_code\":\"DAQABAAEAAAB\",\"verification_uri\":"
        "\"https://www.microsoft.com/link\",\"expires_in\":900,\"interval\":5,\"message\":\"x\"}", &c));
    CHECAR(!strcmp(c.codigo_usuario, "ABCD1234"));
    CHECAR(!strcmp(c.codigo_dispositivo, "DAQABAAEAAAB"));
    CHECAR(!strcmp(c.endereco, "https://www.microsoft.com/link"));
    CHECAR(c.expira_em_s == 900 && c.intervalo_s == 5);

    CHECAR(!auth_parse_codigo("{\"error\":\"invalid_client\"}", &c));
    CHECAR(!auth_parse_codigo("isto nao e json", &c));
}

static void teste_token(void) {
    puts("token da conta");
    AuthConta conta = {0};
    CHECAR(auth_parse_token(200, "{\"access_token\":\"AAA\",\"refresh_token\":\"RRR\",\"token_type\":\"bearer\"}", &conta) == AUTH_OK);
    CHECAR(conta.access_token && !strcmp(conta.access_token, "AAA"));
    CHECAR(conta.refresh_token && !strcmp(conta.refresh_token, "RRR"));

    // Renovação substitui os dois tokens
    CHECAR(auth_parse_token(200, "{\"access_token\":\"A2\",\"refresh_token\":\"R2\"}", &conta) == AUTH_OK);
    CHECAR(!strcmp(conta.access_token, "A2") && !strcmp(conta.refresh_token, "R2"));

    CHECAR(auth_parse_token(400, "{\"error\":\"authorization_pending\"}", &conta) == AUTH_PENDENTE);
    CHECAR(auth_parse_token(400, "{\"error\":\"slow_down\"}", &conta) == AUTH_LENTO);
    CHECAR(auth_parse_token(400, "{\"error\":\"authorization_declined\"}", &conta) == AUTH_RECUSADO);
    CHECAR(auth_parse_token(400, "{\"error\":\"expired_token\"}", &conta) == AUTH_EXPIRADO);
    CHECAR(auth_parse_token(400, "{\"error\":\"invalid_grant\"}", &conta) == AUTH_INVALIDO);
    CHECAR(auth_parse_token(500, "<html>erro</html>", &conta) == AUTH_ERRO);
    CHECAR(auth_parse_token(200, "{\"access_token\":\"so_um\"}", &conta) == AUTH_ERRO);
    // Respostas de erro não apagam os tokens que já existiam
    CHECAR(!strcmp(conta.access_token, "A2"));
    auth_conta_liberar(&conta);
    CHECAR(conta.access_token == NULL);
}

static void teste_xbl(void) {
    puts("tokens do Xbox Live");
    char gtg[64];
    char *t = auth_parse_xbl_token(
        "{\"IssueInstant\":\"2026-11-10T00:00:00Z\",\"Token\":\"eyJ0eXAi\","
        "\"DisplayClaims\":{\"xui\":[{\"uhs\":\"123\",\"gtg\":\"Gustavo123\"}]}}", gtg, sizeof gtg);
    CHECAR(t && !strcmp(t, "eyJ0eXAi"));
    CHECAR(!strcmp(gtg, "Gustavo123"));
    free(t);

    t = auth_parse_xbl_token("{\"Token\":\"T\",\"DisplayClaims\":{\"xui\":[{\"uhs\":\"1\"}]}}", gtg, sizeof gtg);
    CHECAR(t && gtg[0] == '\0');
    free(t);

    CHECAR(auth_parse_xbl_token("{\"XErr\":2148916233}", gtg, sizeof gtg) == NULL);
    CHECAR(auth_descrever_xerr(2148916233LL) != NULL);
    CHECAR(auth_descrever_xerr(1) == NULL);
}

static void teste_streaming(void) {
    puts("token de streaming");
    AuthStreaming s = {0};
    CHECAR(auth_parse_streaming(
        "{\"offeringSettings\":{\"allowRegionSelection\":false,\"regions\":["
        "{\"name\":\"WestUS\",\"baseUri\":\"https://wus.core.gssv-play-prodxhome.xboxlive.com\",\"isDefault\":false},"
        "{\"name\":\"UKSouth\",\"baseUri\":\"https://uks.core.gssv-play-prodxhome.xboxlive.com/\",\"isDefault\":true}]},"
        "\"market\":\"BR\",\"gsToken\":\"GST\",\"tokenType\":\"bearer\",\"durationInSeconds\":14400}", &s));
    CHECAR(s.gs_token && !strcmp(s.gs_token, "GST"));
    CHECAR(!strcmp(s.base_uri, "https://uks.core.gssv-play-prodxhome.xboxlive.com")); // padrão, sem '/' final
    CHECAR(s.duracao_s == 14400);
    auth_streaming_liberar(&s);

    // Sem região padrão: usa a primeira
    AuthStreaming s2 = {0};
    CHECAR(auth_parse_streaming(
        "{\"offeringSettings\":{\"regions\":[{\"baseUri\":\"https://a.example\"},{\"baseUri\":\"https://b.example\"}]},"
        "\"gsToken\":\"G\"}", &s2));
    CHECAR(!strcmp(s2.base_uri, "https://a.example"));
    auth_streaming_liberar(&s2);

    AuthStreaming s3 = {0};
    CHECAR(!auth_parse_streaming("{\"gsToken\":\"G\"}", &s3));
}

static void teste_consoles(void) {
    puts("lista de consoles");
    XConsoles c;
    CHECAR(xapi_parse_consoles(
        "{\"totalItems\":2,\"results\":["
        "{\"deviceName\":\"Xbox da Sala\",\"serverId\":\"F4001ABCDEF\",\"powerState\":\"ConnectedStandby\","
        "\"consoleType\":\"XboxSeriesX\",\"playPath\":\"\",\"outOfHomeWarning\":false},"
        "{\"deviceName\":\"Quarto\",\"serverId\":\"F4002\",\"powerState\":\"On\",\"consoleType\":\"XboxOneS\"}],"
        "\"continuationToken\":null}", &c));
    CHECAR(c.total == 2);
    CHECAR(!strcmp(c.itens[0].nome, "Xbox da Sala"));
    CHECAR(!strcmp(c.itens[0].server_id, "F4001ABCDEF"));
    CHECAR(!strcmp(xapi_estado_amigavel(c.itens[0].estado), "Em repouso"));
    CHECAR(!strcmp(xapi_tipo_amigavel(c.itens[0].tipo), "Xbox Series X"));
    CHECAR(!strcmp(xapi_estado_amigavel(c.itens[1].estado), "Ligado"));

    CHECAR(xapi_parse_consoles("{\"totalItems\":0,\"results\":[]}", &c) && c.total == 0);
    CHECAR(!xapi_parse_consoles("{\"erro\":1}", &c));
}

static void teste_sessao(void) {
    puts("sessao de streaming");
    XSessao s;
    CHECAR(xapi_parse_sessao("{\"sessionId\":\"ABC-123\",\"sessionPath\":\"/v5/sessions/home/ABC-123\",\"state\":\"Provisioning\"}", &s));
    CHECAR(!strcmp(s.caminho, "v5/sessions/home/ABC-123"));
    CHECAR(!strcmp(s.id, "ABC-123"));
    CHECAR(xapi_parse_sessao("{\"sessionPath\":\"v5/sessions/home/X9\"}", &s) && !strcmp(s.caminho, "v5/sessions/home/X9"));
    CHECAR(!xapi_parse_sessao("{\"state\":\"Provisioning\"}", &s));

    char det[128];
    CHECAR(xapi_parse_estado("{\"state\":\"Provisioning\"}", det, sizeof det) == XSESSAO_PREPARANDO);
    CHECAR(!strcmp(det, "Provisioning"));
    CHECAR(xapi_parse_estado("{\"state\":\"ReadyToConnect\"}", det, sizeof det) == XSESSAO_AGUARDA_LOGIN);
    CHECAR(xapi_parse_estado("{\"state\":\"Provisioned\"}", det, sizeof det) == XSESSAO_PRONTA);
    CHECAR(xapi_parse_estado("{\"state\":\"WaitingForResources\"}", det, sizeof det) == XSESSAO_NA_FILA);
    CHECAR(xapi_parse_estado("{\"state\":\"Failed\",\"errorDetails\":{\"code\":\"ConsoleOff\",\"message\":\"Console nao responde\"}}", det, sizeof det) == XSESSAO_FALHOU);
    CHECAR(!strcmp(det, "ConsoleOff: Console nao responde"));
    CHECAR(xapi_parse_estado("{\"state\":\"Novidade\"}", det, sizeof det) == XSESSAO_DESCONHECIDO);
    CHECAR(xapi_parse_estado("<html>", det, sizeof det) == XSESSAO_ERRO);

    char *corpo = xapi_corpo_play("F4001ABCDEF");
    CHECAR(corpo && strstr(corpo, "\"serverId\":\"F4001ABCDEF\""));
    CHECAR(corpo && strstr(corpo, "\"nanoVersion\":\"V3;WebrtcTransport.dll\""));
    CHECAR(corpo && strstr(corpo, "\"titleId\":\"\""));
    free(corpo);

    char *t = auth_parse_token_console(200, "{\"token_type\":\"bearer\",\"access_token\":\"LPT\",\"refresh_token\":\"R\"}");
    CHECAR(t && !strcmp(t, "LPT"));
    free(t);
    CHECAR(auth_parse_token_console(400, "{\"error\":\"invalid_grant\"}") == NULL);
}

static void teste_negociacao(void) {
    puts("negociacao WebRTC");

    // Corpo do /sdp
    char *corpo = xapi_corpo_sdp("v=0\r\no=- 1 2 IN IP4 0.0.0.0\r\n");
    CHECAR(corpo && strstr(corpo, "\"messageType\":\"offer\""));
    CHECAR(corpo && strstr(corpo, "\"sdp\":\"v=0\\r\\no=- 1 2 IN IP4 0.0.0.0\\r\\n\""));
    CHECAR(corpo && strstr(corpo, "\"input\":{\"minVersion\":1,\"maxVersion\":9}"));
    CHECAR(corpo && strstr(corpo, "\"unreliableinput\":{\"minVersion\":9,\"maxVersion\":9}"));
    CHECAR(corpo && strstr(corpo, "\"sampleFrequencyHz\":24000"));
    free(corpo);

    // Resposta do /sdp: exchangeResponse é JSON dentro de texto
    char *sdp = NULL, erro[128];
    CHECAR(xapi_parse_resposta_sdp(
        "{\"exchangeResponse\":\"{\\\"messageType\\\":\\\"answer\\\",\\\"sdp\\\":\\\"v=0\\\\r\\\\na=x\\\\r\\\\n\\\",\\\"status\\\":\\\"success\\\"}\",\"errorDetails\":null}",
        &sdp, erro, sizeof erro) == 1);
    CHECAR(sdp && !strcmp(sdp, "v=0\r\na=x\r\n"));
    free(sdp);
    CHECAR(xapi_parse_resposta_sdp("{\"exchangeResponse\":null,\"errorDetails\":{\"code\":\"SdpFailed\",\"message\":\"x\"}}",
                                   &sdp, erro, sizeof erro) == -1);
    CHECAR(!strcmp(erro, "SdpFailed: x") && sdp == NULL);
    CHECAR(xapi_parse_resposta_sdp("{\"exchangeResponse\":\"{\\\"status\\\":\\\"failed\\\"}\"}", &sdp, erro, sizeof erro) == -1);

    // Corpo do /ice: lista de textos JSON, sem o "a=" do candidato
    XCandidatos locais = {0};
    locais.total = 2;
    strcpy(locais.cand[0], "a=candidate:1 1 UDP 2122317823 192.168.0.20 50000 typ host");
    strcpy(locais.mid[0], "0");
    strcpy(locais.cand[1], "candidate:2 1 UDP 2122317822 192.168.0.20 50001 typ host");
    strcpy(locais.mid[1], "0");
    char *ice = xapi_corpo_ice(&locais, "abcd");
    CHECAR(ice && strstr(ice, "{\"candidates\":[\"{\\\"candidate\\\":\\\"candidate:1 1 UDP"));
    CHECAR(ice && strstr(ice, "\\\"usernameFragment\\\":\\\"abcd\\\""));
    CHECAR(ice && strstr(ice, "\\\"sdpMLineIndex\\\":0"));
    CHECAR(ice && !strstr(ice, "a=candidate"));
    free(ice);

    // Teredo: exemplo da RFC 4380
    char ipv4[16];
    int porta = 0;
    CHECAR(xapi_teredo("2001:0:4136:e378:8000:63bf:3fff:fdd2", ipv4, sizeof ipv4, &porta));
    CHECAR(!strcmp(ipv4, "192.0.2.45") && porta == 40000);
    CHECAR(!xapi_teredo("fe80::1", ipv4, sizeof ipv4, &porta));
    CHECAR(!xapi_teredo("2001:db8::1", ipv4, sizeof ipv4, &porta)); // 2001:db8 não é Teredo
    // Mesmo endereço por extenso e com "::" no meio
    CHECAR(xapi_teredo("2001:0000:4136:e378:8000:63bf:3fff:fdd2", ipv4, sizeof ipv4, &porta));
    CHECAR(!strcmp(ipv4, "192.0.2.45") && porta == 40000);
    CHECAR(xapi_teredo("2001::63bf:3fff:fdd2", ipv4, sizeof ipv4, &porta)); // bytes 4..9 zerados
    CHECAR(!strcmp(ipv4, "192.0.2.45") && porta == 40000);
    // Entradas inválidas
    CHECAR(!xapi_teredo("2001:0:1:2:3:4:5:6:7", ipv4, sizeof ipv4, &porta));   // 9 grupos
    CHECAR(!xapi_teredo("2001::1::2", ipv4, sizeof ipv4, &porta));             // "::" duas vezes
    CHECAR(!xapi_teredo("2001:0:4136:e378:8000:63bf:3fff:fdd2x", ipv4, sizeof ipv4, &porta));
    CHECAR(!xapi_teredo("2001:0:4136:e378:12345:63bf:3fff:fdd2", ipv4, sizeof ipv4, &porta));

    // Resposta do /ice: Teredo vira 2 candidatos extras + o original; end-of-candidates é ignorado
    XCandidatos r;
    int n = xapi_parse_resposta_ice(
        "{\"exchangeResponse\":\"["
        "{\\\"candidate\\\":\\\"a=candidate:1 1 UDP 100 2001:0:4136:e378:8000:63bf:3fff:fdd2 9002 typ host \\\",\\\"sdpMid\\\":\\\"0\\\",\\\"sdpMLineIndex\\\":\\\"0\\\",\\\"messageType\\\":\\\"iceCandidate\\\"},"
        "{\\\"candidate\\\":\\\"a=candidate:2 1 UDP 90 192.168.0.30 9002 typ host \\\",\\\"sdpMid\\\":\\\"0\\\"},"
        "{\\\"candidate\\\":\\\"a=end-of-candidates\\\",\\\"sdpMid\\\":\\\"0\\\"}]\"}", &r);
    CHECAR(n == 4 && r.total == 4);
    CHECAR(!strcmp(r.cand[0], "a=candidate:10 1 UDP 1 192.0.2.45 9002 typ host"));
    CHECAR(!strcmp(r.cand[1], "a=candidate:11 1 UDP 1 192.0.2.45 40000 typ host"));
    CHECAR(strstr(r.cand[2], "2001:0:4136") != NULL);
    CHECAR(strstr(r.cand[3], "192.168.0.30 9002") != NULL && !strcmp(r.mid[3], "0"));
    CHECAR(xapi_parse_resposta_ice("{\"exchangeResponse\":\"{}\"}", &r) == -1);
}

static void teste_filtro_rede_local(void) {
    puts("filtro de candidatos (rede local)");
    // Os 11 candidatos reais que o Xbox enviou no teste da v0.4.3
    static const char *reais[] = {
        "candidate:3765941840 1 udp 2122129151 192.168.0.4 9002 typ host generation 0 ufrag 5MQy network-id 1",
        "a=candidate:10 1 UDP 1 203.0.113.64 9002 typ host",
        "a=candidate:11 1 UDP 1 203.0.113.64 37843 typ host",
        "candidate:3697761578 1 udp 2122255103 2001::14c9:dc0a:140a:6c2c:34ff:8ebf 9002 typ host generation 0 ufrag 5MQy network-id 5 network-cost 50",
        "candidate:1914173063 1 udp 2122197247 2804:6140:2002:5851:8b2e:934d:b2f4:2b61 9002 typ host generation 0 ufrag 5MQy network-id 2",
        "candidate:3306060218 1 udp 1685921535 203.0.113.64 37641 typ srflx raddr 192.168.0.4 rport 9002 generation 0 ufrag 5MQy network-id 1",
        "a=candidate:1 1 UDP 100 192.168.0.4 9002 typ host ",
        "a=candidate:2 1 UDP 1 2804:6140:2002:5851:8b2e:934d:b2f4:2b61 9002 typ host ",
        "a=candidate:10 1 UDP 1 203.0.113.64 9002 typ host",
        "a=candidate:11 1 UDP 1 203.0.113.64 37843 typ host",
        "a=candidate:3 1 UDP 1 2001:0:14c9:dc0a:140a:6c2c:34ff:8ebf 9002 typ host ",
    };
    XCandidatos r = {0};
    for (size_t i = 0; i < sizeof reais / sizeof reais[0]; i++) {
        snprintf(r.cand[r.total], sizeof r.cand[0], "%s", reais[i]);
        strcpy(r.mid[r.total], "0");
        r.total++;
    }
    CHECAR(xapi_filtrar_rede_local(&r) == 1); // só 192.168.0.4:9002, sem o duplicado
    CHECAR(strstr(r.cand[0], "192.168.0.4 9002") != NULL && !strcmp(r.mid[0], "0"));

    XCandidatos f = {0};
    const char *outros[] = {
        "candidate:1 1 UDP 1 10.0.0.5 9002 typ host",
        "candidate:1 1 UDP 1 172.16.1.2 9002 typ host",
        "candidate:1 1 UDP 1 172.32.1.2 9002 typ host",      // fora de 172.16/12
        "candidate:1 1 UDP 1 192.169.0.1 9002 typ host",     // fora de 192.168/16
        "candidate:1 1 UDP 1 169.254.3.4 9002 typ host",
        "candidate:1 1 UDP 1 meu-xbox.local 9002 typ host",  // nome (mDNS)
        "candidate:1 1 UDP 1 192.168.0.300 9002 typ host",   // inválido
        "lixo",
    };
    for (size_t i = 0; i < sizeof outros / sizeof outros[0]; i++) {
        snprintf(f.cand[f.total], sizeof f.cand[0], "%s", outros[i]);
        strcpy(f.mid[f.total], "0");
        f.total++;
    }
    CHECAR(xapi_filtrar_rede_local(&f) == 3);
    CHECAR(strstr(f.cand[0], "10.0.0.5") && strstr(f.cand[1], "172.16.1.2") && strstr(f.cand[2], "169.254.3.4"));
}

static void teste_armazenamento(void) {
    puts("armazenamento do token");
    const char *arq = "teste_token.json";
    remove(arq);
    CHECAR(auth_carregar_refresh(arq) == NULL);
    CHECAR(auth_salvar_refresh(arq, "M.C5_BAY.0.U.-Cabc!*$def"));
    char *t = auth_carregar_refresh(arq);
    CHECAR(t && !strcmp(t, "M.C5_BAY.0.U.-Cabc!*$def"));
    free(t);
    CHECAR(auth_salvar_refresh(arq, "NOVO")); // sobrescreve
    t = auth_carregar_refresh(arq);
    CHECAR(t && !strcmp(t, "NOVO"));
    free(t);
    CHECAR(auth_apagar(arq));
    CHECAR(auth_carregar_refresh(arq) == NULL);
}

int main(void) {
    teste_codigo();
    teste_token();
    teste_xbl();
    teste_streaming();
    teste_consoles();
    teste_sessao();
    teste_negociacao();
    teste_filtro_rede_local();
    teste_armazenamento();
    printf("\n%d de %d verificacoes passaram\n", g_total - g_falhas, g_total);
    return g_falhas ? 1 : 0;
}
