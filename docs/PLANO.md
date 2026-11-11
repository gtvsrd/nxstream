# Plano do projeto

## Objetivo

Um cliente de **Xbox Remote Play** para Nintendo Switch (homebrew `.nro`) que transmite a tela do **seu próprio Xbox, na rede local**, com foco em **confiabilidade**:

- reconectar sozinho depois que o Switch dorme ou a rede oscila, sem fechar o app;
- aguentar rajadas de comandos do controle sem derrubar a conexão.

O projeto nasceu da experiência com o LibNXbox (ursusworks), que funcionava mas era frágil e nunca teve o código-fonte publicado. O NXStream é escrito do zero.

## Decisões tomadas

| Decisão | Escolha | Motivo |
|---|---|---|
| Escopo | Apenas Remote Play do console próprio, rede local | Foco em fazer uma coisa bem feita. xCloud fica fora. |
| Interface | **O mais simples e leve possível**: texto no console do libnx | O esforço vai para estabilidade, não para visual. |
| Build | GitHub Actions gera o `.nro` a cada push | Não depende de ambiente instalado no PC. |
| Licença | GPL-3.0 | Mantém o projeto e derivados livres. Compatível com FFmpeg. |
| Linguagem | C (C++ apenas se uma biblioteca exigir, ex.: WebRTC) | Padrão do libnx, leve, controle total. |
| Fase 2 em três etapas | 2a sessão → 2b WebRTC → 2c vídeo, cada uma testada no Switch | Isola problemas: falha de WebRTC não se mistura com falha de vídeo. |
| Console em repouso | Conectar normalmente: a sessão acorda o console só no modo de streaming | Mesmo comportamento do app oficial, sem ligar a TV. |
| Resolução inicial | 720p | Nativa da tela portátil, mais leve. 1080p como opção na fase 5. |
| Biblioteca WebRTC | libdatachannel (com camada de compatibilidade para o Switch) | Mais robusta. O trabalho de compatibilidade nas dependências seria o mesmo com a libpeer. |
| Botão HOME | **Streaming continua rodando** em segundo plano (objetivo da fase 4, se o Switch permitir) | O HOME é fácil de apertar sem querer; o streaming não deve cair por isso. O bloqueio (sleep) continua pausando e reconectando. |

## Fases

Cada fase termina com um `.nro` testado no Switch antes de seguir para a próxima.

| Fase | Entrega | Critério de aceite (teste no Switch) | Status |
|---|---|---|---|
| **0** | Build automático + app de diagnóstico | Abre em title mode, mostra IP, registra eventos de sleep/resume | ✅ Concluída (v0.0.2 validada no Switch) |
| **1** | Login por código + lista de consoles | Logar uma vez; reabrir o app sem pedir login; seu Xbox aparece listado com estado | ✅ Concluída (v0.1.0 validada no Switch) |
| **2a** | Sessão: criar, acompanhar estados, autorizar, keepalive, encerrar | Sessão fica "pronta" e permanece viva por minutos; comportamento após sleep observado | ✅ Concluída (v0.2.3 validada no Switch) |
| **2b** | Conexão WebRTC com o console (biblioteca portada, SDP/ICE, DTLS, canais de dados) | "Conectado ao console" com os canais abertos | ✅ Parte 1 (v0.3.0): WebRTC validado em execução no Switch. 🧪 Parte 2 (v0.4.0): conexão com o Xbox em teste |
| **2c** | Vídeo: FFmpeg com NVDEC + desenho com deko3d | A tela do Xbox aparece no Switch (sem controle e sem som) | — |
| **3** | Controle + áudio | Jogar normalmente; teste de rajada de botões sem queda | — |
| **4** | Reconexão completa + HOME sem pausar | Sleep/resume, queda do Wi-Fi por alguns segundos e sessão longa (1h+) sem fechar o app; apertar HOME e voltar sem perder a conexão | — |
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
