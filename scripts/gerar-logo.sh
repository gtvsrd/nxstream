#!/bin/sh
# Gera, a partir dos SVG em assets/logo, tudo o que usa o logo:
#   - os PNG (logo de 32 a 1024 px, ícone de 256 a 1024 px)
#   - icon.jpg (ícone do .nro: 256x256, JPEG sem perdas visíveis)
#   - source/ui/logo_png.h (o NXStream-logo-512.png embutido no app; as telas
#     desenham ESSE arquivo, reduzido para o tamanho de cada lugar)
# Precisa de rsvg-convert e python3 com Pillow. Rodar na raiz do repositório
# sempre que o desenho do logo mudar.
set -e
cd "$(dirname "$0")/.."
L=assets/logo
for s in 128 256 512 1024; do rsvg-convert -w $s -h $s $L/NXStream-logo.svg -o $L/NXStream-logo-$s.png; done
for s in 32 48 64; do rsvg-convert -w $s -h $s $L/NXStream-logo-pequeno.svg -o $L/NXStream-logo-$s.png; done
for s in 256 512 1024; do rsvg-convert -w $s -h $s $L/NXStream-icone.svg -o $L/NXStream-icone-$s.png; done
python3 - <<'PY'
from PIL import Image
Image.open('assets/logo/NXStream-icone-256.png').convert('RGB').save('icon.jpg', quality=100, subsampling=0, optimize=True)
dados = open('assets/logo/NXStream-logo-512.png', 'rb').read()
with open('source/ui/logo_png.h', 'w') as f:
    f.write('// Gerado por scripts/gerar-logo.sh a partir de assets/logo/NXStream-logo-512.png. Não editar.\n')
    f.write('static const unsigned char LOGO_PNG[%d] = {\n' % len(dados))
    for i in range(0, len(dados), 20):
        f.write(''.join('%d,' % b for b in dados[i:i+20]) + '\n')
    f.write('};\n')
print('logo_png.h:', len(dados), 'bytes')
PY
