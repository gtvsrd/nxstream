// Reproduz o fechamento do NXStream: conexão com as mesmas trilhas e canais,
// encerramento na mesma ordem do app (conexao_encerrar) e rtcCleanup().
// Falha se rtcCleanup() não voltar em 15 s (impasse).
#include <rtc/rtc.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void ao_alarme(int s) { (void)s; const char m[] = "FALHOU: rtcCleanup travou (impasse)\n"; write(2, m, sizeof m - 1); _exit(2); }
static volatile int coleta = 0;
static void ao_coleta(int pc, rtcGatheringState e, void *p) { (void)pc; (void)p; if (e == RTC_GATHERING_COMPLETE) coleta = 1; }
static void ao_quadro(int tr, const char *d, int n, const rtcFrameInfo *i, void *p) { (void)tr; (void)d; (void)n; (void)i; (void)p; }

int main(void) {
    rtcInitLogger(RTC_LOG_WARNING, NULL);
    rtcConfiguration cfg; memset(&cfg, 0, sizeof cfg); cfg.disableAutoNegotiation = 1;
    int pc = rtcCreatePeerConnection(&cfg);
    rtcSetGatheringStateChangeCallback(pc, ao_coleta);
    int audio = rtcAddTrack(pc, "m=audio 9 UDP/TLS/RTP/SAVPF 111\r\nc=IN IP4 0.0.0.0\r\na=mid:0\r\na=sendrecv\r\na=rtpmap:111 opus/48000/2\r\n");
    int video = rtcAddTrack(pc, "m=video 9 UDP/TLS/RTP/SAVPF 102\r\nc=IN IP4 0.0.0.0\r\na=mid:1\r\na=recvonly\r\na=rtpmap:102 H264/90000\r\n");
    rtcSetUserPointer(video, (void *)100);
    rtcSetH264Depacketizer(video, RTC_NAL_SEPARATOR_LONG_START_SEQUENCE);
    rtcChainRtcpReceivingSession(video);
    rtcSetFrameCallback(video, ao_quadro);
    const char *nomes[] = {"chat", "control", "input", "message"};
    int dc[4];
    for (int i = 0; i < 4; i++) { rtcDataChannelInit init; memset(&init, 0, sizeof init); dc[i] = rtcCreateDataChannelEx(pc, nomes[i], &init); }
    rtcSetLocalDescription(pc, "offer");
    for (int i = 0; i < 50 && !coleta; i++) usleep(100000);
    printf("conexao criada (pc %d, %d trilhas, 4 canais), coleta %s\n", pc, audio >= 0 && video >= 0 ? 2 : 0, coleta ? "ok" : "incompleta");

    // Mesma ordem de conexao_encerrar()
    rtcClosePeerConnection(pc);
    for (int i = 0; i < 4; i++) rtcDeleteDataChannel(dc[i]);
    rtcDeleteTrack(video); rtcDeleteTrack(audio);
    rtcDeletePeerConnection(pc);

    signal(SIGALRM, ao_alarme); alarm(15);
    struct timespec a, b; clock_gettime(CLOCK_MONOTONIC, &a);
    rtcCleanup();
    clock_gettime(CLOCK_MONOTONIC, &b);
    double t = (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9;
    if (t > 5.0) { printf("FALHOU: rtcCleanup demorou %.2f s (impasse ate o timeout interno)\n", t); return 1; }
    printf("OK: rtcCleanup voltou em %.2f s\n", t);
    return 0;
}
