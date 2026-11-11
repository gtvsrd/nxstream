// Registra no log quando o app é encerrado por uma exceção C++ não tratada.
//
// As threads da libdatachannel podem lançar exceções; sem tratamento, o C++
// chama std::terminate() -> abort(), e no Switch o app simplesmente fecha,
// sem tela de erro e sem crash report do Atmosphère (log da v0.4.3).
// Compilado sem exceções (-fno-exceptions): o tipo da exceção é obtido pela
// ABI do C++, sem try/catch.
#include <cstdlib>
#include <cxxabi.h>
#include <exception>
#include <typeinfo>

extern "C" {
#include "log.h"
}

static void ao_terminar() {
    const std::type_info *tipo = abi::__cxa_current_exception_type();
    if (tipo) {
        int status = 0;
        char *nome = abi::__cxa_demangle(tipo->name(), nullptr, nullptr, &status);
        LOG("ERRO FATAL: excecao C++ nao tratada (%s); o app vai fechar", nome ? nome : tipo->name());
        std::free(nome);
    } else {
        LOG("ERRO FATAL: std::terminate chamado sem excecao ativa; o app vai fechar");
    }
    log_fechar();
    std::abort();
}

extern "C" void terminar_instalar(void) {
    std::set_terminate(ao_terminar);
}
