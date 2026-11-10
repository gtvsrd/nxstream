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

## Fases

Cada fase termina com um `.nro` testado no Switch antes de seguir para a próxima.

| Fase | Entrega | Critério de aceite (teste no Switch) | Status |
|---|---|---|---|
| **0** | Build automático + app de diagnóstico | Abre em title mode, mostra IP, registra eventos de sleep/resume | ✅ Concluída (v0.0.2 em teste) |
| **1** | Login por código + lista de consoles | Logar uma vez; reabrir o app sem pedir login; seu Xbox aparece listado com estado | 🧪 v0.1.0 em teste (branch `fase-1`) |
| **2** | Sessão + WebRTC + vídeo | A tela do Xbox aparece no Switch (sem controle e sem som) | — |
| **3** | Controle + áudio | Jogar normalmente; teste de rajada de botões sem queda | — |
| **4** | Reconexão completa | Sleep/resume, queda do Wi-Fi por alguns segundos e sessão longa (1h+) sem fechar o app | — |
| **5** | Vibração, configurações (qualidade, deadzone, mapeamento), acabamento | Uso normal do dia a dia | — |

A fase 2 é a mais pesada e arriscada, porque junta WebRTC, criptografia e decodificação de vídeo. Espera-se várias rodadas de teste e ajuste nela.

## Riscos conhecidos

| Risco | Impacto | Mitigação |
|---|---|---|
| Microsoft muda endpoints ou formatos da API | App para de conectar | Endereços e versões isolados em um único arquivo (`source/api/config.h`). Respostas inesperadas vão para o log. |
| Portar a biblioteca WebRTC para o Switch | Atraso na fase 2 | Testar libdatachannel primeiro; libpeer como alternativa. Ver [ARQUITETURA.md](ARQUITETURA.md#webrtc). |
| Sleep do Switch derruba todos os sockets | Conexão morre ao dormir | Máquina de estados recria WebRTC/sessão ao acordar (fase 4). O tempo de volta do Wi-Fi é medido na fase 0. |
| Token de renovação dá acesso à conta Xbox | Segurança da conta | Fica só no cartão SD, nunca é enviado a outro lugar além dos servidores da Microsoft. Nunca é escrito no log. |
| Build do FFmpeg é demorado | Builds lentos no CI | Cache do FFmpeg compilado no GitHub Actions. |

## Documentos relacionados

- [ARQUITETURA.md](ARQUITETURA.md): módulos, threads, máquina de estados e bibliotecas
- [PROTOCOLO.md](PROTOCOLO.md): como o Remote Play funciona (autenticação, API, WebRTC)
- [DESENVOLVIMENTO.md](DESENVOLVIMENTO.md): build, testes no Switch, logs e convenções
