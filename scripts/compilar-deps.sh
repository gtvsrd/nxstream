#!/bin/sh
# Compila as dependências do WebRTC para o Switch e instala em deps/prefix:
#   - mbedTLS 3.6.2 (a imagem do devkitPro só tem a 2.28)
#   - libdatachannel + libjuice + libSRTP + usrsctp
#
# Precisa do devkitPro (roda dentro da imagem devkitpro/devkita64 no CI).
# Detalhes de cada ajuste: docs/ARQUITETURA.md (seção WebRTC) e compat/README.md.
set -eu

RAIZ=$(cd "$(dirname "$0")/.." && pwd)
DESTINO=$RAIZ/deps/prefix
FONTES=$RAIZ/deps/fontes

# Versões fixas: a combinação abaixo foi a validada no experimento da fase 2b
MBEDTLS_VERSAO=v3.6.2
LDC_COMMIT=2afeedc70878043bcd59d2d726e1476824f687c3

TOOLCHAIN=$DEVKITPRO/cmake/Switch.cmake
JOBS=$(nproc 2>/dev/null || echo 2)

rm -rf "$FONTES" "$DESTINO"
mkdir -p "$FONTES" "$DESTINO"

echo "== Baixando fontes =="
git clone --quiet --depth 1 --branch "$MBEDTLS_VERSAO" https://github.com/Mbed-TLS/mbedtls "$FONTES/mbedtls"
git -C "$FONTES/mbedtls" submodule update --quiet --init --depth 1

git init --quiet "$FONTES/libdatachannel"
git -C "$FONTES/libdatachannel" remote add origin https://github.com/paullouisageneau/libdatachannel
git -C "$FONTES/libdatachannel" fetch --quiet --depth 1 origin "$LDC_COMMIT"
git -C "$FONTES/libdatachannel" checkout --quiet FETCH_HEAD
git -C "$FONTES/libdatachannel" submodule update --quiet --init --depth 1 \
    deps/libjuice deps/libsrtp deps/usrsctp deps/plog deps/json

echo "== mbedTLS 3 =="
cd "$FONTES/mbedtls"
python3 scripts/config.py set   MBEDTLS_SSL_DTLS_SRTP           # chaves do SRTP via DTLS (WebRTC)
python3 scripts/config.py unset MBEDTLS_SSL_PROTO_TLS1_3        # WebRTC usa DTLS 1.2
python3 scripts/config.py unset MBEDTLS_NET_C                   # sem sockets do mbedTLS
python3 scripts/config.py unset MBEDTLS_TIMING_C
python3 scripts/config.py unset MBEDTLS_PSA_ITS_FILE_C
python3 scripts/config.py unset MBEDTLS_PSA_CRYPTO_STORAGE_C
python3 scripts/config.py set   MBEDTLS_NO_PLATFORM_ENTROPY
python3 scripts/config.py set   MBEDTLS_ENTROPY_HARDWARE_ALT    # app fornece mbedtls_hardware_poll()
python3 scripts/config.py set   MBEDTLS_PLATFORM_MS_TIME_ALT    # app fornece mbedtls_ms_time()
cmake -S . -B build -G "Unix Makefiles" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$DESTINO" \
    -DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF -DUSE_SHARED_MBEDTLS_LIBRARY=OFF \
    -DMBEDTLS_FATAL_WARNINGS=OFF
cmake --build build -j"$JOBS"
cmake --install build

echo "== libdatachannel =="
python3 "$RAIZ/compat/patches/aplicar.py" "$FONTES/libdatachannel"
cd "$FONTES/libdatachannel"
cmake -S . -B build -G "Unix Makefiles" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$DESTINO" \
    -DCMAKE_PROJECT_INCLUDE="$RAIZ/compat/compat.cmake" \
    -DMbedTLS_INCLUDE_DIR="$DESTINO/include" \
    -DMbedTLS_LIBRARY="$DESTINO/lib/libmbedtls.a" \
    -DMbedX509_LIBRARY="$DESTINO/lib/libmbedx509.a" \
    -DMbedCrypto_LIBRARY="$DESTINO/lib/libmbedcrypto.a" \
    -DMBEDTLS_INCLUDE_DIRS="$DESTINO/include" \
    -DMBEDTLS_LIBRARY="$DESTINO/lib/libmbedtls.a" \
    -DMBEDX509_LIBRARY="$DESTINO/lib/libmbedx509.a" \
    -DMBEDCRYPTO_LIBRARY="$DESTINO/lib/libmbedcrypto.a" \
    -DBUILD_SHARED_LIBS=OFF -DUSE_MBEDTLS=ON -DNO_WEBSOCKET=ON \
    -DNO_EXAMPLES=ON -DNO_TESTS=ON
cmake --build build -j"$JOBS"

# Instala só o necessário: cabeçalhos rtc/ e as bibliotecas estáticas
mkdir -p "$DESTINO/include" "$DESTINO/lib"
cp -r include/rtc "$DESTINO/include/"
cp build/libdatachannel.a "$DESTINO/lib/"
cp build/deps/libjuice/libjuice.a "$DESTINO/lib/"
cp build/deps/libsrtp/libsrtp2.a "$DESTINO/lib/"
cp build/deps/usrsctp/usrsctplib/libusrsctp.a "$DESTINO/lib/"

rm -rf "$FONTES"
echo "== Instalado em $DESTINO =="
ls -l "$DESTINO/lib"
