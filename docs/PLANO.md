# Plano do projeto

## Situação atual (12/11/2026)

> Parte do trabalho das fases 2c.3 a 4 foi feita numa conversa do Claude que era do blog (Arcar.se). Este resumo junta tudo aqui, para a conversa do NXStream continuar.

- **`main`** = **v0.10.2** (13/11/2026): juntada com a `fase-4` a pedido do usuário, antes do fim dos testes da fase 4. Inclui tudo da v0.8.1 (imagem pela GPU, controle, som, menu com + e − por 1 s, botão Xbox) mais reconexão automática, HOME sem pausar, interface gráfica e logo próprio.
- **`fase-4`** = mesma versão da `main`; os testes da fase 4 continuam no dia a dia.
- Branches `fase-2c`, `fase-2c3` e `fase-3` já estão contidas na `main`/`fase-4` e podem ser apagadas.
- Resultados de cada teste, versão por versão: `docs/DESENVOLVIMENTO.md` (fases 2c.2, 2c.3, 3 e 4).
- **v0.10.0** (fase-4): interface gráfica simples no estilo do app do Xbox (`docs/telas/`) e tela de reconexão imediata ao acordar. A v0.9.3 reconectou 3 de 3 vezes em 12–13 s.
- **v0.10.1**: logo próprio (`assets/logo/`), no ícone do app e nas telas.
- **v0.10.3**: logo corrigido dentro do app, ícone sem manchas, gamertag no cabeçalho e menu gráfico no streaming.
- **v0.10.4**: as telas usam o próprio PNG do logo (embutido), sem anéis em volta.
- **v0.10.5**: abertura mais rápida (sem contagem, console e gamertag lembrados no SD).
- **v0.10.6**: inicialização sem trancos (rede em segundo plano) e estatísticas gráficas.
- **v0.10.7**: fila do decodificador maior e menos gravação no log durante rajadas de mensagens.
- **v0.11.0** (25/11): início do xCloud, fase X0 (`docs/XCLOUD.md`): R na lista de consoles abre a lista dos jogos da nuvem. Funcionou: 622 jogos, Ultimate, região BRAZILSOUTH, mas em 20 s.
- **v0.11.1**: carga da nuvem mais rápida (gzip, nomes no SD): 2ª abertura em 5,7 s.
- **v0.11.4**: grava no log o motivo quando o Xbox encerra a conexão.
- **v0.13.1**: L e R no formato dos botões de ombro; foto do perfil no cabeçalho.
- **v0.13.0**: xCloud fase X3: abas Consoles/Nuvem (L/R, abre na última), faixa de recentes, grade de capas, busca pelo teclado.
- **v0.12.6**: sem as tentativas de sessão durante a espera (prendiam 90 s); sessão presa com o Xbox ligado é refeita.
- **v0.12.5**: espera do Xbox ligar mais rápida (lista a cada 2 s e tentativa de sessão a cada 8 s).
- **v0.12.4**: tela "O Xbox está desligado" que conecta sozinha quando o console é ligado pelo botão; abertura sem tentativa à toa.
- **v0.12.3**: o app lembra que o Xbox está desligado (a Microsoft continua dizendo "em repouso").
- **v0.12.2**: rede no cartão de estatísticas; "O Xbox foi desligado" quando é o caso. X1 e X2 do xCloud validadas.
- **v0.12.1**: montador de quadros próprio com NACK (fim dos quadros quebrados pela internet) e 720p na nuvem.
- **v0.12.0**: xCloud fase X1: A na lista de jogos da nuvem abre o jogo (fila, preparação, conexão pela internet).
- **v0.11.5**: desligar o Xbox no meio da sessão não o religa mais: o app pergunta se quer conectar de novo.
- **v0.11.3**: Xbox desligado aparece assim na lista; nuvem sem renovar a conta à toa.
- **v0.11.2**: com o Xbox desligado (não em repouso), o erro aparece na hora com o que fazer; "Tentar de novo" depois de um erro de sessão não segue mais com a sessão que falhou.
- **23/11:** repositório público (Actions sem cota). Antes, o histórico foi reescrito para tirar um dado pessoal dos testes; quem tinha um clone precisa clonar de novo.
- Próximo passo do xCloud: o usuário testar a v0.13.0 (interface da nuvem); depois, fase X4 (reconexão sem reiniciar o jogo).
- Fase 4: o usuário testar no dia a dia (repouso, Wi-Fi, HOME, sessão longa) e mandar os logs; com a fase 4 validada, juntar `fase-4` na `main` e seguir para a fase 5 (vibração, mapeamento configurável, opções).
- Dicas registradas: para atualizar, copiar só o `NXStream.nro` (o `token.json` guarda o login entre versões); o preto em 16 (faixa limitada) é o correto no OLED.

