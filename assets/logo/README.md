# Logo do NXStream

Anel bicolor com o **N** em vermelho e a diagonal do **X** em verde, cortando o anel na mesma direção.

| Arquivo | Uso |
|---|---|
| `NXStream-logo.svg` | Original vetorial, fundo transparente (tamanhos acima de 64 px) |
| `NXStream-logo-pequeno.svg` | Traço mais grosso, para 64 px ou menos |
| `NXStream-icone.svg` | Ícone do app: logo com margem sobre fundo preto |
| `NXStream-logo-{32,48,64,128,256,512,1024}.png` | Logo em PNG transparente (32–64 vêm da versão pequena) |
| `NXStream-icone-{256,512,1024}.png` | Ícone em PNG; o `icon.jpg` da raiz (ícone do `.nro`) é o de 256 |

Cores: vermelho `#F0353D`, verde `#2BD46A`, fundo `#000000`.

No app, as telas desenham o próprio `NXStream-logo-512.png`, embutido em `source/ui/logo_png.h` e reduzido para cada tamanho.

Se o desenho mudar, edite os SVG e rode `sh scripts/gerar-logo.sh` na raiz: ele refaz todos os PNG, o `icon.jpg` e o `logo_png.h`.
