// NXStream - configuração central
//
// Todos os endereços, identificadores e versões de protocolo ficam aqui.
// Se a Microsoft mudar algo, este é o primeiro arquivo a revisar.
// Referência: docs/PROTOCOLO.md
#pragma once

// ---------------------------------------------------------------------------
// Arquivos no cartão SD
// ---------------------------------------------------------------------------
#ifdef __SWITCH__
#define NX_PASTA_APP        "sdmc:/switch/NXStream"
#else
#define NX_PASTA_APP        "."
#endif
#define NX_ARQ_TOKEN        NX_PASTA_APP "/token.json"
#define NX_ARQ_LOG          NX_PASTA_APP "/NXStream.log"
#define NX_ARQ_CONTA        NX_PASTA_APP "/conta.json"   // gamertag e último console (sem tokens)
#define NX_ARQ_NUVEM_NOMES  NX_PASTA_APP "/nuvem-nomes.json" // nomes dos jogos da nuvem (catálogo)
// Opcional: se existir, o HTTPS usa estes certificados em vez dos do sistema
#define NX_ARQ_CERTIFICADOS NX_PASTA_APP "/cacert.pem"

// ---------------------------------------------------------------------------
// Login Microsoft (device code)
// Client ID público do app Xbox, o mesmo usado por xal-node/Greenlight.
// ---------------------------------------------------------------------------
#define MS_CLIENT_ID        "1f907974-e22b-4810-a9de-d9647380c97e"
#define MS_ESCOPO           "xboxlive.signin openid profile offline_access"
#define MS_URL_DEVICECODE   "https://login.microsoftonline.com/consumers/oauth2/v2.0/devicecode"
#define MS_URL_TOKEN        "https://login.microsoftonline.com/consumers/oauth2/v2.0/token"

// ---------------------------------------------------------------------------
// Xbox Live
// ---------------------------------------------------------------------------
#define XBL_URL_USER_AUTH   "https://user.auth.xboxlive.com/user/authenticate"
#define XBL_URL_XSTS        "https://xsts.auth.xboxlive.com/xsts/authorize"
#define XBL_RP_USER         "http://auth.xboxlive.com"
#define XBL_RP_GSSV         "http://gssv.xboxlive.com/"
#define XBL_RP_XBOXLIVE     "http://xboxlive.com"       // só para ler a gamertag

// ---------------------------------------------------------------------------
// Serviço de streaming (GSSV) - console próprio ("xhome")
// ---------------------------------------------------------------------------
#define GSSV_OFERTA         "xhome"
#define GSSV_URL_LOGIN      "https://" GSSV_OFERTA ".gssv-play-prod.xboxlive.com/v2/login/user"
#define GSSV_CLIENTE        "XboxComBrowser"
#define API_CAMINHO_CONSOLES "/v6/servers/home"
#define API_CAMINHO_PLAY     "/v5/sessions/home/play"
#define API_CAMINHO_PLAY_NUVEM "/v5/sessions/cloud/play"   // xCloud: titleId no lugar do serverId
#define API_CAMINHO_ESPERA   "/v1/waittime/"               // + titleId: estimativa da fila da nuvem
// Demais chamadas da sessão usam o "sessionPath" devolvido pelo play:
//   <sessionPath>/state, /connect, /keepalive, /sdp, /ice  e DELETE <sessionPath>

// ---------------------------------------------------------------------------
// xCloud (jogos na nuvem, docs/XCLOUD.md). Mesmo XSTS do gssv, outra oferta:
// xgpuweb = Game Pass Ultimate; xgpuwebf2p = só jogos gratuitos, qualquer conta.
// ---------------------------------------------------------------------------
#define GSSV_OFERTA_NUVEM        "xgpuweb"
#define GSSV_OFERTA_NUVEM_F2P    "xgpuwebf2p"
#define GSSV_URL_LOGIN_NUVEM     "https://" GSSV_OFERTA_NUVEM ".gssv-play-prod.xboxlive.com/v2/login/user"
#define GSSV_URL_LOGIN_NUVEM_F2P "https://" GSSV_OFERTA_NUVEM_F2P ".gssv-play-prod.xboxlive.com/v2/login/user"
#define API_CAMINHO_TITULOS      "/v2/titles"              // jogos da nuvem e se a conta tem direito
#define API_CAMINHO_RECENTES     "/v2/titles/mru?mr=25"    // jogados recentemente, o mais recente primeiro
// Catálogo do Game Pass: nomes (e, na fase X3, capas) a partir do productId
#define CATALOGO_MERCADO         "BR"
#define CATALOGO_IDIOMA          "pt-BR"
#define CATALOGO_URL_PRODUTOS \
    "https://catalog.gamepass.com/v3/products?market=" CATALOGO_MERCADO "&language=" CATALOGO_IDIOMA \
    "&hydration=RemoteHighSapphire0"
#define CATALOGO_LOTE            100   // productIds por consulta (limite do serviço)

// Token de transferência para o console (enviado em /connect)
#define MS_URL_LIVE_TOKEN   "https://login.live.com/oauth20_token.srf"
#define MS_ESCOPO_CONSOLE   "service::http://Passport.NET/purpose::PURPOSE_XBOX_CLOUD_CONSOLE_TRANSFER_TOKEN"

// Resolução do streaming: o console escolhe pelo nome do sistema informado em
// X-MS-Device-Info ao criar a sessão. Nos clientes de referência, "android" pede
// 720p e "windows" 1080p. Na fase 2c.1, com "windows", o Xbox Series X mandou
// 2560x1440 (H.264 nível 5.1, ~25 Mbps): muito acima da tela do Switch.
#define NX_OS_RESOLUCAO     "android"

// Identificação do cliente enviada à API de streaming
#define NX_DEVICE_INFO \
    "{\"appInfo\":{\"env\":{\"clientAppId\":\"www.xbox.com\",\"clientAppType\":\"browser\"," \
    "\"clientAppVersion\":\"21.1.98\",\"clientSdkVersion\":\"8.5.3\",\"httpEnvironment\":\"prod\"," \
    "\"sdkInstallId\":\"\"}},\"dev\":{\"hw\":{\"make\":\"Microsoft\",\"model\":\"unknown\"," \
    "\"sdktype\":\"web\"},\"os\":{\"name\":\"" NX_OS_RESOLUCAO "\",\"ver\":\"22631.2715\",\"platform\":\"desktop\"}," \
    "\"displayInfo\":{\"dimensions\":{\"widthInPixels\":1920,\"heightInPixels\":1080}," \
    "\"pixelDensity\":{\"dpiX\":2,\"dpiY\":2}},\"browser\":{\"browserName\":\"chrome\",\"browserVersion\":\"131.0\"}}}"

#define NX_USER_AGENT \
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36"

// ---------------------------------------------------------------------------
// Tempos de rede (segundos)
// ---------------------------------------------------------------------------
#define HTTP_TEMPO_CONEXAO  10
#define HTTP_TEMPO_TOTAL    20

// ---------------------------------------------------------------------------
// Sessão
// ---------------------------------------------------------------------------
#define SESSAO_INTERVALO_ESTADO_S   1     // consulta do estado enquanto prepara
#define SESSAO_TEMPO_MAX_PREPARO_S  120   // desiste se não ficar pronta
#define SESSAO_INTERVALO_KEEPALIVE_S 30   // o Greenlight usa 30 s
#define SESSAO_MAX_CONNECT          3     // tentativas de /connect
#define NX_IDIOMA                   "pt-BR"
// Convenção do campo não confirmada (as referências usam valores fixos); não afeta a conexão
#define NX_FUSO_MINUTOS             -180
