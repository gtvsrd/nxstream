// Funções de plataforma que o mbedTLS 3 pede ao app (ver scripts/compilar-deps.sh):
//   MBEDTLS_PLATFORM_MS_TIME_ALT  -> mbedtls_ms_time()
//   MBEDTLS_ENTROPY_HARDWARE_ALT  -> mbedtls_hardware_poll()
#include <mbedtls/build_info.h>
#include <mbedtls/platform_time.h>
#include <stddef.h>
#include <switch.h>

#if MBEDTLS_VERSION_MAJOR < 3
#error "Compilando com o mbedTLS 2.x da imagem: deps/prefix/include deveria vir antes"
#endif

// Relógio em milissegundos (monotônico, usado nos timeouts do DTLS)
mbedtls_ms_time_t mbedtls_ms_time(void) {
    return (mbedtls_ms_time_t)(armTicksToNs(armGetSystemTick()) / 1000000ULL);
}

// Entropia para chaves e certificados: gerador seguro do sistema do Switch
int mbedtls_hardware_poll(void *dados, unsigned char *saida, size_t tamanho, size_t *gerado) {
    (void)dados;
    randomGet(saida, tamanho);
    *gerado = tamanho;
    return 0;
}
