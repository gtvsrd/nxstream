# Logo do NXStream

Anel bicolor com o **N** em vermelho e a diagonal do **X** em verde, cortando o anel na mesma direção.

| Arquivo | Uso |
|---|---|
| `nxstream-logo.svg` | Original vetorial, fundo transparente (tamanhos acima de 64 px) |
| `nxstream-logo-pequeno.svg` | Traço mais grosso, para 64 px ou menos |
| `nxstream-icone.svg` | Ícone do app: logo com margem sobre fundo preto |
| `nxstream-logo-{32,48,64,128,256,512,1024}.png` | Logo em PNG transparente (32–64 vêm da versão pequena) |
| `nxstream-icone-{256,512,1024}.png` | Ícone em PNG; o `icon.jpg` da raiz (ícone do `.nro`) é o de 256 |

Cores: vermelho `#F0353D`, verde `#2BD46A`, fundo `#000000`.

No app, o logo é desenhado pelo próprio código (`logo_ponto()` em `source/ui/tela.c`, com a mesma geometria do SVG). Se o desenho mudar, atualize os dois.

Para gerar os PNGs de novo: `rsvg-convert -w 256 -h 256 nxstream-logo.svg -o nxstream-logo-256.png` (e assim por diante).
