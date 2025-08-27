#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// ---------------- Mini Framework de Testes ----------------
#define checa(msg, cond) do { if (!(cond)) return msg; } while (0)
#define roda_teste(fn) do { char *msg = fn(); total_testes++; \
                            if (msg) return msg; } while (0)

int total_testes = 0;

// ---------------- Definições do Protocolo ----------------
#define FRAME_SOF 0x02
#define FRAME_EOF 0x03
#define FRAME_MAX 255

typedef enum {
    FRAME_PROGRESS,
    FRAME_OK,
    FRAME_FAIL
} FrameResult;

// Checksum simples: XOR
uint8_t gera_chk(const uint8_t* dados, uint8_t n) {
    uint8_t c = 0;
    for (int i = 0; i < n; i++) {
        c ^= dados[i];
    }
    return c;
}

// ---------------- Receptor (FSM) ----------------
typedef enum {
    RX_WAIT_SOF,
    RX_WAIT_LEN,
    RX_READ_DATA,
    RX_WAIT_CHK,
    RX_WAIT_EOF
} RxState;

typedef struct {
    RxState estado;
    uint8_t buf[FRAME_MAX];
    uint8_t pos;
    uint8_t tamanho;
    uint8_t calc_chk;
} FSM_Rx;

void rx_reset(FSM_Rx* f) {
    f->estado = RX_WAIT_SOF;
    f->pos = 0;
    f->tamanho = 0;
    f->calc_chk = 0;
    memset(f->buf, 0, FRAME_MAX);
}

FrameResult rx_handle_byte(FSM_Rx* f, uint8_t b) {
    switch (f->estado) {
        case RX_WAIT_SOF:
            if (b == FRAME_SOF) {
                f->estado = RX_WAIT_LEN;
            }
            break;

        case RX_WAIT_LEN:
            if (b > FRAME_MAX) {
                rx_reset(f);
                return FRAME_FAIL;
            }
            f->tamanho = b;
            f->pos = 0;
            f->calc_chk = 0;
            f->estado = (b == 0) ? RX_WAIT_CHK : RX_READ_DATA;
            break;

        case RX_READ_DATA:
            f->buf[f->pos++] = b;
            f->calc_chk ^= b;
            if (f->pos == f->tamanho) {
                f->estado = RX_WAIT_CHK;
            }
            break;

        case RX_WAIT_CHK:
            if (b == f->calc_chk) {
                f->estado = RX_WAIT_EOF;
            } else {
                rx_reset(f);
                return FRAME_FAIL;
            }
            break;

        case RX_WAIT_EOF:
            rx_reset(f);
            if (b == FRAME_EOF) return FRAME_OK;
            else return FRAME_FAIL;
    }
    return FRAME_PROGRESS;
}

// ---------------- Transmissor ----------------
typedef struct {
    const uint8_t* dados;
    uint8_t qtd;
} TxPacket;

void tx_compose(TxPacket* tx, const uint8_t* dados, uint8_t n, uint8_t* buf) {
    uint8_t idx = 0;
    buf[idx++] = FRAME_SOF;
    buf[idx++] = n;
    for (int i = 0; i < n; i++) {
        buf[idx++] = dados[i];
    }
    buf[idx++] = gera_chk(dados, n);
    buf[idx++] = FRAME_EOF;
    tx->dados = dados;
    tx->qtd = n;
}

// ---------------- Testes ----------------
static char* teste_rx_valido() {
    FSM_Rx rx;
    rx_reset(&rx);

    uint8_t msg[] = {'O','K','!'};
    uint8_t chk = gera_chk(msg, 3);
    uint8_t quadro[] = {FRAME_SOF, 3, 'O','K','!', chk, FRAME_EOF};

    FrameResult r;
    for (int i = 0; i < sizeof(quadro)-1; i++) {
        r = rx_handle_byte(&rx, quadro[i]);
        checa("Falha: deveria estar em progresso", r == FRAME_PROGRESS);
    }
    r = rx_handle_byte(&rx, quadro[sizeof(quadro)-1]);
    checa("Falha: quadro válido não reconhecido", r == FRAME_OK);
    return 0;
}

static char* teste_rx_chk_errado() {
    FSM_Rx rx; rx_reset(&rx);
    uint8_t quadro[] = {FRAME_SOF, 2, 'A','B', 0x99, FRAME_EOF};

    rx_handle_byte(&rx, quadro[0]);
    rx_handle_byte(&rx, quadro[1]);
    rx_handle_byte(&rx, quadro[2]);
    rx_handle_byte(&rx, quadro[3]);
    FrameResult r = rx_handle_byte(&rx, quadro[4]);

    checa("Falha: checksum inválido não detectado", r == FRAME_FAIL);
    return 0;
}

static char* teste_tx_monta_pacote() {
    TxPacket tx;
    uint8_t dados[] = {1,2,3,4};
    uint8_t buf[4+5];
    tx_compose(&tx, dados, 4, buf);

    checa("Erro STX", buf[0] == FRAME_SOF);
    checa("Erro LEN", buf[1] == 4);
    checa("Erro dado[0]", buf[2] == 1);
    checa("Erro dado[3]", buf[5] == 4);
    checa("Erro CHK", buf[6] == gera_chk(dados,4));
    checa("Erro EOF", buf[7] == FRAME_EOF);
    return 0;
}

// ---------------- Runner ----------------
static char* roda_todos(void) {
    roda_teste(teste_rx_valido);
    roda_teste(teste_rx_chk_errado);
    roda_teste(teste_tx_monta_pacote);
    return 0;
}

int main(void) {
    char* res = roda_todos();
    printf("\n     Resultado \n");
    if (res) {
        printf("FALHOU: %s\n", res);
    } else {
        printf("TODOS OS TESTES PASSARAM\n");
    }
    printf("Testes executados: %d\n", total_testes);
    return res != 0;
}