## Objetivo

Um cliente de **Xbox Remote Play** para Nintendo Switch (homebrew `.nro`) que transmite a tela do **seu próprio Xbox, na rede local**, com foco em **confiabilidade**:

- reconectar sozinho depois que o Switch dorme ou a rede oscila, sem fechar o app;
- aguentar rajadas de comandos do controle sem derrubar a conexão.

O projeto nasceu da experiência com o LibNXbox (ursusworks), que funcionava mas era frágil e nunca teve o código-fonte publicado. O NXStream é escrito do zero.

## Decisões tomadas

| Decisão | Escolha | Motivo |
|---|---|---|
| Escopo | Remote Play do console próprio na rede local; **desde 25/11 também xCloud** ([XCLOUD.md](XCLOUD.md)) | O Remote Play ficou estável primeiro; o xCloud reaproveita login, WebRTC, vídeo, áudio, controle e interface. |
| Interface | **O mais simples e leve possível**: texto no console do libnx até a v0.9; desde a v0.10.0, telas gráficas simples no estilo do app do Xbox, em português | O esforço vai para estabilidade; com o streaming estável, uma interface simples e bonita. |
| Build | GitHub Actions gera o `.nro` a cada push | Não depende de ambiente instalado no PC. |
| Licença | GPL-3.0 | Mantém o projeto e derivados livres. Compatível com FFmpeg. |
| Linguagem | C (C++ apenas se uma biblioteca exigir, ex.: WebRTC) | Padrão do libnx, leve, controle total. |
| Fase 2 em três etapas | 2a sessão → 2b WebRTC → 2c vídeo, cada uma testada no Switch | Isola problemas: falha de WebRTC não se mistura com falha de vídeo. |
| Console em repouso | Conectar normalmente: a sessão acorda o console só no modo de streaming | Mesmo comportamento do app oficial, sem ligar a TV. |
| Resolução inicial | 720p | Nativa da tela portátil, mais leve. 1080p como opção na fase 5. |
| Biblioteca WebRTC | libdatachannel (com camada de compatibilidade para o Switch) | Mais robusta. O trabalho de compatibilidade nas dependências seria o mesmo com a libpeer. |
| Sair do streaming | **Segurar + e − juntos por 2 s** | Na fase 3 todos os botões vão para o Xbox; a combinação não é usada por jogos. |
| Estatísticas na tela | **Texto pequeno sempre num canto** durante o desenvolvimento | Facilita os testes; pode virar "aparece ao segurar um atalho" na fase 5. |
| Botão HOME | **Streaming continua rodando** em segundo plano (objetivo da fase 4, se o Switch permitir) | O HOME é fácil de apertar sem querer; o streaming não deve cair por isso. O bloqueio (sleep) continua pausando e reconectando. |

## Fases

Cada fase termina com um `.nro` testado no Switch antes de seguir para a próxima.

