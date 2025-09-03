#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ===========================================================
   Mini-framework de testes (minUnit)
   =========================================================== */
#define verifica(msg, cond) do { if(!(cond)) return msg; } while(0)
#define executa_teste(fn) do { char* _m = fn(); total_testes++; if(_m) return _m; } while(0)
static int total_testes = 0;

/* ===========================================================
   Protocolo e utilidades
   =========================================================== */
#define FRAME_SOF   0x02
#define FRAME_EOF   0x03
#define FRAME_ACK   0x06
#define FRAME_MAX   255

static uint8_t xor_chk(const uint8_t* d, uint8_t n){
    uint8_t c = 0; for(uint8_t i=0;i<n;i++) c ^= d[i]; return c;
}

/* ===========================================================
   Protothreads (macro-based, estilo Adam Dunkels — simplificado)
   =========================================================== */
typedef struct { int lc; } pt_t;

#define PT_BEGIN(pt)   switch((pt)->lc) { case 0:
#define PT_END(pt)     } (void)0
#define PT_YIELD(pt)   do{ (pt)->lc=__LINE__; return; case __LINE__:; }while(0)
#define PT_WAIT_UNTIL(pt, cond) do{ (pt)->lc=__LINE__; case __LINE__: if(!(cond)) return; }while(0)
#define PT_RESTART(pt) do{ (pt)->lc=0; return; }while(0)
#define PT_INIT(pt)    do{ (pt)->lc=0; }while(0)

/* ===========================================================
   Canal simulado (fila circular não-bloqueante)
   =========================================================== */
#define QCAP 512
typedef struct {
    uint8_t buf[QCAP];
    int h, t, sz;
} queue_t;

static void q_init(queue_t* q){ q->h=q->t=q->sz=0; }
static bool q_push(queue_t* q, uint8_t b){
    if(q->sz==QCAP) return false;
    q->buf[q->t]=b; q->t=(q->t+1)%QCAP; q->sz++; return true;
}
static bool q_pop(queue_t* q, uint8_t* out){
    if(q->sz==0) return false;
    *out=q->buf[q->h]; q->h=(q->h+1)%QCAP; q->sz--; return true;
}
static bool q_peek(queue_t* q, uint8_t* out){
    if(q->sz==0) return false; *out=q->buf[q->h]; return true;
}
static int  q_size(queue_t* q){ return q->sz; }

/* Canais: dados (TX->RX) e controle/ACK (RX->TX) */
static queue_t ch_data, ch_ctrl;

/* “Camada física” simulada: TX escreve no canal de dados; RX lê. */
static bool phy_send_byte(uint8_t b){ return q_push(&ch_data, b); }
static bool phy_recv_byte(uint8_t* b){ return q_pop(&ch_data, b); }
/* Canal de retorno (ACK): RX → TX */
static bool ctrl_send_ack(uint8_t b){ return q_push(&ch_ctrl, b); }
static bool ctrl_recv_ack(uint8_t* b){ return q_pop(&ch_ctrl, b); }

/* ===========================================================
   FSM do Receptor (dentro de uma protothread)
   =========================================================== */
typedef enum {
    RX_WAIT_SOF,
    RX_WAIT_LEN,
    RX_READ_DATA,
    RX_WAIT_CHK,
    RX_WAIT_EOF
} rx_state_e;

typedef struct {
    pt_t       pt;
    rx_state_e st;
    uint8_t    len, idx, chk;
    uint8_t    payload[FRAME_MAX];
} rx_ctx_t;

static void rx_init(rx_ctx_t* rx){
    PT_INIT(&rx->pt);
    rx->st = RX_WAIT_SOF;
    rx->len=0; rx->idx=0; rx->chk=0;
    memset(rx->payload, 0, sizeof(rx->payload));
}

/* Protothread receptora: consome bytes, valida, envia ACK ao final */
static void rx_thread(rx_ctx_t* rx){
    uint8_t b;
    PT_BEGIN(&rx->pt);

    while(1){
        /* Espera byte disponível no canal de dados */
        PT_WAIT_UNTIL(&rx->pt, q_size(&ch_data) > 0);
        phy_recv_byte(&b);

        switch(rx->st){
            case RX_WAIT_SOF:
                if(b==FRAME_SOF){ rx->st=RX_WAIT_LEN; rx->idx=0; rx->chk=0; }
                /* lixo é ignorado */
                break;

            case RX_WAIT_LEN:
                if(b>FRAME_MAX){ rx->st=RX_WAIT_SOF; } /* proteção */
                else{
                    rx->len=b;
                    rx->idx=0; rx->chk=0;
                    rx->st = (rx->len==0) ? RX_WAIT_CHK : RX_READ_DATA;
                }
                break;

            case RX_READ_DATA:
                rx->payload[rx->idx++] = b;
                rx->chk ^= b;
                if(rx->idx == rx->len) rx->st = RX_WAIT_CHK;
                break;

            case RX_WAIT_CHK:
                if(b == rx->chk) rx->st=RX_WAIT_EOF;
                else rx->st=RX_WAIT_SOF; /* erro => reset (sem ACK) */
                break;

            case RX_WAIT_EOF:
                if(b==FRAME_EOF){
                    /* sucesso → envia ACK e volta ao início */
                    ctrl_send_ack(FRAME_ACK);
                }
                rx->st = RX_WAIT_SOF;
                break;
        }

        /* coopera com o escalonador */
        PT_YIELD(&rx->pt);
    }

    PT_END(&rx->pt);
}

