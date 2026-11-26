# Desenvolvimento

## Build

### Automático (GitHub Actions)

Todo push dispara o workflow [`build.yml`](../.github/workflows/build.yml), que compila dentro do container oficial `devkitpro/devkita64` e publica o `.nro`. As dependências do WebRTC (`scripts/compilar-deps.sh`) ficam em cache e só são recompiladas quando o script ou a camada `compat/` mudam.

Para baixar: aba **Actions** → execução mais recente → seção **Artifacts** → `NXStream-nro` (um `.zip` com o `NXStream.nro`). É preciso estar logado no GitHub.

### Local (opcional)

1. Instale o [devkitPro](https://devkitpro.org/wiki/Getting_Started) com o grupo `switch-dev`.
2. Uma vez, compile as dependências do WebRTC (mbedTLS 3 e libdatachannel, alguns minutos): `sh scripts/compilar-deps.sh`. Elas ficam em `deps/prefix/` (fora do Git).
3. Na pasta do projeto: `make`
4. Para limpar: `make clean`

## Testar no Switch

1. Copie `NXStream.nro` para `/switch/NXStream/` no cartão SD.
2. Abra em **title mode**: segure **R** ao iniciar qualquer jogo instalado e escolha o NXStream no menu do homebrew. Pelo álbum (applet mode) o app tem pouca memória e o streaming não vai funcionar.
3. Anote o resultado e, se algo der errado, mande o arquivo de log (ver abaixo).

## Crash reports

Quando o app fecha com erro, o Atmosphère salva um relatório em `/atmosphere/crash_reports/` no cartão SD. Para traduzir os endereços em funções e linhas:

1. Baixe o artifact `NXStream-elf-para-crash-reports` do **mesmo** build do `.nro` que travou.
2. Com o devkitPro instalado: `sh scripts/traduzir-crash.sh NXStream.elf <relatorio>.log`

O script confere se o `Module Id` do relatório bate com o `.elf` antes de traduzir.

## Logs

- **No cartão SD:** `/switch/NXStream/NXStream.log`, com horário de cada evento. O arquivo cresce a cada uso; pode ser apagado a qualquer momento.
- **Ao vivo no PC (nxlink):** no menu do homebrew, aperte **Y** para entrar no modo de rede. No PC, com as ferramentas do devkitPro instaladas, rode:
  ```sh
  nxlink -s NXStream.nro
  ```
  O `.nro` é enviado ao Switch pela rede e as mensagens do app aparecem no terminal. Evita tirar o cartão SD a cada teste.

**Nunca são gravados no log:** tokens, senhas, código de login e qualquer dado da conta.

## Testes no PC

### Teste de limpeza da libdatachannel

`sh tests/limpeza/rodar.sh` (precisa de git, cmake, g++ e libssl-dev): compila a libdatachannel no mesmo commit do Switch, com os patches de `compat/patches/aplicar.py`, e verifica que `rtcCleanup()` termina depois de uma conexão igual à do app. Pegou o travamento da v0.5.1 antes do Switch. Roda no GitHub Actions quando os patches mudam.

### Interpretação de respostas

As partes sem rede (interpretação das respostas da Microsoft e armazenamento do token) têm testes que rodam em qualquer PC com `gcc`, e também automaticamente no GitHub Actions:

```sh
make -C tests
```

## Checklists de teste por fase

### Fase 0: diagnóstico

| Teste | Resultado esperado | Resultado obtido |
|---|---|---|
| Abrir em title mode | Linha "Modo: TITLE MODE (ok)" | ✅ ok |
| Rede | Linha com o IP do Switch | ✅ ok |
| Apertar POWER, esperar ~10 s, acordar | App continua aberto e mostra "Switch acordou do sleep" | ✅ v0.0.2: app continua aberto e recebe os avisos do sistema |
| Apertar Y logo após acordar | Tempo até a rede voltar como "conectado" | ✅ Conectado de imediato |
| Colocar e tirar da dock | Linha "Modo mudou" | |

**v0.0.1:** o app não recebeu nenhum aviso ao acordar (o libnx suspende em silêncio por padrão). A rede volta conectada imediatamente.
**v0.0.2:** ativa os avisos do sistema (`AppletFocusHandlingMode_SuspendHomeSleepNotify`) e adiciona detector de congelamento. ✅ O app detecta a volta e a rede volta instantaneamente ao acordar. ⚠️ Correção posterior (log da v0.2.2): no **bloqueio/sleep**, a detecção veio do detector de tempo, não de um aviso do sistema. Nenhum aviso chega nesse caso.

### Fase 1: login e lista de consoles (v0.1.0)

| Teste | Resultado esperado | Resultado obtido |
|---|---|---|
| Primeira abertura | Tela com endereço `microsoft.com/link` e um código | ✅ |
| Digitar o código no celular/PC e logar | App detecta sozinho em poucos segundos e mostra "Conectando ao Xbox Live..." | ✅ |
| Lista de consoles | Gamertag (ou "conectada") e seu Xbox com tipo e estado (Ligado / Em repouso) | ✅ Console encontrado; A mostra o aviso da fase 2 |
| Fechar e abrir o app de novo | Vai direto para a lista, **sem pedir login** | ✅ 2 a 3 s até a lista |
| Abrir de novo no dia seguinte | Continua sem pedir login | |
| Y na lista | Lista atualiza | |
| X na lista e confirmar | Volta para a tela de login | |
| Desligar o Wi-Fi do Switch e abrir o app | Mensagem de erro com "A = tentar de novo", **sem fechar o app** | |

Sleep/resume com o app na lista: retoma em ~0,5 s.

Se algo falhar, mande o arquivo `/switch/NXStream/NXStream.log` (ele não contém tokens).

### Fase 2a: sessão de streaming (v0.2.3)

| Teste | Resultado esperado | Resultado obtido |
|---|---|---|
| A em um console **ligado** | Estados passam por "Preparando..." até "Sessao pronta!" | ✅ Pronta em ~5 s |
| A em um console **em repouso** | Mesmo resultado; o console acorda só no modo de streaming | ✅ Pronta em ~11 s |
| Tempo até ficar pronta | Mostrado na tela ("preparada em X s") | ✅ ~11 s a partir do repouso |
| Deixar a sessão aberta 3+ minutos | Verificações com "viva" e nenhuma falha (v0.2.2: ✅ ~6 min) | v0.2.0: keepalive HTTP 400 a cada envio (~30–40 s). Servidor: `KeepAlive : ServerStreamingStarted` (keepalive não aceito antes do cliente conectar). v0.2.1 troca por consulta de estado. Sessão continuou existindo por 4+ min |
| Dormir o Switch 30 s com a sessão aberta e acordar | Linha "Voltou de X s suspenso: sessao continua viva" (ou "caiu") | ✅ v0.2.3: detectado e mostrado na tela; sessão viva após 37 s e 98 s |
| Dormir o Switch 5+ minutos e acordar | Idem: descobre por quanto tempo a sessão sobrevive sem keepalive | ✅ Viva após 190 s (3 min 10 s). Limite máximo ainda não medido |
| HOME com a sessão aberta | Detectado como suspensão | ✅ Detectado por tempo (sem aviso do sistema) |
| B na sessão | Volta para a lista; a sessão é encerrada | ✅ `DELETE` HTTP 202 |

**v0.2.1 tem um bug:** chamadas HTTP lentas eram confundidas com suspensão. A v0.2.2 passou a depender só dos avisos do sistema, mas ao **bloquear** o Switch nenhum aviso foi detectado. A v0.2.3 usa os dois sinais: aviso do sistema e tempo parado entre quadros, descontando o tempo das chamadas de rede. O log registra qual sinal detectou cada suspensão.

Os dois testes de sleep não têm resposta "certa": servem para medir quanto tempo o servidor mantém a sessão sem keepalive. Isso define a estratégia de reconexão da fase 4.

### Fase 2b, parte 1: diagnóstico WebRTC (v0.3.0)

Na lista de consoles, aperte **−** para abrir o diagnóstico. Ele não usa o Xbox.

| Teste | Resultado esperado | Resultado obtido |
|---|---|---|
| Abrir o diagnóstico | App não trava nem fecha | ✅ |
| Certificado e oferta SDP | "ok em X s" com certificado, vídeo, áudio e dados = sim | ✅ 0,01 s, tudo "sim" |
| Candidatos de rede | Pelo menos um com o IP do Switch (o mesmo da fase 0), "coleta completa" | ✅ Coleta completa em 0,05 s |
| Resultado | "tudo funcionando no Switch!" | ✅ |
| A (repetir) algumas vezes | Repete sem travar | ✅ Todas as vezes |

Log da v0.3.0 (35 diagnósticos seguidos, todos ok) mostrou, corrigido na v0.4.1:
- canal e trilhas do diagnóstico não eram apagados (105 objetos sobrando ao fechar, e o log terminava antes de "encerrado");
- libjuice usava `rand()` para as credenciais ICE (agora usa o gerador seguro do Switch, via `compat/patches/aplicar.py`);
- linhas de threads diferentes se misturavam no log; o log agora tem trava, linhas de até 8 KB e recomeça ao passar de 1 MB.
| B e depois + (fechar o app) | Fecha normalmente, sem travar | |

O log registra a oferta SDP completa e cada candidato, para comparar com o que o Xbox espera na parte 2.

### Fase 2b, parte 2: conexão com o Xbox (v0.4.5)

Na lista, escolha o console com **A**. Depois de "Sessao pronta", a tela mostra as etapas da conexão.

| Teste | Resultado esperado | Resultado obtido |
|---|---|---|
| 1. Oferta local | ok, com 1 ou mais candidatos locais | ✅ v0.4.4 |
| 2. Resposta do console | ok | ✅ v0.4.3 |
| 3. Candidatos de rede | ok, com candidatos do console aceitos | ✅ v0.4.4: 11 do console, 1 da rede local usado |
| 4. Conexão WebRTC | ok, "CONECTADO" | ✅ v0.4.3, ~2,6 s |
| 5. Handshake dos canais | ok; os quatro canais "aberto" | ✅ v0.4.3 (o canal `chat` abre e o console fecha logo em seguida) |
| Pacotes de vídeo | Contador subindo (o console está enviando imagem) | ✅ v0.4.4 |
| Keepalive após 30 s | ok (HTTP 200): confirma a hipótese da fase 2a | ✅ v0.4.4: HTTP 200 `{"aliveSeconds":null,"reason":"None"}`; hipótese confirmada |
| Deixar conectado 3+ minutos | Continua conectado, keepalives ok | ✅ v0.4.4: 3 min 16 s, 6 keepalives ok |
| B | Desconecta e volta para a lista | ✅ v0.4.5: sem avisos no log |
| Fechar o app com + | Log termina com `encerrando (4/4...)` e `NXStream encerrado` | ✅ v0.4.5: sem `ERRO FATAL` |
| Bloquear o Switch conectado | A conexão cai (reconexão é a fase 4) | v0.4.4: caiu após 282 s de sleep, mas **a sessão sobreviveu** (keepalive HTTP 200 ao acordar): na fase 4 basta refazer só a conexão WebRTC |

**v0.4.1:** nas 3 tentativas, o `POST /sdp` falhou com "Couldn't connect to server" no mesmo instante, enquanto o mesmo servidor tinha respondido a `/play` e `/state` logo antes. Causa provável (hipótese): a libjuice pede 1 MB + 1 MB de buffer para o socket UDP e esgota o poço de memória de sockets da libnx (~2,25 MB por padrão), sem sobrar para o socket TCP do curl. **v0.4.2:** poço de sockets dobrado (`sb_efficiency` 8), 6 sessões BSD, buffers da libjuice reduzidos no Switch (512 KB + 128 KB) e o log das falhas do curl passa a trazer a causa do sistema.

**v0.4.2:** ✅ hipótese dos buffers confirmada: `/sdp` e `/ice` passaram (HTTP 202), o console respondeu com o SDP dele, anunciou o IP local (192.168.0.4:9002), e nas 3 tentativas **a conexão ICE e o handshake DTLS com o Xbox foram concluídos**. ❌ No passo seguinte, ao abrir o SCTP: erro `Could not bind usrsctp socket, errno=106` (EAFNOSUPPORT) numa tentativa, e o app fechou sozinho nas outras duas. Causa (confirmada pelo crash report do Atmosphère, traduzido com o `.elf` do mesmo build): a `struct sockaddr_conn` da usrsctp ficava no formato Linux enquanto a usrsctp (`HAVE_SA_LEN`) e a libnx usam o formato BSD, então AF_CONN (123) era lido como "tamanho 123, família 0". Em `usrsctp_register_address` → `sctp_add_addr_to_vrf`, o `memcpy(..., addr->sa_len)` copiava 123 bytes para um espaço de ~28: corrupção de memória, e o app caía na reserva de memória seguinte (`_malloc_r`, criando o `SctpTransport`). Quando não caía, o `bind` falhava com errno 106. **v0.4.3:** patch na `usrsctp.h` (formato BSD no Switch) e `HAVE_SCONN_LEN` em `compat.cmake`.

**v0.4.3:** ✅ **primeira conexão completa com o Xbox**: CONECTADO em ~2,6 s, SCTP aberto, os 4 canais abertos, `HandshakeAck` do console, primeiro pacote de vídeo e mensagens `titleinfo` / `showlayoutv2`. A conexão subiu antes mesmo de os candidatos do console chegarem. ❌ Logo depois, ao adicionar os candidatos do console, o app fechou sem tela de erro: a libnx fez DNS reverso dos IPs públicos derivados do Teredo (ignorando `NI_NUMERICHOST`) e o candidato IPv6 encerrou o app (provável exceção C++ não tratada, que não gera crash report). **v0.4.4:** só candidatos IPv4 de rede local são usados (o filtro foi testado com os 11 candidatos reais: sobra só `192.168.0.4:9002`), `getnameinfo()` corrigido na camada compat e exceções C++ não tratadas passam a ser registradas no log.

**v0.4.4:** ✅ todas as etapas, sem erros. Achados no log, corrigidos na v0.4.5: ao fechar o app, `rtcCleanup()` lançava `std::system_error` (pego pelo novo registro de exceções; era o motivo de os logs anteriores terminarem sem "encerrado"); ao desconectar, ~1000 avisos "dropping incoming message" porque as trilhas eram apagadas antes de fechar a conexão.

Se alguma etapa falhar, mande o `NXStream.log`: ele contém a oferta, a resposta SDP do console, todos os candidatos e as mensagens dos canais.

### Fase 2c.1: quadros de vídeo (v0.5.2)

Conecte ao console como na fase 2b. A tela de conectado agora mostra a seção **Video**.

| Teste | Resultado esperado | Resultado obtido |
|---|---|---|
| Resolução | Aparece em poucos segundos (provavelmente 1280x720) | v0.5.0: 2560x1440 (o pedido de 720p em `dimensionschanged` foi ignorado). ✅ v0.5.1: **1280x720**, H.264 Main 3.2, ~4,5 Mbps, informando "android" no `X-MS-Device-Info` |
| Quadros por segundo | Estável, perto de 60/s (ou 30/s), mesmo com a tela do Xbox parada | ✅ v0.5.0: 59–61/s durante 2 min, 17–29 Mbps |
| Quadros-chave | Pelo menos 1, logo após conectar (sozinho ou depois de "pedido") | ✅ v0.5.0: primeiro em 0,2 s após conectar, sem pedido; depois a cada 30 s |
| Mexer no Xbox pelo controle dele (menu, um jogo) | Mbps sobe quando a imagem muda muito | ✅ v0.5.2: 4,1–5,5 Mbps; quadros-chave maiores (até ~125 KB) quando a imagem muda |
| Deixar 3+ minutos | Quadros por segundo continuam estáveis | ✅ v0.5.2: 9 min 45 s a 59–61 quadros/s, quadros-chave a cada 30 s, todos os keepalives ok |
| Fechar com + na tela conectada | Fecha sem travar e sem erro de sistema | ✅ v0.5.2: log termina com os 4 passos e `NXStream encerrado` |

O log registra uma linha de vídeo a cada 10 s, a resolução e os primeiros quadros-chave.

**v0.5.0:** fechar o app com **+** direto da tela conectada deu erro de sistema (o log terminou limpo, então o erro veio depois de fechar o log). Causa **confirmada pelo crash report** (traduzido com o `.elf` do mesmo build): a thread principal estava em `socketExit()` (`main.c`) enquanto uma thread da libjuice ainda estava em `recvfrom()` (falha ao acessar a tabela de sockets já liberada: `_socketGetFd`) e uma thread da libdatachannel ainda destruía a conexão ICE (`agent_destroy` → `conn_destroy`). Ou seja: sem `rtcCleanup()`, as threads da biblioteca ainda fechavam a conexão usando sockets quando o app fechou a rede. O `rtcCleanup()` tinha sido removido na v0.4.5 porque lançava `std::system_error`: o motivo é o `std::thread::detach()` da thread de limpeza, que falha na libnx. **v0.5.1:** patch na libdatachannel (`join()` no lugar de `detach()` no Switch) e `rtcCleanup()` de volta antes de fechar a rede; avisos "dropping incoming message" só contados.

**v0.5.1:** ❌ o app travou em "3/4: liberando a libdatachannel" e precisou ser fechado por fora. O `join()` causa um impasse: a thread de limpeza é criada dentro de um destrutor que segura a trava (ou roda numa thread do pool) de que a própria limpeza precisa. **Reproduzido no PC** com `tests/limpeza` (compila a libdatachannel com os mesmos patches): com `join()`, `rtcCleanup()` trava. **v0.5.2:** a thread de limpeza é mantida viva (objeto `std::thread` no heap, sem `detach()` nem `join()`); no mesmo teste, `rtcCleanup()` volta em 0,01 s em 3 de 3 rodadas. O teste roda no GitHub Actions sempre que os patches mudam.

### Fase 2c.2: decodificação (v0.6.0)

Conecte ao console. A tela de conectado ganha a seção **Decodificacao**.

| Teste | Resultado esperado | Resultado obtido |
|---|---|---|
| Decodificador | "h264 + nvtegra" em verde (chip de vídeo do Switch) | ✅ v0.6.0: `h264 + nvtegra (FFmpeg 7.1)` |
| Decodificados por segundo | Igual aos recebidos (~60/s) | ✅ v0.6.0: 59–61/s, sempre igual aos recebidos |
| Tempo de decodificação | Médio bem abaixo de 16,7 ms (o limite para 60/s) | ✅ v0.6.0: médio 1,1–2,3 ms, máximo 6,3 ms |
| Erros e descartados | 0 erros; descartados só os poucos quadros antes do 1º quadro-chave | ⚠️ v0.6.0: 1 descartado (o quadro que chegou antes do 1º quadro-chave) e **1 erro**, no 1º quadro-chave (`AVERROR_UNKNOWN`); a partir do quadro seguinte, nenhum erro em 11 min |
| Imagem mudando muito (menu, jogo) | Continua ~60/s, sem erros | ✅ v0.6.0: 4,2–5,4 Mbps, quadros-chave de até ~127 KB, 60/s |
| Deixar 3+ minutos | Estável | ✅ v0.6.0: 10 min 51 s, 38 994 quadros decodificados, todos os keepalives ok |
| Fechar com + | Fecha sem travar (o log mostra "decod: encerrado") | ✅ v0.6.0: `decod: encerrado`, os 4 passos e `NXStream encerrado` |

**v0.6.0:** a 10 min 10 s de conexão, o console enviou três vezes `TransactionStart /streaming/sessionLifetimeManagement/serverInitiatedDisconnect` (junto com `titleinfo` e `showlayoutv2`), mas o vídeo e os keepalives continuaram até o app ser fechado 41 s depois. O app ainda não responde a essas transações. Nada foi feito no console naquele momento, então a causa provável é **inatividade**: o app ainda não tem controle conectado ao stream (só avisa e retira o gamepad no início). A fase 3 (controle) deve evitar isso; responder a essas transações fica anotado para a fase 4 (estabilidade).

### Fase 2c.3: imagem na tela (v0.7.0)

Conecte ao console. Assim que o primeiro quadro é decodificado, a tela de texto dá lugar ao vídeo do Xbox em tela cheia (ainda sem controle e sem som). Nos primeiros segundos aparece o aviso "Segure + e - por 1 s para abrir o menu".

| Teste | Resultado esperado | Resultado obtido |
|---|---|---|
| Imagem | A tela do Xbox aparece em tela cheia, sem estar de cabeça para baixo ou espelhada | ✅ v0.7.1. (v0.7.0: imagem invertida na vertical; no deko3d padrão o topo da tela é y = +1, como nos exemplos oficiais, e o shader assumia y = -1) |
| Cores | Naturais: preto bem preto, sem tons esverdeados ou rosados | |
| Movimento | Mexendo no Xbox pelo controle dele, a imagem acompanha suave, sem travadas regulares | ✅ v0.7.0: suave; 60 quadros/s recebidos e decodificados, 0 erros |
| Menu (+ e - por 1 s) | Abre o menu; cima/baixo escolhe, A confirma, B volta | ✅ v0.7.1 |
| Estatísticas (pelo menu) | O texto da tela de conexão aparece por cima do vídeo, legível; some ao escolher de novo | ✅ v0.7.1: ligadas e desligadas pelo menu |
| Desconectar (menu ou B) | Volta para a lista de consoles, em texto normal | ✅ v0.7.0 (B, duas vezes): `video: GPU encerrada` antes de fechar a conexão; reconectou em seguida sem problema |
| Fechar o app (menu) | Fecha sem travar; o log mostra "video: GPU encerrada" antes de "decod: encerrado" | |
| Deixar 3+ minutos | Imagem estável | ✅ v0.7.1: 2 min 44 s, 9 877 quadros, 0 erros; ❌ travada a cada 30 s (keepalive, ver abaixo) |
| Modo dock (se possível) | A imagem aparece na TV (em 720p ampliado nesta etapa) | |

O log registra quando a GPU assume a tela (`video: GPU pronta`, `main: video na tela`), o primeiro quadro mapeado na GPU e o padrão de cores detectado. Se a imagem não aparecer, a tela volta para o texto com a linha "Tela: ..." e o motivo fica no log.

Controle do Switch ainda não age no Xbox nesta etapa (é a fase 3).

**v0.7.1:** a imagem travava a cada 30 s: o keepalive era uma chamada HTTP na thread principal, que parava de desenhar até a resposta chegar. **v0.7.2:** o keepalive roda numa thread própria (o módulo HTTP cria uma conexão por requisição, então é seguro); encerrar a sessão espera um keepalive em andamento terminar.

| Teste (v0.7.2) | Resultado esperado | Resultado obtido |
|---|---|---|
| Deixar 2+ minutos com estatísticas ligadas | Nenhuma travada a cada 30 s; "Keepalive: ok" continua atualizando | ✅ v0.7.2: 8 min 45 s sem travadas, 20 keepalives ok, 0 erros |

**v0.7.2:** no OLED o preto fica claro demais. O app já converte a faixa limitada (16 = preto), então o preto do vídeo do Xbox provavelmente chega acima de 16. **v0.7.3** (diagnóstico): o log mede os níveis de luma do quadro a cada 10 s (`video: luma min ..., 1% ..., mediana ...`) e o menu ganha **Nivel de preto: 16 / 24 / 32** para comparar na tela.

| Teste (v0.7.3) | Resultado esperado | Resultado obtido |
|---|---|---|
| Tela escura do Xbox (fundo preto) por ~20 s | O log mostra os níveis reais do preto | |
| Menu > Nivel de preto (16, 24, 32) | Dizer qual deixa o preto realmente preto sem apagar os detalhes escuros | |

### Fase 3: controle e áudio (v0.8.0)

Conecte ao console. Com o vídeo na tela, o controle do Switch passa a controlar o Xbox e o som do Xbox sai no Switch. O ajuste **Nivel de preto** (v0.7.3) continua no menu para o teste dos pretos.

Mapeamento por posição, como no controle do Xbox: **B** do Switch = A, **A** = B, **Y** = X, **X** = Y; L/R = LB/RB; ZL/ZR = LT/RT; **+** = Menu, **−** = View; clique dos analógicos = LS/RS. O **botão Xbox**: com **+** segurado, um clique no **−** (v0.8.1; também no menu do NXStream, + e − por 1 s). O **B** não desconecta mais direto: desconectar e fechar ficam só no menu.

| Teste | Resultado esperado | Resultado obtido |
|---|---|---|
| Navegar no menu do Xbox (analógico, direcional, B = A do Xbox) | Responde na hora, sem botão "preso" | |
| Gatilhos e bumpers (num jogo) | ZL/ZR e L/R funcionam | |
| Botão Xbox (pelo menu do NXStream) | Abre o guia do Xbox | |
| + sozinho e − sozinho | Menu e View do Xbox | |
| Som | Sai o áudio do Xbox, sem chiado nem picotes | |
| Atraso do som | Som junto com a imagem (sem eco perceptível) | |
| Rajada de botões (apertar vários rápido por ~10 s) | A conexão não cai | |
| Ficar 10+ minutos jogando | Não desconecta por inatividade (o pedido de desconexão de ~10 min da v0.6.0 some) | |
| Menu aberto | O Xbox não recebe os botões enquanto o menu está aberto | |

O log registra `audio: pronto (...)`, o primeiro pacote de áudio, `main: primeiro botao enviado ao Xbox` e, com as estatísticas ligadas, a linha de áudio (pacotes, perdidos, fila, faltas) e de controle (pacotes enviados).

**v0.8.0 (12 min jogando):** ✅ controle funcionou (navegação, jogo, botão Xbox pelo menu); ✅ não caiu e não desconectou por inatividade; ✅ preto: 16 é o mais preto (o ajuste saiu do menu). ❌ O som parou aos ~9 min (22:02:36), junto com o único erro de áudio e um salto de ~32 mil no contador de perdidos: a sequência RTP do áudio saltou e o app passou a tratar todo pacote seguinte como "atrasado" (o resto da sessão ficou em silêncio: 9 027 faltas de 20 ms). ⚠️ Às vezes não parecia 60 quadros/s, embora a rede (59–61/s) e a decodificação (0 erros) estivessem estáveis: o app mostrava só o quadro mais recente, então quando dois chegavam no mesmo intervalo de tela um era pulado e o outro repetido.

**v0.8.1:** áudio se ressincroniza em saltos grandes da sequência ou troca de SSRC, aceita só pacotes Opus (payload 111) e registra os outros; vídeo com fila curta de exibição (um quadro por atualização de tela, até 2 de folga); o log a cada 10 s e as estatísticas trazem quadros pulados/repetidos e o estado do áudio.

| Teste (v0.8.1) | Resultado esperado | Resultado obtido |
|---|---|---|
| Jogar 15+ minutos | Som continua até o fim; se o fluxo recomeçar, o log mostra "audio: fluxo recomecou" e o som volta | |
| Fluidez | Sem os trancos de antes; "repetidos" e "pulados" crescendo devagar no log | |
| Botão Xbox: + segurado e clique no - | Abre o guia do Xbox (como no homebrew anterior) | ✅ v0.8.1 |
| + sozinho (toque rápido ou segurado) | Menu do Xbox (sai ao soltar, ou depois de 0,3 s segurado) | |

**v0.8.1 (21 min jogando):** ✅ som até o fim (63 mil pacotes, 0 perdidos, 27 faltas de 20 ms); ✅ atalho do botão Xbox. ⚠️ Pulos de quadro/áudio em alguns momentos. No log: (1) às 22:28:46–56 uma oscilação da rede (faltas de áudio e quadros repetidos ao mesmo tempo); às 22:31:58 e 22:39 quadros repetidos só no vídeo; no total 136 pulados e 324 repetidos em 75 864 (0,6%); (2) a fila de áudio cresceu devagar de 40 para 100 ms (relógios do Xbox e do Switch diferentes) e, ao passar do limite, era cortada de uma vez, um pulo no som. Os pacotes "payload 72" eram RTCP (200) no mesmo fluxo, não áudio.

**v0.8.2:** a saída de áudio toca 1% mais rápido ou mais devagar para manter a fila em ~40 ms (sem cortes; corte só acima de 150 ms); RTCP não vai mais para o log; o log a cada 10 s mostra também os cortes e os blocos acelerados/desacelerados.

| Teste (v0.8.2) | Resultado esperado | Resultado obtido |
|---|---|---|
| Jogar 15+ minutos | Sem pulos no som fora das oscilações da rede; "cortados ms" em 0 e a fila perto de 40 ms no log | |

### Fase 4: reconexão e HOME sem pausar (v0.9.0)

Para usar no dia a dia. Quando a conexão cai, a tela mostra "A conexao com XBOX caiu. Reconectando em N s (tentativa X de 5)..." e o vídeo volta sozinho; **B** nessa tela desiste e volta para a lista.

| Teste | Resultado esperado | Resultado obtido |
|---|---|---|
| Apertar HOME, esperar ~30 s, voltar | Vídeo volta na hora, sem reconectar (o som pode continuar tocando no HOME) | |
| Colocar o Switch em repouso por 1–5 min e acordar | Reconecta sozinho em poucos segundos e o vídeo volta | |
| Repouso longo (30+ min) | Reconecta (se a sessão tiver expirado, abre uma sessão nova sozinho) | |
| Wi-Fi caindo por alguns segundos (ex.: sair de perto do roteador) | Se voltar em até 4 s, segue sem reconectar; senão reconecta | |
| Sessão longa (1 h+) | Continua conectado | |

**v0.9.1:** se a conta tiver um só console, o app conecta sozinho na primeira vez que chega à lista (depois do login ou ao abrir o app): uma tela de 2 s mostra "Conectando a XBOX em 2 s..." com **A** = conectar agora e **B** = ficar na lista. Depois de desconectar, a lista não reconecta sozinha.

**v0.9.2:** corrige o que o log da v0.9.1 mostrou:
- Ao acordar depois de 6 h, todas as tentativas falhavam com HTTP 401: o acesso ao streaming vence em 4 h. Agora é renovado antes de vencer (durante o streaming, sem travar o vídeo) e antes de cada tentativa. No log: `main: acesso ao streaming ... renovando` e `renovado`.
- Cada queda gastava ~25 s em 5 tentativas na sessão antiga, que nunca aceita uma conexão nova. Agora a reconexão abre uma sessão nova direto: deve levar ~6–8 s com o console acordado (mais se ele tiver voltado ao repouso). No log: `main: sessao nova pronta para a reconexao (tentativa N)`.

| Teste (v0.9.2) | Resultado esperado | Resultado obtido |
|---|---|---|
| Repouso de 1–10 min e acordar | Vídeo volta em menos de ~10 s, na tentativa 1 | |
| Repouso de uma noite | Renova o acesso e reconecta sozinho (pode levar ~25 s com o console em repouso) | |
| Sessão de 4 h+ sem desligar a tela | Continua conectado; o log mostra a renovação em segundo plano por volta de 3h45 | |

**Resultado da v0.9.2 (log de 12/11):** 4 de 4 reconexões na 1ª tentativa, incluindo duas depois de ~4 h e ~6 h em repouso (acesso renovado sozinho). Tempo da queda até o vídeo: 8 s (console acordado), 20 s e 19 s (console em repouso, ~9 s preparando a sessão) e 48 s (o encerramento da sessão antiga levou 7 s e o console 36 s para preparar a nova).

**v0.9.3:** otimizações da reconexão:
- **Conexões HTTPS reaproveitadas.** Antes, cada chamada à API abria TCP + TLS do zero até o servidor nos EUA (~1 s cada, e a reconexão faz mais de 10 chamadas). Agora um pequeno conjunto de handles do curl mantém as conexões abertas; depois de um repouso elas são descartadas.
- **Sessão antiga encerrada em segundo plano**, em paralelo com o pedido da sessão nova (antes: 1–7 s esperando).
- **Acesso renovado antes de encerrar a sessão antiga** (antes, depois de horas em repouso, o encerramento dava 401 e gastava 2 s).
- Estado da sessão consultado a cada 1 s de fato (antes saía a cada ~2 s) e a troca de SDP/candidatos consultada a cada 0,25 s.
- O log mostra `main: reconectado (tentativa N), X s desde a queda`.

O tempo que o console leva para preparar a sessão (2,5 s acordado, ~9 s em repouso, às vezes 35 s) depende dele e não do app.

| Teste (v0.9.3) | Resultado esperado | Resultado obtido |
|---|---|---|
| Repouso curto (1–10 min) | "X s desde a queda" menor que os 8 s da v0.9.2 | |
| Repouso longo (horas) | Reconecta na 1ª tentativa, sem "encerrar sessao respondeu HTTP 401" | |
| Conectar pela lista (abrir o app) | Mais rápido que antes, principalmente a etapa "Resposta do console" | |

**Resultado da v0.9.3 (log de 13/11):** 3 de 3 reconexões na 1ª tentativa, em 12–13 s (antes 19–48 s). O que sobra é o console acordando (~9 s em "Provisioning"), a renovação do acesso depois de 4 h (~4 s) e a conexão (~1,5 s). Problema visto: ao voltar do repouso, o último quadro do vídeo ficava parado na tela por alguns segundos antes da tela de reconexão (a renovação do acesso e um keepalive de antes do repouso eram esperados com a tela congelada).

**v0.10.0: interface gráfica.** Telas novas no estilo do app do Xbox (fundo escuro, verde, fonte do Switch), desenhadas pela CPU no framebuffer (`source/ui/tela.c`, texto com stb_truetype). Imagens de referência (geradas no PC com `make -C tests/ui`, com outra fonte) em `docs/telas/`.
- Login: endereço e código grandes; **A abre a página da Microsoft no navegador do Switch** com o código já preenchido.
- Conexão e reconexão: símbolo pulsando com o estado embaixo ("Preparando o console", "Autorizando acesso ao console"...).
- Lista de consoles em cartões, erros e perguntas com ícone, botões no rodapé.
- **A tela de reconexão aparece na hora** em que a conexão cai ou o Switch acorda, e as esperas (renovação, keepalive antigo) animam a tela.
- O diagnóstico WebRTC (−) continua no console de texto.

| Teste (v0.10.0) | Resultado esperado | Resultado obtido |
|---|---|---|
| Abrir o app | Telas novas, texto com acentos, símbolo pulsando ao conectar | |
| Repouso e acordar | A tela "Reconectando" aparece logo, sem ficar no último quadro | |
| Lista de consoles (B na conexão automática) | Cartões, cima/baixo muda a seleção | |
| Login (sair da conta com X) | Código grande; A abre o navegador com o código preenchido | |
| Diagnóstico (−) e voltar | Console de texto e de volta às telas novas | |

**v0.10.1:** logo próprio do projeto (`assets/logo/`): anel bicolor com o N vermelho e a diagonal verde do X. Vira o ícone do `.nro` e aparece nas telas (cabeçalho e no centro da tela de conexão, com ondas vermelhas e verdes). O verde da interface passa a ser o do logo.

**v0.10.2:** o nome passa a ser escrito sempre **NXStream** (arquivos do logo, `NXStream.nro`, artefatos do CI, pasta `/switch/NXStream/` no SD). O cartão SD não diferencia maiúsculas de minúsculas, então a pasta antiga `/switch/nxstream/` continua valendo: o login salvo e o log seguem funcionando, sem mover nada. Pelo mesmo motivo, copiar o `NXStream.nro` substitui o `nxstream.nro` antigo.

**v0.10.3:**
- **Logo dentro do app:** o cabeçalho usava a versão de traço grosso (feita para ícones pequenos) e a tela de conexão tinha um disco cinza atrás, então parecia outro logo. Agora é sempre o desenho do `NXStream-logo.svg`, maior (56 px no cabeçalho, 200 px no centro) e com bordas mais limpas.
- **Ícone do `.nro`:** o `icon.jpg` tinha manchas de compressão JPEG no preto (até 55 de 255 em 23% dos pixels pretos). Agora é salvo com qualidade máxima e sem redução de cor (desvio máximo de 3), com o logo um pouco maior. O tamanho continua 256×256, que é o que o Switch aceita.
- **Gamertag:** o token do streaming não traz o nome da conta; o app agora pede o do Xbox Live comum (uma chamada a mais no login; não se repete nas renovações). Aparece no cabeçalho de todas as telas e na lista ("Conectado como ..."). No log: `auth: gamertag obtida`.
- **Menu gráfico no streaming** (+ e − por 1 s): cartão no centro, por cima do vídeo, com o console e o tempo conectado (`docs/telas/8_menu_video.png`). As estatísticas continuam em texto.

| Teste (v0.10.3) | Resultado esperado | Resultado obtido |
|---|---|---|
| Ícone no menu de homebrews | Preto limpo, sem manchas | |
| Logo nas telas | Igual ao do ícone | |
| Gamertag | Aparece no canto de cima à direita e na lista | |
| Menu no streaming | Cartão gráfico; cima/baixo, A e B funcionam como antes | |

**v0.10.4:** as telas passam a desenhar **o próprio arquivo** `assets/logo/NXStream-logo-512.png`, embutido no app (`source/ui/logo_png.h`) e reduzido para cada tamanho, em vez de redesenhar o logo por código. Conferido no PC: o logo da tela de conexão é igual, pixel a pixel, ao PNG do repositório reduzido para 200 px. Os anéis animados em volta do logo saíram (pareciam fazer parte dele, como se fosse outra versão); agora o logo "respira", crescendo e encolhendo alguns pixels. `scripts/gerar-logo.sh` refaz os PNG, o `icon.jpg` e o `logo_png.h` a partir dos SVG.

**Resultado da v0.10.1 (log de 13/11, 1 h de uso):** tudo saudável. 60 quadros/s, ~5,5 Mbps, decodificação em 2,1 ms, 0 erros; áudio sem pacotes perdidos; reconexão depois de 17 min em repouso na 1ª tentativa, em 12,4 s (7,3 s deles o console acordando); HOME voltando sem reconectar. Da abertura do app até o vídeo: 19 s (3 s de login, 1 s da lista, 2 s da contagem da conexão automática, 9,3 s do console acordando, 2 s de conexão).

**v0.10.5: abertura mais rápida.**
- Sem a contagem de 2 s da conexão automática: com um só console, conecta na hora. O **B** da tela de conexão continua voltando para a lista.
- O app guarda em `/switch/NXStream/conta.json` a gamertag e, se a conta tem um só console, os dados dele (nenhum token). Na abertura seguinte ele pula a consulta da lista (~1 s) e não pede a gamertag de novo (~0,5 s). No log: `main: abertura rapida: direto para XBOX`.
- Esperado: ~3 s a menos da abertura até o vídeo (a partir da 2ª abertura). Sair da conta apaga o arquivo. Se um segundo console entrar na conta, ele aparece ao voltar para a lista (B), e a abertura rápida se desliga sozinha.

| Teste (v0.10.5) | Resultado esperado | Resultado obtido |
|---|---|---|
| 1ª abertura depois de atualizar | Lista consultada, conecta sem contagem | |
| 2ª abertura | "Conectando a XBOX" logo depois do login, sem "Procurando seus consoles" | |
| B durante "Preparando o console" | Volta para a lista de consoles | |

**Resultado da v0.10.5 (logs de 13 a 15/11):** abertura rápida funcionando (`abertura rapida: direto para XBOX`), gamertag obtida, renovação do acesso depois de 12 h em repouso e reconexões na 1ª tentativa (6,5 a 14 s). Streaming sem erros.

**v0.10.6:**
- **Inicialização sem trancos.** Cada chamada de rede (login, lista, estado da sessão a cada segundo, troca de SDP e candidatos) parava o laço principal por 0,2 a 3 s, e a animação congelava a cada consulta. Agora as chamadas rodam numa thread (`chamar()` em `main.c`) e a tela segue a 60 quadros/s. O logo também "respira" em tamanhos fracionários (antes pulava de 2 em 2 px).
- **Estatísticas gráficas** (menu › Mostrar estatísticas): cartão no canto de cima à esquerda, sem escurecer o jogo, com resolução e quadros/s, taxa com gráfico dos últimos 60 s, decodificação, quadros pulados/repetidos, áudio e sessão (`docs/telas/9_estatisticas.png`). Cada linha tem uma bolinha verde, amarela ou vermelha.

| Teste (v0.10.6) | Resultado esperado | Resultado obtido |
|---|---|---|
| Abrir o app | Logo pulsando liso, sem travadas a cada segundo, do login até o vídeo | |
| Reconectar depois do repouso | Mesma animação lisa | |
| Menu › Mostrar estatísticas | Cartão no canto, gráfico da taxa enchendo da direita para a esquerda | |

**Resultado da v0.10.6 (log de 15 a 22/11, 7 aberturas e 7 reconexões):** tudo na 1ª tentativa. Abertura até o vídeo em ~14 s (3 s de login, 7–9 s do console acordando, 2 s de conexão); reconexões em 6–17 s, inclusive depois de 35 h em repouso. Streaming: 0 erros de decodificação, 0 pacotes de áudio perdidos. Ponto a melhorar: em 3 aberturas a fila do decodificador encheu logo depois de entrar num jogo (imagem parada ~0,3 s até o próximo quadro-chave), sempre durante uma rajada de dezenas de mensagens `canRecordChanged` do console, cada uma gravada no log.

**v0.10.7:**
- Fila do decodificador de 8 para 24 quadros: uma parada curta não obriga mais a descartar tudo e esperar um quadro-chave.
- Mensagens repetidas do console vão para o log só na 1ª vez e a cada 100, com um resumo ao desconectar (`conexao: message ... recebida N vezes`).
- Keepalive no log só no 1º e a cada 10 min (falhas continuam sempre).

O log registra `main: conexao instavel (...)`, `main: conexao voltou sozinha em X s`, `main: ... reconectando`, `main: reconectado (tentativa N)` e, se for o caso, `abrindo uma sessao nova`.

### xCloud, fase X0: lista dos jogos da nuvem (v0.11.0)

Checklist e detalhes em `docs/XCLOUD.md` (seção X0). A lista de consoles ganhou **R = Nuvem**.

**v0.11.1** (log de 25/11): além da carga mais rápida da nuvem (gzip e nomes no SD), a abertura rápida falhou na hora com `WNSError ... State WaitingForServerToRegister`, como já tinha acontecido em 22/11 (3 tentativas manuais em 17 s, todas iguais). O serviço da Microsoft não consegue chamar o console porque ele ainda não se registrou (acabou de ligar na tomada, voltou de uma queda de internet, atualizou). Agora o app espera e tenta sozinho: até 6 vezes, a cada 10 s, com a tela "Esperando o console ficar disponível" (B cancela). O log mostra `console ainda nao registrado ... tentativa N de 6` e, quando der certo, `o console ficou disponivel depois de N espera(s)`. Se não der em ~1 min, o erro sugere ligar o console pelo botão ou pelo app Xbox no celular.

| Teste (v0.11.1) | Resultado esperado | Resultado obtido |
|---|---|---|
| Abrir o app quando aparecer o WNSError | Tela "Esperando o console ficar disponível" com contagem, e o vídeo sozinho depois | Não: 6 de 6 esperas falharam, em 4 aberturas |

**Resultado da v0.11.1 (log de 25/11):** o erro continuou a noite toda. Causa (informada pelo usuário): o Xbox estava **desligado**, não em repouso. Desligado (economia de energia), ele não recebe o chamado da Microsoft para ligar, mesmo com a lista dizendo `ConnectedStandby`. O log também mostrou um bug antigo: depois do erro, **A = Tentar de novo** seguia direto para a conexão WebRTC com a sessão que tinha falhado (`URL using bad/illegal format`); o mesmo aconteceu em 22/11.

**v0.11.2:**
- Sem a espera de 1 min: o erro aparece na hora, como "O Xbox está desligado", com o que fazer (ligar pelo botão e apertar A; para ligar de longe, deixar o Xbox em Suspensão nas opções de energia).
- "Tentar de novo" abre uma sessão nova de verdade (`main: tentando abrir a sessao de novo` no log).

| Teste (v0.11.2) | Resultado esperado | Resultado obtido |
|---|---|---|
| Abrir o app com o Xbox desligado | Erro "O Xbox está desligado" logo depois do login | Sim: 5 s depois de abrir o app |
| Ligar o Xbox pelo botão e apertar A | Conecta e mostra o jogo | Sim: sessão pronta em 4,3 s, vídeo 0,5 s depois |

**v0.11.3** (log de 25/11, 20:33):
- Depois do erro, a lista de consoles mostra o Xbox como "Desligado (ligue pelo botão)", com bolinha cinza, em vez de "Em repouso" (a Microsoft continua dizendo `ConnectedStandby`). Volta ao normal quando uma sessão der certo.
- Ao abrir a nuvem, a conta só é renovada se o token tiver mais de 30 min. No log, ela era renovada de novo 10 s depois da abertura do app (~1 s a mais até a lista).

**Resultado da v0.11.3 (log de 25/11, 20:40):** com o Xbox desligado, erro na hora; ligado pelo botão, A conectou na 1ª (4,3 s de sessão). Depois de 72 s jogando, o próprio console mandou `serverInitiatedDisconnect` e fechou a conexão 3 s depois; a reconexão automática funcionou, mas a sessão nova ficou 42 s em "Provisioning" (o console parecia ainda estar terminando de ligar). Vídeo sem problemas: 60 fps, 0 erros.

**v0.11.4:**
- Ao receber `serverInitiatedDisconnect`, o app reconecta na hora (antes esperava o canal fechar, ~3 s) e mostra "O Xbox encerrou a conexão".
- O conteúdo do aviso vai para o log (`conexao: o console vai encerrar a conexao: ...`), para descobrir o motivo.

**v0.11.5:** o usuário contou que tinha **desligado o Xbox** no meio da sessão: a reconexão automática religou o console (por isso os 42 s em "Provisioning"). Agora, quando o próprio Xbox encerra a conexão, não há reconexão automática: a tela "O Xbox encerrou a conexão" oferece **A = Conectar de novo** e **B = Voltar para a lista**. Quedas de rede e a volta do repouso do Switch continuam reconectando sozinhas.

| Teste (v0.11.5) | Resultado esperado | Resultado obtido |
|---|---|---|
| Desligar o Xbox no meio da sessão | Tela "O Xbox encerrou a conexão", sem religar o console | Sim (v0.12.1: motivo `KickForServerShutdown`) |

**v0.12.3:** o usuário notou que a lista mostrava o Xbox "Em repouso" com ele desligado. A Microsoft informa o último estado que o console avisou (`ConnectedStandby`) e não sabe que ele foi desligado no modo de economia de energia. O app descobre ao tentar conectar (`WaitingForServerToRegister`) ou quando o console avisa que foi desligado na sessão (`KickForServerShutdown`). Antes ele esquecia isso ao fechar; agora guarda em `conta.json` (`"desligado"`) e a lista mostra "Desligado (ligue pelo botão)" até uma sessão com o console dar certo. O log da abertura diz `desligado da ultima vez`.

| Teste (v0.12.3) | Resultado esperado | Resultado obtido |
|---|---|---|
| Fechar e abrir o app com o Xbox desligado | Lista com "Desligado (ligue pelo botão)", bolinha cinza | |
| Ligar o Xbox e conectar | Conecta; a lista volta a mostrar o estado da Microsoft | |

**v0.12.4** (pedido do usuário: contornar os limites da v0.12.3). A Microsoft pode dizer `ConnectedStandby` com o console desligado, mas `On` só aparece com ele ligado de verdade. Então:
- **Tela "O Xbox está desligado"** no lugar do erro (`docs/telas/11_ligue_o_xbox.png`): consulta a lista a cada 5 s e, quando o console aparece `On`, conecta sozinho. B volta para a lista, R abre os jogos da nuvem. Depois de 15 min sem ligar, mostra o erro de antes.
- **Abertura com o Xbox marcado como desligado:** em vez de tentar a sessão direto (e falhar), consulta a lista antes: se já estiver `On`, conecta; se não, vai para a tela de espera.
- Na lista, `On` apaga a marca de desligado na hora.
- Log: `esperando o Xbox ser ligado` e `o Xbox ligou (N s, N consulta(s)); conectando`.

| Teste (v0.12.4) | Resultado esperado | Resultado obtido |
|---|---|---|
| Abrir o app com o Xbox desligado | Tela "O Xbox está desligado" (sem erro) | |
| Ligar o Xbox pelo botão com essa tela aberta | Conecta sozinho uns segundos depois de ele ligar | |
| Ligar o Xbox antes de abrir o app | Conecta direto | |

**Resultado da v0.12.4 (log de 25/11, 22:13):** tela de espera no lugar do erro; quando a lista mostrou o Xbox `On`, a sessão ficou pronta em 4,3 s e o vídeo apareceu ~7 s depois, sem apertar nada no Switch. O usuário mediu **~10 s** entre o Xbox terminar de ligar e a conexão começar.

**v0.12.5:** lista a cada **2 s** (era 5) e, a cada **8 s**, uma tentativa de sessão ("sonda") sem trocar a tela: com o console desligado ela falha em ~2 s; se ele já atende antes de aparecer `On` na lista, a própria sonda vira a conexão. O log diz qual chegou primeiro: `o Xbox apareceu ligado na lista (N s de espera, N sonda(s))` ou `o Xbox atendeu a sonda (N s de espera, sonda N)`.

| Teste (v0.12.5) | Resultado esperado | Resultado obtido |
|---|---|---|
| Ligar o Xbox logo que a tela de espera aparecer | Conecta poucos segundos depois de ele terminar de ligar | Não: 1 min 50 s (sonda presa) |

**Resultado da v0.12.5 (log de 25/11, 22:24):** a sonda das 22:26:47, feita enquanto o Xbox ligava, foi aceita, mas a sessão ficou **90 s** em Provisioning e falhou com `ProvisioningTimedOut: ServerNeverRegistered`. Nesse tempo o app não olhava a lista; quando voltou, ela já dizia `On` e a sessão nova ficou pronta em 4,3 s. Também: desligado pelo menu do Xbox, a Microsoft sabe (lista `Off`, erro `XccsUnknownError: XboxPowerOff`), e o aviso de fim da sessão traz `KickForStopCommand`.

**v0.12.6:**
- Sem sondas: a sessão só é aberta quando a lista mostra `On` (lista a cada 2 s, como na v0.12.5).
- Sessão presa: depois de 15 s em Provisioning, o app consulta a lista a cada 3 s; se o console já está `On` e a sessão continua preparando 10 s depois, ela é fechada e outra é aberta (`sessao presa (aberta com o Xbox ainda ligando); abrindo outra`). Cobre também abrir o app logo depois de ligar o Xbox.
- `XboxPowerOff` leva à mesma tela de espera.

| Teste (v0.12.6) | Resultado esperado | Resultado obtido |
|---|---|---|
| Ligar o Xbox logo que a tela de espera aparecer | `o Xbox ligou (N s de espera)` poucos segundos depois de ele terminar de ligar | |
| Ligar o Xbox e abrir o app logo em seguida | Conecta; se a 1ª sessão prender, `abrindo outra` em ~25 s | |

As próximas fases recebem seus checklists aqui quando começarem.

## Convenções

- **Branches:** `main` sempre compila. Trabalho de cada fase em `fase-N` e merge quando o teste no Switch passar.
- **Commits:** em português, no imperativo e curtos (ex.: "Adiciona login por código").
- **Código:** C, comentários em português, nomes de funções e variáveis em português ou inglês de forma consistente dentro de cada módulo.
- **Configuração da API:** endpoints, versões e tempos ficam em `source/api/config.h`, nunca espalhados pelo código.
