// NXStream - inicialização compartilhada da libdatachannel
#pragma once
void rtc_comum_iniciar(void);    // liga o log da biblioteca (só na primeira vez)
void rtc_comum_finalizar(void);  // encerra as threads internas (ao fechar o app)