/* ===========================================================
   Protothread do Transmissor (envio + espera ACK + retransmissão)
   =========================================================== */
typedef enum {
    TX_IDLE,
    TX_SEND_SOF,
    TX_SEND_LEN,
    TX_SEND_DATA,
    TX_SEND_CHK,
    TX_SEND_EOF,
    TX_WAIT_ACK,
    TX_DONE,
    TX_FAIL
} tx_state_e;

typedef struct {
    pt_t       pt;
    tx_state_e st;
    const uint8_t* data;
    uint8_t    len, idx, chk;
    int        retries;
    int        ack_deadline; /* “tick” limite para receber ACK */
    bool       inject_error_once; /* para testes de corrupção */
} tx_ctx_t;

/* parâmetros de timeout/retransmissão */
#define MAX_RETRIES   3
#define ACK_TIMEOUT_TICKS  30 /* “ticks” simulados */

/* tick global (simulado no laço do teste) */
static int g_tick = 0;

static void tx_init(tx_ctx_t* tx, const uint8_t* d, uint8_t n){
    PT_INIT(&tx->pt);
    tx->st = TX_IDLE;
    tx->data = d; tx->len = n;
    tx->idx = 0; tx->chk = xor_chk(d,n);
    tx->retries = 0;
    tx->inject_error_once = false;
    tx->ack_deadline = 0;
}

static void tx_set_inject_error(tx_ctx_t* tx, bool once){ tx->inject_error_once = once; }

/* envia um byte, com opção de corromper UM byte (teste) */
static bool tx_phy_send_with_optional_corruption(tx_ctx_t* tx, uint8_t b){
    if(tx->inject_error_once){
        /* corrompe o primeiro byte de dados que sair */
        if(tx->st==TX_SEND_DATA && tx->idx==0){
            b ^= 0xFF; /* inverte bits pra forçar erro */
            tx->inject_error_once = false; /* só uma vez */
        }
    }
    return phy_send_byte(b);
}

/* Protothread transmissora */
static void tx_thread(tx_ctx_t* tx){
    uint8_t ack;
    PT_BEGIN(&tx->pt);

    while(1){
        switch(tx->st){
            case TX_IDLE:
                tx->idx = 0;
                tx->chk = xor_chk(tx->data, tx->len);
                tx->st  = TX_SEND_SOF;
                break;

            case TX_SEND_SOF:
                PT_WAIT_UNTIL(&tx->pt, tx_phy_send_with_optional_corruption(tx, FRAME_SOF));
                tx->st = TX_SEND_LEN;
                break;

            case TX_SEND_LEN:
                PT_WAIT_UNTIL(&tx->pt, tx_phy_send_with_optional_corruption(tx, tx->len));
                tx->st = (tx->len==0) ? TX_SEND_CHK : TX_SEND_DATA;
                break;

            case TX_SEND_DATA:
                PT_WAIT_UNTIL(&tx->pt, tx_phy_send_with_optional_corruption(tx, tx->data[tx->idx]));
                tx->idx++;
                if(tx->idx>=tx->len) tx->st=TX_SEND_CHK;
                break;

            case TX_SEND_CHK:
                PT_WAIT_UNTIL(&tx->pt, tx_phy_send_with_optional_corruption(tx, tx->chk));
                tx->st = TX_SEND_EOF;
                break;

            case TX_SEND_EOF:
                PT_WAIT_UNTIL(&tx->pt, tx_phy_send_with_optional_corruption(tx, FRAME_EOF));
                /* inicia espera por ACK */
                tx->ack_deadline = g_tick + ACK_TIMEOUT_TICKS;
                tx->st = TX_WAIT_ACK;
                break;

            case TX_WAIT_ACK:
                /* recebeu algo no canal de controle? */
                if(ctrl_recv_ack(&ack)){
                    if(ack==FRAME_ACK){
                        tx->st = TX_DONE;
                    }else{
                        /* qualquer coisa != ACK trata como falha e reenvia */
                        tx->st = TX_IDLE;
                        tx->retries++;
                        if(tx->retries>MAX_RETRIES) tx->st=TX_FAIL;
                    }
                }else{
                    /* checa timeout */
                    if(g_tick >= tx->ack_deadline){
                        tx->retries++;
                        if(tx->retries > MAX_RETRIES){
                            tx->st = TX_FAIL;
                        }else{
                            tx->st = TX_IDLE; /* retransmite tudo */
                        }
                    }
                }
                break;

            case TX_DONE:
                /* fica concluído; coopera e mantém estado */
                PT_YIELD(&tx->pt);
                break;

            case TX_FAIL:
                /* falhou; idem */
                PT_YIELD(&tx->pt);
                break;
        }

        /* cooperação a cada passo */
        PT_YIELD(&tx->pt);
    }

    PT_END(&tx->pt);
}