| Fase | Entrega | Critério de aceite (teste no Switch) | Status |
|---|---|---|---|
| **0** | Build automático + app de diagnóstico | Abre em title mode, mostra IP, registra eventos de sleep/resume | ✅ Concluída (v0.0.2 validada no Switch) |
| **1** | Login por código + lista de consoles | Logar uma vez; reabrir o app sem pedir login; seu Xbox aparece listado com estado | ✅ Concluída (v0.1.0 validada no Switch) |
| **2a** | Sessão: criar, acompanhar estados, autorizar, keepalive, encerrar | Sessão fica "pronta" e permanece viva por minutos; comportamento após sleep observado | ✅ Concluída (v0.2.3 validada no Switch) |
| **2b** | Conexão WebRTC com o console (biblioteca portada, SDP/ICE, DTLS, canais de dados) | "Conectado ao console" com os canais abertos | ✅ Concluída (v0.4.4): conexão completa com o Xbox, 4 canais, handshake, vídeo chegando, keepalive aceito, 3+ min estável |
| **2c.1** | Quadros: montar os quadros H.264 dos pacotes RTP e ler a resolução | Quadros por segundo estáveis, quadros-chave chegando, resolução lida | ✅ Concluída (v0.5.2): 1280x720 a 60 quadros/s, ~4,5 Mbps, estável por quase 10 min |
| **2c.2** | Decodificação: quadros no chip de vídeo do Switch (FFmpeg com NVDEC) | Quadros decodificados por segundo = recebidos, tempo de decodificação baixo | ✅ Concluída (v0.6.0): 59–61 decodificados/s, ~2 ms por quadro no NVDEC, estável por ~11 min |
| **2c.3** | Imagem: desenhar os quadros na tela (GPU) | A tela do Xbox aparece no Switch (sem controle e sem som) | ✅ v0.7.2: imagem certa, menu e estatísticas, 8+ min sem travadas (nível de preto em teste junto com a fase 3) |
| **3** | Controle + áudio | Jogar normalmente; teste de rajada de botões sem queda | ✅ v0.8.1: 21 min jogando, controle e botão Xbox, som até o fim, sem quedas (v0.8.2: áudio sem cortes por diferença de relógio) |
| **4** | Reconexão completa + HOME sem pausar | Sleep/resume, queda do Wi-Fi por alguns segundos e sessão longa (1h+) sem fechar o app; apertar HOME e voltar sem perder a conexão | 🧪 v0.9.0 em teste (uso no dia a dia) |
| **5** | Vibração, configurações (qualidade, deadzone, mapeamento), acabamento | Uso normal do dia a dia | — |

A fase 2 é a mais pesada e arriscada, porque junta WebRTC, criptografia e decodificação de vídeo. Por isso foi dividida em 2a, 2b e 2c.

## Riscos conhecidos

| Risco | Impacto | Mitigação |
|---|---|---|
| Microsoft muda endpoints ou formatos da API | App para de conectar | Endereços e versões isolados em um único arquivo (`source/api/config.h`). Respostas inesperadas vão para o log. |
| ~~Portar a biblioteca WebRTC para o Switch~~ | ~~Atraso na fase 2~~ | ✅ **Resolvido na 2b:** libdatachannel portada (camada `compat/`) e validada em execução no Switch. Ver [ARQUITETURA.md](ARQUITETURA.md#webrtc). |
| Sleep do Switch derruba todos os sockets | Conexão morre ao dormir | Máquina de estados recria WebRTC/sessão ao acordar (fase 4). O app detecta a volta pelo tempo parado entre quadros (no sleep o sistema não envia aviso) e o Wi-Fi volta na hora. A sessão no servidor sobrevive ao sleep (testado até 190 s, no sleep e no HOME), então ao acordar deve bastar refazer a conexão com o console. |
| Token de renovação dá acesso à conta Xbox | Segurança da conta | Fica só no cartão SD, nunca é enviado a outro lugar além dos servidores da Microsoft. Nunca é escrito no log. |
| Build do FFmpeg é demorado | Builds lentos no CI | Cache do FFmpeg compilado no GitHub Actions. |

## Documentos relacionados

- [ARQUITETURA.md](ARQUITETURA.md): módulos, threads, máquina de estados e bibliotecas
- [PROTOCOLO.md](PROTOCOLO.md): como o Remote Play funciona (autenticação, API, WebRTC)
- [DESENVOLVIMENTO.md](DESENVOLVIMENTO.md): build, testes no Switch, logs e convenções
- [TELAS.md](TELAS.md): todas as telas do app
- [XCLOUD.md](XCLOUD.md): roadmap do xCloud (jogos na nuvem)
