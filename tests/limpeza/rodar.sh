#!/bin/sh
# Teste de limpeza da libdatachannel, no PC (Linux).
#
# Compila a libdatachannel no MESMO commit usado no Switch, com os patches de
# compat/patches/aplicar.py (os específicos do Switch ficam desligados; o da
# limpeza é ligado por NXSTREAM_TESTE_PC) e roda tests/limpeza/teste.c:
# conexão com as mesmas trilhas e canais do app, fechada na mesma ordem, e
# rtcCleanup(). Falha se rtcCleanup() travar ou demorar mais de 5 s.
#
# Precisa de: git, cmake, g++, libssl-dev.  Uso: sh tests/limpeza/rodar.sh
set -eu
RAIZ=$(cd "$(dirname "$0")/../.." && pwd)
TRABALHO=${TMPDIR:-/tmp}/nxstream-teste-limpeza
LDC_COMMIT=$(sed -n 's/^LDC_COMMIT=//p' "$RAIZ/scripts/compilar-deps.sh")

rm -rf "$TRABALHO" && mkdir -p "$TRABALHO" && cd "$TRABALHO"
git init --quiet ldc
git -C ldc remote add origin https://github.com/paullouisageneau/libdatachannel
git -C ldc fetch --quiet --depth 1 origin "$LDC_COMMIT"
git -C ldc checkout --quiet FETCH_HEAD
git -C ldc submodule update --quiet --init --depth 1 deps/libjuice deps/libsrtp deps/usrsctp deps/plog deps/json
python3 "$RAIZ/compat/patches/aplicar.py" ldc

cmake -S ldc -B ldc/build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
    -DNO_WEBSOCKET=ON -DNO_EXAMPLES=ON -DNO_TESTS=ON -DCMAKE_CXX_FLAGS=-DNXSTREAM_TESTE_PC > cmake.log
cmake --build ldc/build -j"$(nproc)" > build.log 2>&1 || { tail -30 build.log; exit 1; }

B=ldc/build
gcc -o teste "$RAIZ/tests/limpeza/teste.c" -Ildc/include "$B/libdatachannel.a" "$B/deps/libjuice/libjuice.a" \
    "$B/deps/libsrtp/libsrtp2.a" "$B/deps/usrsctp/usrsctplib/libusrsctp.a" -lssl -lcrypto -lstdc++ -lpthread -lm
for rodada in 1 2 3; do
    echo "== rodada $rodada =="
    ./teste
done
