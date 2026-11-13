#!/bin/sh
# Traduz um crash report do Atmosphère em funções e linhas do código.
#
# Uso: sh scripts/traduzir-crash.sh <NXStream.elf> <crash_report.log>
#
# O .elf vem do artifact "NXStream-elf-para-crash-reports" do MESMO build do
# .nro que travou. O script confere o identificador do build (Module Id) do
# relatório com o do .elf e avisa se não baterem.
# Precisa do devkitPro (aarch64-none-elf-addr2line e readelf).
set -eu
ELF=$1
RELATORIO=$2
BIN=${DEVKITPRO:-/opt/devkitpro}/devkitA64/bin

id_relatorio=$(grep -m1 "Module Id:" "$RELATORIO" | awk '{print $3}' | cut -c1-40 | tr 'A-F' 'a-f')
id_elf=$("$BIN/aarch64-none-elf-readelf" -n "$ELF" | awk '/Build ID/ {print $3}' | cut -c1-40)
echo "Module Id do relatorio: $id_relatorio"
echo "Build ID do .elf:       $id_elf"
if [ "$id_relatorio" = "$id_elf" ]; then
    echo "OK: o .elf e do mesmo build que travou"
else
    echo "ATENCAO: o .elf NAO e do mesmo build; os nomes abaixo podem estar errados"
fi
echo

# Endereços "NXStream + 0x..." (aceita também o nome antigo em minúsculas) na ordem do relatório (thread que travou primeiro)
traduzir() {
    grep -io "$1.*(NXStream + 0x[0-9a-f]*)" "$RELATORIO" | sed 's/.*[nN][xX][sS][tT][rR][eE][aA][mM] + \(0x[0-9a-f]*\)).*/\1/'
}
echo "== Thread que travou (PC, LR e pilha) =="
sed -n '/^Crashed Thread Info:/,/^    Stack Dump:/p' "$RELATORIO" \
    | grep -io "NXStream + 0x[0-9a-f]*" | awk '{print $3}' \
    | while read -r endereco; do
        printf '%-10s ' "$endereco"
        "$BIN/aarch64-none-elf-addr2line" -f -C -p -e "$ELF" "$endereco"
    done
