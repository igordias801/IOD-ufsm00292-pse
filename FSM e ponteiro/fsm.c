
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#define STX 0x02
#define ETX 0x03
#define MAX_DADOS 256

typedef enum {
    ST_STX,
    ST_QTD,
    ST_DADOS,
    ST_CHK,
    ST_ETX,
    ST_DONE,
    ST_ERROR
} State;

typedef struct {
    State state;
    uint8_t qtd;
    uint8_t dados[MAX_DADOS];
    uint8_t pos;
    uint8_t checksum;
} FSM;

typedef State (*StateFunc)(FSM *fsm, uint8_t byte);

// --- Estados ---
State st_stx(FSM *fsm, uint8_t byte) {
    if (byte == STX) {
        fsm->checksum = 0;
        fsm->qtd = 0;
        fsm->pos = 0;
        return ST_QTD;
    }
    return ST_STX;
}

State st_qtd(FSM *fsm, uint8_t byte) {
    fsm->qtd = byte;
    fsm->checksum ^= byte;
    fsm->pos = 0;
    return (fsm->qtd > 0) ? ST_DADOS : ST_CHK;
}

State st_dados(FSM *fsm, uint8_t byte) {
    if (fsm->pos < MAX_DADOS) {
        fsm->dados[fsm->pos++] = byte;
        fsm->checksum ^= byte;
        if (fsm->pos >= fsm->qtd) return ST_CHK;
        return ST_DADOS;
    }
    // overflow de buffer
    return ST_ERROR;
}

State st_chk(FSM *fsm, uint8_t byte) {
    if (byte == fsm->checksum) return ST_ETX;
    return ST_ERROR;
}

State st_etx(FSM *fsm, uint8_t byte) {
    if (byte == ETX) return ST_DONE;
    return ST_ERROR;
}

// FIX: manter estados finais/erro como estados absorventes até reset explícito
State st_done(FSM *fsm, uint8_t byte) {
    (void)fsm; (void)byte;
    return ST_DONE; // permanecemos em DONE até reinit
}

State st_error(FSM *fsm, uint8_t byte) {
    (void)fsm; (void)byte;
    return ST_ERROR; // permanecemos em ERROR até reinit
}

// --- Tabela de transições ---
StateFunc state_table[] = {
    st_stx,
    st_qtd,
    st_dados,
    st_chk,
    st_etx,
    st_done,
    st_error
};

// --- Inicialização ---
void fsm_init(FSM *fsm) {
    memset(fsm, 0, sizeof(*fsm));
    fsm->state = ST_STX;
}

// --- Execução ---
void fsm_process(FSM *fsm, uint8_t byte) {
    fsm->state = state_table[fsm->state](fsm, byte);
}

// --- Testes ---
void test_valid_message() {
    FSM fsm;
    fsm_init(&fsm);

    uint8_t msg[] = {STX, 3, 'A', 'B', 'C', (3 ^ 'A' ^ 'B' ^ 'C'), ETX};
    for (size_t i = 0; i < sizeof(msg); i++) {
        fsm_process(&fsm, msg[i]);
    }
    assert(fsm.state == ST_DONE);
    assert(fsm.dados[0] == 'A');
    assert(fsm.dados[1] == 'B');
    assert(fsm.dados[2] == 'C');
}

void test_invalid_checksum() {
    FSM fsm;
    fsm_init(&fsm);

    uint8_t msg[] = {STX, 2, 'X', 'Y', 0x00, ETX};
    for (size_t i = 0; i < sizeof(msg); i++) {
        fsm_process(&fsm, msg[i]);
    }
    assert(fsm.state == ST_ERROR);
}

int main() {
    test_valid_message();
    test_invalid_checksum();
    printf("Todos os testes passaram!\n");
    return 0;
}
