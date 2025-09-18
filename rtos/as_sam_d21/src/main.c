/**
 * \file
 *
 * \brief Exemplos diversos de tarefas e funcionalidades de um sistema operacional multitarefas.
 *
 */

/**
 * \mainpage User Application template doxygen documentation
 *
 * \par Empty user application template
 *
 * Este arquivo contem exemplos diversos de tarefas e 
 * funcionalidades de um sistema operacional multitarefas.
 *
 *
 * \par Conteudo
 *
 * -# Inclui funcoes do sistema multitarefas (atraves de multitarefas.h)
 * -# Inicizalizao do processador e do sistema multitarefas
 * -# Criacao de tarefas de demonstracao
 *
 */

/*
 * Inclusao de arquivos de cabecalhos
 */
#include <asf.h>
#include "stdint.h"
#include "rtos.h"

/* ---------------------------------------------------
 * Definição de modos de escalonamento
 * Ative apenas UM dos modos por vez
 * --------------------------------------------------- */
#define MODO_COOPERATIVO   1
#define MODO_PREEMPTIVO    0

/*
 * Prototipos das tarefas
 */
void tarefa_1(void);
void tarefa_2(void);
void tarefa_3(void);
void tarefa_4(void);
void tarefa_5(void);
void tarefa_6(void);
void tarefa_7(void);
void tarefa_8(void);
void tarefa_extra(void);   /* <<< Prototipo da tarefa extra */

/*
 * Configuracao dos tamanhos das pilhas
 */
#define TAM_PILHA_1			(TAM_MINIMO_PILHA + 24)
#define TAM_PILHA_2			(TAM_MINIMO_PILHA + 24)
#define TAM_PILHA_3			(TAM_MINIMO_PILHA + 24)
#define TAM_PILHA_4			(TAM_MINIMO_PILHA + 24)
#define TAM_PILHA_5			(TAM_MINIMO_PILHA + 24)
#define TAM_PILHA_6			(TAM_MINIMO_PILHA + 24)
#define TAM_PILHA_7			(TAM_MINIMO_PILHA + 24)
#define TAM_PILHA_8			(TAM_MINIMO_PILHA + 24)
#define TAM_PILHA_EXTRA		(TAM_MINIMO_PILHA + 24)   /* <<< Tarefa extra */
#define TAM_PILHA_OCIOSA	(TAM_MINIMO_PILHA + 24)

/*
 * Declaracao das pilhas das tarefas
 */
uint32_t PILHA_TAREFA_1[TAM_PILHA_1];
uint32_t PILHA_TAREFA_2[TAM_PILHA_2];
uint32_t PILHA_TAREFA_3[TAM_PILHA_3];
uint32_t PILHA_TAREFA_4[TAM_PILHA_4];
uint32_t PILHA_TAREFA_5[TAM_PILHA_5];
uint32_t PILHA_TAREFA_6[TAM_PILHA_6];
uint32_t PILHA_TAREFA_7[TAM_PILHA_7];
uint32_t PILHA_TAREFA_8[TAM_PILHA_8];
uint32_t PILHA_TAREFA_EXTRA[TAM_PILHA_EXTRA];   /* <<< Pilha da tarefa extra */
uint32_t PILHA_TAREFA_OCIOSA[TAM_PILHA_OCIOSA];

/*
 * Funcao principal de entrada do sistema
 */
int main(void)
{
#if 0
	system_init();
#endif
	
	/* Criacao das tarefas */
	CriaTarefa(tarefa_1, "Tarefa 1", PILHA_TAREFA_1, TAM_PILHA_1, 2);
	CriaTarefa(tarefa_2, "Tarefa 2", PILHA_TAREFA_2, TAM_PILHA_2, 1);
	CriaTarefa(tarefa_extra, "Tarefa Extra", PILHA_TAREFA_EXTRA, TAM_PILHA_EXTRA, 3);

	/* Cria tarefa ociosa do sistema */
	CriaTarefa(tarefa_ociosa,"Tarefa ociosa", PILHA_TAREFA_OCIOSA, TAM_PILHA_OCIOSA, 0);
	
	/* Configura marca de tempo (necessário para preemptivo) */
#if MODO_PREEMPTIVO
	ConfiguraMarcaTempo();   
#endif	
	
	/* Inicia sistema multitarefas */
	IniciaMultitarefas();
	
	while (1) {}
}