/* Helpers de status para os testes */
static bool tx_is_done(const tx_ctx_t* tx){ return tx->st==TX_DONE; }
static bool tx_is_fail(const tx_ctx_t* tx){ return tx->st==TX_FAIL; }
static int  tx_retry_count(const tx_ctx_t* tx){ return tx->retries; }

/* ===========================================================
   “Scheduler” super simples para rodar as duas protothreads
   =========================================================== */
static void scheduler_step(rx_ctx_t* rx, tx_ctx_t* tx){
    /* ordem: RX lê o que tiver, TX envia/espera ACK; ambos cooperam */
    rx_thread(rx);
    tx_thread(tx);
    g_tick++; /* passa o tempo */
}

/* ===========================================================
   TESTES
   =========================================================== */

/* 1) Caminho feliz: sem corrupção, RX reconhece e manda ACK; TX conclui */
static char* teste_ok_sem_retransmissao(void){
    q_init(&ch_data); q_init(&ch_ctrl); g_tick = 0;
    rx_ctx_t rx; tx_ctx_t tx;
    rx_init(&rx);
    const uint8_t payload[] = { 'T','D','D' };
    tx_init(&tx, payload, sizeof(payload));

    /* roda até TX concluir ou falhar, com limite para evitar loop infinito */
    for(int i=0; i<2000 && !tx_is_done(&tx) && !tx_is_fail(&tx); i++){
        scheduler_step(&rx, &tx);
    }

    verifica("TX não concluiu (caminho feliz)", tx_is_done(&tx));
    verifica("Houve retransmissão indevida", tx_retry_count(&tx)==0);
    return 0;
}

/* 2) Corrupção na primeira tentativa: RX não manda ACK; TX retransmite e depois conclui */
static char* teste_corrupcao_uma_vez_reenvia(void){
    q_init(&ch_data); q_init(&ch_ctrl); g_tick = 0;
    rx_ctx_t rx; tx_ctx_t tx;
    rx_init(&rx);
    const uint8_t payload[] = { 0xAA, 0xBB, 0xCC };
    tx_init(&tx, payload, sizeof(payload));
    tx_set_inject_error(&tx, true); /* corromper a 1ª tentativa */

    for(int i=0; i<4000 && !tx_is_done(&tx) && !tx_is_fail(&tx); i++){
        scheduler_step(&rx, &tx);
    }

    verifica("TX deveria concluir após retransmitir", tx_is_done(&tx));
    verifica("Esperava exatamente 1 retransmissão", tx_retry_count(&tx)==1);
    return 0;
}

/* 3) Sem ACK (simulado): RX nunca responde ⇒ TX estoura timeout e falha */
static char* teste_sem_ack_timeout_falha(void){
    q_init(&ch_data); q_init(&ch_ctrl); g_tick = 0;
    rx_ctx_t rx; tx_ctx_t tx;
    rx_init(&rx);
    const uint8_t payload[] = { 1,2,3,4,5 };
    tx_init(&tx, payload, sizeof(payload));

    /* “Desabilita” RX: não chamaremos rx_thread no scheduler. */
    PT_INIT(&rx.pt); /* rx_init já fez, mas vamos ignorar RX no loop */

    for(int i=0; i<5000 && !tx_is_done(&tx) && !tx_is_fail(&tx); i++){
        /* roda só o TX para simular ausência total de ACK */
        tx_thread(&tx);
        g_tick++;
    }

    verifica("TX deveria falhar por timeout sem ACK", tx_is_fail(&tx));
    verifica("Deveria ter esgotado retries", tx_retry_count(&tx) > 0);
    return 0;
}

/* Runner */
static char* executa_todos_testes(void){
    executa_teste(teste_ok_sem_retransmissao);
    executa_teste(teste_corrupcao_uma_vez_reenvia);
    executa_teste(teste_sem_ack_timeout_falha);
    return 0;
}

/* ===========================================================
   main
   =========================================================== */
int main(void){
    char* msg = executa_todos_testes();
    puts("\n--- Resultado Final ---");
    if(msg){
        printf("FALHOU: %s\n", msg);
    }else{
        puts("TODOS OS TESTES PASSARAM");
    }
    printf("Testes executados: %d\n", total_testes);
    return msg != NULL;
}