/* --------------------------------------------------- */
/*  TAREFAS EXISTENTES (1 ATÉ 8) — SEM ALTERAÇÕES      */
/* --------------------------------------------------- */

/* Tarefas de exemplo que usam funcoes para suspender/continuar as tarefas */
void tarefa_1(void)
{
	volatile uint16_t a = 0;
	for(;;)
	{
		a++;
		port_pin_set_output_level(LED_0_PIN, LED_0_ACTIVE); /* Liga LED. */
		TarefaContinua(2);
	}
}

void tarefa_2(void)
{
	volatile uint16_t b = 0;
	for(;;)
	{
		b++;
		TarefaSuspende(2);	
		port_pin_set_output_level(LED_0_PIN, !LED_0_ACTIVE); 	/* Desliga LED. */
	}
}

/* Tarefas de exemplo que usam funcoes de atraso */
void tarefa_3(void)
{
	volatile uint16_t a = 0;
	for(;;)
	{
		a++;	
		port_pin_set_output_level(LED_0_PIN, LED_0_ACTIVE);
		TarefaEspera(1000); 	/* espera 1000 ms */
		
		port_pin_set_output_level(LED_0_PIN, !LED_0_ACTIVE);
		TarefaEspera(1000); 	
	}
}

void tarefa_4(void)
{
	volatile uint16_t b = 0;
	for(;;)
	{
		b++;
		TarefaEspera(5);	
	}
}

/* Tarefas de exemplo que usam funcoes de semaforo */
semaforo_t SemaforoTeste = {0,0}; 

void tarefa_5(void)
{
	uint32_t a = 0;			
	for(;;)
	{
		a++;				
		TarefaEspera(3); 	
		SemaforoLibera(&SemaforoTeste); 
	}
}

void tarefa_6(void)
{
	uint32_t b = 0;	    
	for(;;)
	{
		b++; 			
		SemaforoAguarda(&SemaforoTeste); 
	}
}

/* solucao com buffer compartihado */
#define TAM_BUFFER 10
uint8_t buffer[TAM_BUFFER]; 

semaforo_t SemaforoCheio = {0,0}; 
semaforo_t SemaforoVazio = {TAM_BUFFER,0}; 

void tarefa_7(void)
{
	uint8_t a = 1;			
	uint8_t i = 0;
	
	for(;;)
	{
		SemaforoAguarda(&SemaforoVazio);
		buffer[i] = a++;
		i = (i+1)%TAM_BUFFER;
		SemaforoLibera(&SemaforoCheio); 
		TarefaEspera(10); 			
	}
}

void tarefa_8(void)
{
	static uint8_t f = 0;
	volatile uint8_t valor;
		
	for(;;)
	{
		volatile uint8_t contador;
		
		do{
			REG_ATOMICA_INICIO();			
				contador = SemaforoCheio.contador;			
			REG_ATOMICA_FIM();
			
			if (contador == 0)
			{
				TarefaEspera(100);
			}
				
		} while (!contador);
		
		SemaforoAguarda(&SemaforoCheio);
		valor = buffer[f];
		f = (f+1) % TAM_BUFFER;		
		SemaforoLibera(&SemaforoVazio);
	}
}

/* --------------------------------------------------- */
/*  >>> NOVA TAREFA EXTRA (executa a cada 100 ms)      */
/* --------------------------------------------------- */
void tarefa_extra(void)
{
	for(;;)
	{
		port_pin_toggle_output_level(LED_0_PIN); /* Pisca LED */
		TarefaEspera(100);  /* Espera 100 ms */
	}
}

/* --------------------------------------------------
 * - No MODO COOPERATIVO, a troca de tarefas só ocorre
 *   quando uma tarefa chama TarefaEspera(), TarefaSuspende()
 *   ou libera recursos voluntariamente.
 *
 * - No MODO PREEMPTIVO, o escalonador força a troca
 *   de tarefas a cada tick (ConfiguraMarcaTempo()),
 *   mesmo que a tarefa não tenha chamado funções de yield.
 *
 * - Ao observar a tarefa_extra (piscar LED a cada 100 ms):
 *   -> Em cooperativo, ela só roda se as outras tarefas
 *      liberarem a CPU voluntariamente.
 *   -> Em preemptivo, ela roda com precisão periódica,
 *      mesmo que outras tarefas estejam ocupadas.
 * --------------------------------------------------- */
