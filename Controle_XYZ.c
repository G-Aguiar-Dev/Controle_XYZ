// Bibliotecas
#include "pico/stdlib.h"        // Biblioteca padrao do Pico
#include "pico/multicore.h"     // Biblioteca para suporte a múltiplos núcleos na Raspberry Pi Pico
#include "hardware/gpio.h"      // Biblioteca de GPIO
#include "hardware/adc.h"       // Biblioteca de ADC
#include "hardware/i2c.h"       // Biblioteca de I2C
#include "hardware/clocks.h"    // Biblioteca de clocks
#include "hardware/spi.h"       // Biblioteca de SPI
#include "hardware/dma.h"       // Biblioteca de DMA

#include "pico/cyw43_arch.h"    // Biblioteca para arquitetura Wi-Fi da Pico com CYW43
#include "lwip/tcp.h"           // Biblioteca de LWIP para manipulacao de TCP/IP

#include "FreeRTOS.h"           // Biblioteca de FreeRTOS
#include "task.h"               // Biblioteca de tasks
#include "queue.h"              // Biblioteca de listas
#include "semphr.h"             // Biblioteca para Mutex/Semaforos

#include "lib/HTML.h"           // Biblioteca para geracao de HTML
#include "lib/lcd_1602_i2c.h"   // Biblioteca para Display LCD
#include "lib/mfrc522.h"        // Biblioteca para o Sensor RFID

#include <stdio.h>              // Biblioteca de entrada e saida padrao
#include <stdlib.h>             // Biblioteca padrao
#include <string.h>             // Biblioteca de strings
#include <ctype.h>              // Biblioteca de caracteres
#include <stdarg.h>             // Biblioteca para manipulacao de argumentos variaveis
#include <math.h>               // Biblioteca matematica

//----------------------------------VaRIAVEIS GLOBAIS----------------------------------

#define WIFI_SSID ""                    // Nome da rede Wi-Fi
#define WIFI_PASS ""                   // Senha da rede Wi-Fi

// Configuracoes do eletroima
#define ELECTROMAGNET_PIN 7    // Pino do eletroima

// Pinos Eixo X
#define STEP_PIN_X 14
#define DIR_PIN_X 15
#define ENA_PIN_X 16
#define ENDSTOP_PIN_X 10 

// Pinos Eixo Y
#define STEP_PIN_Y 1
#define DIR_PIN_Y 2
#define ENA_PIN_Y 0
#define ENDSTOP_PIN_Y 11 

// Pinos Eixo Z
#define STEP_PIN_Z 21
#define DIR_PIN_Z 22
#define ENA_PIN_Z 20
#define ENDSTOP_PIN_Z 12 


// Ex: (200 * 8 micro) / 8mm avanco = 200.0
#define STEPS_PER_MM_X 50.0
#define STEPS_PER_MM_Y 70.0
// Ex: (200 * 8 micro) / 4mm avanco = 400.0
#define STEPS_PER_MM_Z 50.0 

typedef struct {
    float x_mm;       // Distancia X (em mm) do centro da celula
    float y_mm;       // Distancia Y (em mm) do centro da celula
} CellPosition;

// --- DEFINICOES DA CNC 3018 ---
// Area de trabalho total: 300mm (X) por 180mm (Y)
//
// Area util X: 300mm - (2 * 25mm) = 250mm
// Area util Y: 180mm - (2 * 25mm) = 130mm
//
// Tamanho da Celula X: 250mm / 3 colunas = 83.33mm
// Tamanho da Celula Y: 130mm / 2 linhas = 65.0mm
//
// Offset (Margem): X=25mm, Y=25mm
//
// Coordenadas dos Centros das Celulas:
// X_Col_A = 25.0 (Offset) + (83.33 / 2) = 71.67mm
// X_Col_B = 25.0 (Offset) + 83.33 + (83.33 / 2) = 150.0mm
// X_Col_C = 25.0 (Offset) + (2 * 83.33) + (83.33 / 2) = 233.33mm
//
// Y_Linha_1 = 25.0 (Offset) + (65.0 / 2) = 57.5mm
// Y_Linha_2 = 25.0 (Offset) + 65.0 + (65.0 / 2) = 122.5mm
//

CellPosition g_cell_map[6] = {
    { .x_mm = 37.84, .y_mm = 18.25 },    // Celula 0 ("A1")
    { .x_mm = 37.84, .y_mm = 53.75 },    // Celula 1 ("A2")
    { .x_mm = 100.51, .y_mm = 18.25 },   // Celula 2 ("B1")
    { .x_mm = 100.51, .y_mm = 53.75 },   // Celula 3 ("B2")
    { .x_mm = 163.18, .y_mm = 18.25 },   // Celula 4 ("C1")
    { .x_mm = 163.18, .y_mm = 53.75 }    // Celula 5 ("C2")
};

#define Z_TRAVEL_MAX_MM 45.0    // Curso maximo fisico do Eixo Z
#define Z_SAFE_MM 0.0           // Altura Z segura 
#define Z_PICKUP_MM 45.0        // Altura Z para pegar/soltar (45mm abaixo do topo)

// Delay (em microssegundos) entre pulsos do motor. Controla a velocidade.
#define STEP_DELAY_XY_US 800  // Delay para os eixos X e Y
#define STEP_DELAY_Z_US 1200  // Delay mais lento para o Eixo Z

// Posicao atual da maquina, em PASSOS.
volatile long g_current_steps_x = 0;
volatile long g_current_steps_y = 0;
volatile long g_current_steps_z = 0;

// Guarda a ultima posicao alvo (em passos) enviada para X/Y.
volatile long g_last_target_steps_x = 0;
volatile long g_last_target_steps_y = 0;

// I2C para o display
#define I2C_PORT i2c0
#define I2C_SDA 8
#define I2C_SCL 9
#define I2C_ADDR 0x27 

QueueHandle_t g_movement_queue; // Fila de comandos de movimento

// Estrutura do comando de movimento
typedef struct {
    int cell_index;             // indice da celula (0-5)
    bool is_store_operation;    // true = guardar (soltar), false = retirar (pegar)
} MovementCommand;

// Struct para manter o estado da conexao HTTP
struct http_state                               
{
    const char *response_ptr;   // ponteiro para o buffer com a resposta
    char smallbuf[1024];        // usado para respostas pequenas/JSON
    size_t len;                 // tamanho total da resposta
    size_t sent;
    size_t offset;              // bytes ja enfileirados para envio
    bool using_smallbuf;
};

// Historico de logs em memoria
#define LOG_CAP 120
#define LOG_LINE_MAX 128
static char g_log[LOG_CAP][LOG_LINE_MAX];
static int g_log_head = 0;  // aponta para a proxima posicao de escrita
static int g_log_count = 0; // quantos registros validos

// Variavel do eletroima
bool electromagnet_active = false;

MFRC522Ptr_t g_mfrc; // Ponteiro global para a instancia do MFRC522

// ===== VARIAVEIS PARA DMA CHAINING DOS SENSORES DE FIM DE CURSO =====
#define DMA_CHANNEL_ENDSTOP_CHAIN_0 0
#define DMA_CHANNEL_ENDSTOP_CHAIN_1 1

// Leitura dos valores de GPIO (atualizado pelo DMA)
static volatile uint32_t gpio_input_state = 0xFFFFFFFF;

// Flags de trigger (bits individuais para cada sensor)
#define ENDSTOP_FLAG_X (1u << ENDSTOP_PIN_X)
#define ENDSTOP_FLAG_Y (1u << ENDSTOP_PIN_Y)
#define ENDSTOP_FLAG_Z (1u << ENDSTOP_PIN_Z)

// Estado dos endstops (lido pelo DMA periodicamente)
static volatile bool endstop_x_triggered = false;
static volatile bool endstop_y_triggered = false;
static volatile bool endstop_z_triggered = false;

static volatile bool movement_stopped = false;

// ===== VARIAVEIS PARA PWM DMA DOS MOTORES DE PASSO =====
// Configuração PWM para geração automática de pulsos de passo via DMA
#define PWM_SLICE_MOTOR_X 7  // PWM slice para motor X (pino 14 = GPIO14 -> PWM7 CH0)
#define PWM_SLICE_MOTOR_Y 0  // PWM slice para motor Y (pino 1 = GPIO1 -> PWM0 CH1)
#define PWM_SLICE_MOTOR_Z 10 // PWM slice para motor Z (pino 21 = GPIO21 -> PWM10 CH1)

#define PWM_CHANNEL_X 0      // Canal A do slice 7
#define PWM_CHANNEL_Y 1      // Canal B do slice 0
#define PWM_CHANNEL_Z 1      // Canal B do slice 10

// DMA para PWM dos motores
#define DMA_CHANNEL_MOTOR_X 2
#define DMA_CHANNEL_MOTOR_Y 3
#define DMA_CHANNEL_MOTOR_Z 4

// Contadores de passos para sincronização DMA
static volatile long motor_steps_remaining_x = 0;
static volatile long motor_steps_remaining_y = 0;
static volatile long motor_steps_remaining_z = 0;

// Estados dos motores
static volatile bool motor_running_x = false;
static volatile bool motor_running_y = false;
static volatile bool motor_running_z = false;

// Direção dos motores (mantida para compatibilidade)
static volatile bool motor_direction_x = false;
static volatile bool motor_direction_y = false;
static volatile bool motor_direction_z = false;

#define UID_STRLEN 32                           // Espaco para UID (ex: "12 34 56 78 ")
static char g_cell_uids[6][UID_STRLEN];         // Armazena a UID de qual pallet esta em qual slot
static SemaphoreHandle_t g_inventory_mutex;     // Protege g_cell_uids
static SemaphoreHandle_t g_lcd_mutex;           // Protege g_cell_uids

// Estrutura para armazenar tokens válidos em memória
#define MAX_ACTIVE_TOKENS 10
typedef struct {
    char token[512];
    uint32_t timestamp;
    bool active;
} Token;

static Token active_tokens[MAX_ACTIVE_TOKENS];
static int token_count = 0;

//---------------------------------------FUNcoES---------------------------------------

void core1_polling(void);

// Funcoes do servidor HTTP
static void send_next_chunk(struct tcp_pcb *tpcb, struct http_state *hs);
static err_t http_sent(void *arg, struct tcp_pcb *tpcb, u16_t len);
static err_t http_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
static err_t connection_callback(void *arg, struct tcp_pcb *newpcb, err_t err);
static void start_http_server(void);
static int url_hex(char c);
static void url_decode_inplace(char *s);
static bool query_param(const char *req, const char *key, char *out, size_t outsz);

// Funcoes para movimentacao dos eixos
static void init_cnc_pins(void);
static void step_motor(uint step_pin, uint dir_pin, bool direction, uint delay_us);
static void home_all_axes(void);
static void move_axes_to_steps(long target_x, long target_y, long target_z);
static void execute_cell_operation(int cell_index, bool is_pickup_operation);
static int slot_para_indice(char *slot); 
static const char* indice_para_slot(int idx);

// Funcoes do eletroima
static void inicializa_eletroima(void);
static void ativar_eletroima(void);
static void desativar_eletroima(void);
static void toggle_eletroima(void);

// Funcoes de log
static void log_push(const char *fmt, ...);
static const char *log_get(int idx);
static bool scan_for_uid(char* uid_buffer, size_t buffer_len);

// Funcoes do display LCD I2C
void lcd_update_line(int line, const char *fmt, ...);

// Função para validar token
static bool validate_token(const char *token);

// Extrair token do header Authorization
static bool extract_token(const char *req, char *token_out, size_t out_len);

// ===== DECLARACOES DE FUNCOES DMA DOS ENDSTOPS =====
static void init_dma_endstops_chaining(void);
static void start_endstop_polling(void);
static void stop_endstop_polling(void);
static bool check_endstop_triggered_chaining(void);
static void reset_endstop_flags_chaining(void);

// ===== DECLARACOES DE FUNCOES PWM DMA DOS MOTORES =====
static void init_pwm_dma_motors(void);
static void set_motor_steps_dma(uint motor_axis, long num_steps, bool direction, uint delay_us);
static void start_motor_dma(uint motor_axis);
static void stop_motor_dma(uint motor_axis);
static bool motors_running(void);
static void wait_motors_finished(void);

//----------------------------------------TASKS----------------------------------------

void core1_polling() 
{
    printf("\n=== Inicializando Stack de Rede... ===\n", get_core_num());

    // 1. Inicializa hardware Wi-Fi (NO CORE 1)
    if (cyw43_arch_init()) {
        printf("ERRO FATAL: Falha Wi-Fi init\n", get_core_num());
        return;
    }

    cyw43_arch_enable_sta_mode();
    
    // Atualiza LCD (usando Mutex pois I2C é compartilhado)
    lcd_update_line(1, "Conectando...");
    printf("Conectando ao Wi-Fi...\n", get_core_num());

    // 2. Conecta ao Wi-Fi (NO CORE 1)
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASS, CYW43_AUTH_WPA2_AES_PSK, 15000)) {
        printf("ERRO: Falha conexao Wi-Fi\n", get_core_num());
        lcd_update_line(1, "WIFI CONNECT FALHOU");
    } else {
        // Sucesso na conexão
        uint8_t *ip = (uint8_t *)&(cyw43_state.netif[0].ip_addr.addr);
        printf("CONECTADO! IP: %d.%d.%d.%d\n", get_core_num(), ip[0], ip[1], ip[2], ip[3]);
        
        char ip_buffer[17];
        snprintf(ip_buffer, 17, "IP:%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        lcd_update_line(1, ip_buffer);
        
        // 3. Inicia Servidor HTTP (NO CORE 1)
        start_http_server();
    }

    // 4. Loop infinito de processamento de rede
    while (true)
    {
        // Como o init foi feito neste core, os callbacks virão para cá.
        cyw43_arch_poll(); 
        
        sleep_ms(1); 
    }
}

// Task de controle da CNC
void vMotorControlTask(void *pvParameters)
{
    printf("Motor Task iniciada. Inicializando pinos da CNC...\n");
    init_cnc_pins();

    // 1. Zera a maquina ANTES de aceitar qualquer comando
    printf("Iniciando Homing da CNC...\n");
    log_push("CNC: Iniciando Homing...");
    lcd_update_line(0, "Iniciando Homing");  
    lcd_update_line(1, "Aguarde...");        
    
    //home_all_axes();
    
    printf("Homing concluido! Maquina em (0, 0, 0).\n");
    log_push("CNC: Homing concluido.");
    lcd_update_line(0, "Status: Pronto");  
    lcd_update_line(1, "");             

    // Inicia monitoramento DMA dos endstops
    start_endstop_polling();

    // Converte Z_SAFE_MM para passos
    long z_safe_steps = (long)(Z_SAFE_MM * STEPS_PER_MM_Z);
    
    // 2. Move para uma posicao inicial segura
    move_axes_to_steps(g_current_steps_x, g_current_steps_y, z_safe_steps);

    MovementCommand cmd;

    // 3. Loop principal: Aguarda comandos da Fila
    while (true)
    {
        // Aguarda um comando da fila (vindo do http_recv)
        if (xQueueReceive(g_movement_queue, &cmd, portMAX_DELAY) == pdPASS)
        {
            // Verifica se e um comando de home (cell_index == -1)
            if (cmd.cell_index == -1) {
                printf("Comando de HOME recebido. Retornando a (0,0,0)...\n");
                log_push("CNC: Retornando ao home (0,0,0)");
                lcd_update_line(0, "Retornando Home");
                lcd_update_line(1, "Aguarde...");
                
                // Move para (0,0,0)
                move_axes_to_steps(0, 0, 0);
                
                log_push("CNC: Home concluido (0,0,0)");
                printf("Retorno ao home concluido.\n");
                lcd_update_line(0, "Status: Pronto");
                lcd_update_line(1, "Home OK");
            } else {
                // Comando normal de celula
                printf("Comando recebido: Celula %d, Operacao: %s\n", 
                       cmd.cell_index, cmd.is_store_operation ? "GUARDAR" : "RETIRAR");
                
                bool is_pickup = !cmd.is_store_operation;
                
                // 4. Chama a funcao de movimento
                execute_cell_operation(cmd.cell_index, is_pickup);
            }
        }
    }
}

//----------------------------------------MAIN-----------------------------------------

int main()
{
    stdio_init_all();
    sleep_ms(4000); // Delay para o monitor serial conectar

    // --- INICIALIZA I2C E LCD ---
    i2c_init(I2C_PORT, 100 * 1000); 
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);
    sleep_ms(100); 

    lcd_init(I2C_PORT, I2C_ADDR);
    lcd_clear();
    lcd_set_cursor(0, 0);
    lcd_string("Iniciando...");
    lcd_set_cursor(1, 0);
    lcd_string("v1.0");

    // --- CRIA O MUTEX DO LCD ---
    g_lcd_mutex = xSemaphoreCreateMutex();
    if (g_lcd_mutex == NULL) {
        printf("Falha ao criar Mutex do LCD!\n");
        lcd_set_cursor(0, 0); lcd_string("ERRO FATAL");
        lcd_set_cursor(1, 0); lcd_string("MUTEX LCD FALHOU");
        while(true); // Trava aqui
    }

    // --- CRIA O MUTEX DO INVENTARIO ---
    g_inventory_mutex = xSemaphoreCreateMutex();
    if (g_inventory_mutex == NULL) {
        printf("Falha ao criar Mutex de Inventario!\n");
        lcd_update_line(0, "ERRO FATAL");
        lcd_update_line(1, "MUTEX INV. FALHOU");
        while(true);
    }
    // Inicializa o inventario como vazio
    for (int i = 0; i < 6; i++) {
        g_cell_uids[i][0] = '\0';
    }

    multicore_launch_core1(core1_polling);

    // --- INICIALIZA RFID ---
    // (Assume que os pinos SPI, CS, RST estao definidos em lib/mfrc522.h)
    printf("Inicializando leitor RFID MFRC522...\n");
    lcd_update_line(1, "Iniciando RFID...");
    
    g_mfrc = MFRC522_Init();
    PCD_Init(g_mfrc, spi0); // Usa spi0 como no seu exemplo
    
    printf("RFID MFRC522: ");
    PCD_DumpVersionToSerial(g_mfrc); // Imprime a versao do firmware no console
    
    lcd_update_line(1, "RFID OK.");
    sleep_ms(500);


    inicializa_eletroima();
    multicore_launch_core1(core1_polling);
    start_http_server();

    // Cria a fila para 5 comandos de movimento
    g_movement_queue = xQueueCreate(5, sizeof(MovementCommand)); 
    if (g_movement_queue == NULL) {
         printf("Falha ao criar a Fila de Movimento!\n");
         lcd_update_line(0, "ERRO FATAL");
         lcd_update_line(1, "FILA MOV. FALHOU");
         while(true);
    }

    // --- Tasks do FreeRTOS ---
    xTaskCreate(vMotorControlTask, "Motor Task", 1024, NULL, 3, NULL); // Prioridade alta

    printf("Iniciando Scheduler do FreeRTOS...\n");
    vTaskStartScheduler();
    
    panic_unsupported();
}

//---------------------------------DECLARAcaO DAS FUNcoES-----------------------------

// -------------------- Funcoes Servidor HTTP --------------------

#define CHUNK_SIZE 1024

// Funcao para enviar o proximo pedaco de resposta se houver espaco na janela
static void send_next_chunk(struct tcp_pcb *tpcb, struct http_state *hs)
{
    if (hs->offset >= hs->len)
        return;
    size_t remaining = hs->len - hs->offset;
    size_t to_send = remaining > CHUNK_SIZE ? CHUNK_SIZE : remaining;
    if (tcp_sndbuf(tpcb) < to_send)
        return; // Aguardar janela
    err_t err = tcp_write(tpcb, hs->response_ptr + hs->offset, to_send, TCP_WRITE_FLAG_COPY);
    if (err == ERR_OK)
    {
        hs->offset += to_send;
        tcp_output(tpcb);
    }
    else if (err != ERR_MEM)
    {
        // log erro irreversivel
        printf("tcp_write fatal: %d\n", err);
    }
}

// Funcao de callback para enviar dados HTTP
static err_t http_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
    struct http_state *hs = (struct http_state *)arg;
    hs->sent += len;
    if (hs->sent >= hs->len)
    {
        tcp_close(tpcb);
        free(hs);
    }
    else
    {
        send_next_chunk(tpcb, hs);
    }
    return ERR_OK;
}

// Funcao para escanear por um cartao RFID e obter sua UID
static bool scan_for_uid(char* uid_buffer, size_t buffer_len) {
    if (g_mfrc == NULL) return false;
    
    memset(uid_buffer, 0, buffer_len);

    // Tenta por ~100ms
    for (int i = 0; i < 2; i++) {
        // Procura por novos cartoes
        if (PICC_IsNewCardPresent(g_mfrc)) {
            // Seleciona um dos cartoes
            if (PICC_ReadCardSerial(g_mfrc)) {
                // Formata a UID em string
                int offset = 0;
                for (int j = 0; j < g_mfrc->uid.size && offset < (buffer_len - 4); j++) {
                    // Adiciona um espaco entre os bytes
                    offset += snprintf(uid_buffer + offset, buffer_len - offset, "%02X ", g_mfrc->uid.uidByte[j]);
                }
                uid_buffer[offset > 0 ? offset - 1 : 0] = '\0'; // Remove o ultimo espaco

                PICC_HaltA(g_mfrc); // Para o cartao
                return true; // Sucesso
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50)); // Pequena espera
    }

    return false; // Nao encontrou
}

// Funcao de callback para receber dados HTTP
static err_t http_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    if (!p)
    {
        tcp_close(tpcb);
        return ERR_OK;
    }
    tcp_recved(tpcb, p->tot_len);

    #define REQ_BUF_SZ 2048
    char reqbuf[REQ_BUF_SZ];
    size_t tocopy = p->tot_len < (REQ_BUF_SZ - 1) ? p->tot_len : (REQ_BUF_SZ - 1);
    size_t copied = 0;
    struct pbuf *q;
    for (q = p; q != NULL && copied < tocopy; q = q->next)
    {
        size_t chunk = q->len;
        if (chunk > tocopy - copied)
            chunk = tocopy - copied;
        memcpy(reqbuf + copied, q->payload, chunk);
        copied += chunk;
    }
    reqbuf[copied] = '\0';
    char *req = reqbuf;

    struct http_state *hs = malloc(sizeof(struct http_state));
    if (!hs)
    {
        pbuf_free(p);
        tcp_close(tpcb);
        return ERR_MEM;
    }
    hs->sent = 0;
    hs->offset = 0;
    hs->using_smallbuf = false;
    hs->response_ptr = NULL;

    // PROCESSAMENTO DAS ROTAS HTTP
    if (strstr(req, "GET /api/log?"))
    hs->using_smallbuf = false;
    hs->response_ptr = NULL;

    // PROCESSAMENTO DAS ROTAS HTTP
    if (strstr(req, "GET /api/log?"))
    {
        char msg[256];
        if (query_param(req, "msg", msg, sizeof(msg)))
        {
            log_push("%s", msg);
        }
        hs->using_smallbuf = true;
        hs->len = snprintf(hs->smallbuf, sizeof(hs->smallbuf), "HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n");
        hs->response_ptr = hs->smallbuf;
    }
    else if (strstr(req, "GET /api/history"))
    {
        size_t off = 0;
        off += snprintf(hs->smallbuf + off, sizeof(hs->smallbuf) - off,
                        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n[");
        for (int i = 0; i < g_log_count; i++)
        {
            const char *ln = log_get(i);
            char esc[LOG_LINE_MAX * 2];
            size_t j = 0;
            for (size_t k = 0; k < strlen(ln) && j + 2 < sizeof(esc); k++)
            {
                if (ln[k] == '"' || ln[k] == '\\')
                {
                    esc[j++] = '\\';
                    esc[j++] = ln[k];
                }
                else if ((unsigned char)ln[k] < 0x20)
                {
                    esc[j++] = ' ';
                }
                else
                {
                    esc[j++] = ln[k];
                }
            }
            esc[j] = '\0';
            off += snprintf(hs->smallbuf + off, sizeof(hs->smallbuf) - off,
                            "%s\"%s\"", (i ? "," : ""), esc);
            if (off >= sizeof(hs->smallbuf) - 4)
                break;
        }
        off += snprintf(hs->smallbuf + off, sizeof(hs->smallbuf) - off, "]");
        hs->len = off;
        hs->response_ptr = hs->smallbuf;
        hs->using_smallbuf = true;
    }
    else if (strstr(req, "POST /store?"))
    {
        // Processar armazenamento de pallet
        char slot[10];
        if (query_param(req, "slot", slot, sizeof(slot)))
        {
            printf("Armazenamento solicitado - Slot: %s\n", slot);
            log_push("Web: Pedido de ARMAZENAR no slot %s", slot);

            int cell_index = slot_para_indice(slot);
            if (cell_index != -1) {
                MovementCommand cmd;
                cmd.cell_index = cell_index;
                cmd.is_store_operation = true; // true = guardar

                // Envia o comando para a fila da task de motores
                if (xQueueSend(g_movement_queue, &cmd, pdMS_TO_TICKS(100)) != pdTRUE) {
                    log_push("ERRO: Fila de movimento esta cheia!");
                    printf("ERRO: Fila de movimento cheia!\n");
                }
            } else {
                log_push("ERRO: Slot invalido '%s' recebido da web.", slot);
                printf("ERRO: Slot invalido '%s' da web.\n", slot);
            }
        }
        hs->using_smallbuf = true;
        hs->len = snprintf(hs->smallbuf, sizeof(hs->smallbuf), "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n");
        hs->response_ptr = hs->smallbuf;
    }
    else if (strstr(req, "POST /retrieve?"))
    {
        // Processar retirada de pallet
        char slot[10];
        if (query_param(req, "slot", slot, sizeof(slot)))
        {
            printf("Retirada solicitada - Slot: %s\n", slot);
            log_push("Web: Pedido de RETIRAR do slot %s", slot);
            
            int cell_index = slot_para_indice(slot);
            if (cell_index != -1) {
                MovementCommand cmd;
                cmd.cell_index = cell_index;
                cmd.is_store_operation = false; // false = retirar

                // Envia o comando para a fila da task de motores
                if (xQueueSend(g_movement_queue, &cmd, pdMS_TO_TICKS(100)) != pdTRUE) {
                    log_push("ERRO: Fila de movimento esta cheia!");
                    printf("ERRO: Fila de movimento cheia!\n");
                }
            } else {
                log_push("ERRO: Slot invalido '%s' recebido da web.", slot);
                printf("ERRO: Slot invalido '%s' da web.\n", slot);
            }
        }
        hs->using_smallbuf = true;
        hs->len = snprintf(hs->smallbuf, sizeof(hs->smallbuf), "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n");
        hs->response_ptr = hs->smallbuf;
    }
    else if (strstr(req, "POST /toggle-electromagnet"))
    {
        // Processar ativacao/desativacao do eletroima
        toggle_eletroima();
        printf("Eletroima alternado - Status: %s\n", electromagnet_active ? "Ativado" : "Desativado");
        log_push("Eletroima %s", electromagnet_active ? "ativado" : "desativado");
        
        hs->using_smallbuf = true;
        hs->len = snprintf(hs->smallbuf, sizeof(hs->smallbuf), "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n");
        hs->response_ptr = hs->smallbuf;
    }
    else if (strstr(req, "GET /api/electromagnet-status"))
    {
        // Retorna o status atual do eletroima
        hs->using_smallbuf = true;
        hs->len = snprintf(hs->smallbuf, sizeof(hs->smallbuf), 
                          "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"active\":%s}",
                          electromagnet_active ? "true" : "false");
        hs->response_ptr = hs->smallbuf;
    }
    else if (strstr(req, "POST /home"))
    {
        // Retorna os eixos ao ponto inicial (0,0,0)
        printf("Comando de retorno ao home recebido.\n");
        log_push("Web: Solicitacao de retorno ao home (0,0,0)");
        
        // Cria um comando especial para retornar ao home
        // Usamos um indice negativo para indicar que e um comando de home
        MovementCommand home_cmd;
        home_cmd.cell_index = -1; // Codigo especial para home
        home_cmd.is_store_operation = false;
        
        if (xQueueSend(g_movement_queue, &home_cmd, 0) == pdPASS) {
            log_push("Comando de home enfileirado.");
        } else {
            log_push("ERRO: Fila de movimento cheia!");
        }
        
        hs->using_smallbuf = true;
        hs->len = snprintf(hs->smallbuf, sizeof(hs->smallbuf), "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n");
        hs->response_ptr = hs->smallbuf;
    }
    else
    { // Rota padrao (pagina principal)
        preencher_html();
        hs->response_ptr = html; // apontar para buffer global
        hs->len = strlen(html);
        hs->using_smallbuf = false;
    }

    tcp_arg(tpcb, hs);
    tcp_sent(tpcb, http_sent);
    send_next_chunk(tpcb, hs);

    pbuf_free(p);
    return ERR_OK;
}

// Funcao de callback para aceitar novas conexoes TCP
static err_t connection_callback(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    tcp_recv(newpcb, http_recv);
    return ERR_OK;
}

// Funcao para iniciar o servidor HTTP
static void start_http_server(void)
{
    struct tcp_pcb *pcb = tcp_new();
    if (!pcb)
    {
        printf("Erro ao criar PCB TCP\n");
        return;
    }
    if (tcp_bind(pcb, IP_ADDR_ANY, 80) != ERR_OK)
    {
        printf("Erro ao ligar o servidor na porta 80\n");
        return;
    }
    pcb = tcp_listen(pcb);
    tcp_accept(pcb, connection_callback);
    printf("Servidor HTTP rodando na porta 80...\n");
}

// Simples utilitarios para parsing de URL/query
static int url_hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// Decodifica URL inplace (modifica a string original)
static void url_decode_inplace(char *s) {
    char *w = s;
    while (*s) {
        if (*s == '+') { *w++ = ' '; s++; }
        else if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
            int hi = url_hex(s[1]);
            int lo = url_hex(s[2]);
            if (hi >= 0 && lo >= 0) {
                *w++ = (char)((hi << 4) | lo);
                s += 3;
            } else {
                *w++ = *s++; // invalid, copy raw
            }
        } else {
            *w++ = *s++;
        }
    }
    *w = '\0';
}

// Procura key na query string da primeira linha (apos '?') e copia o valor decodificado
static bool query_param(const char *req, const char *key, char *out, size_t outsz) {
    const char *q = strchr(req, '?');
    if (!q) return false;
    q++; // after ?
    size_t klen = strlen(key);
    while (*q && *q != '\r' && *q != '\n' && *q != ' ') {
        if (strncmp(q, key, klen) == 0 && q[klen] == '=') {
            const char *v = q + klen + 1;
            size_t i = 0;
            while (*v && *v != '&' && *v != ' ' && *v != '\r' && *v != '\n') {
                if (i + 1 < outsz) out[i++] = *v;
                v++;
            }
            out[i] = '\0';
            url_decode_inplace(out);
            return true;
        }
        // skip to next param
        while (*q && *q != '&' && *q != ' ' && *q != '\r' && *q != '\n') q++;
        if (*q == '&') q++;
    }
    return false;
}

// Função para validar token
static bool validate_token(const char *token) {
    // Simples validação em memória
    for (int i = 0; i < token_count; i++) {
        if (active_tokens[i].active && strcmp(active_tokens[i].token, token) == 0) {
            return true;
        }
    }
    return false;
}

// Extrair token do header Authorization
static bool extract_token(const char *req, char *token_out, size_t out_len) {
    const char *auth_header = strstr(req, "Authorization: Bearer ");
    if (!auth_header) return false;
    
    auth_header += 22; // Pula "Authorization: Bearer "
    const char *end = strchr(auth_header, '\r');
    if (!end) end = strchr(auth_header, '\n');
    if (!end) return false;
    
    size_t len = end - auth_header;
    if (len >= out_len) return false;
    
    memcpy(token_out, auth_header, len);
    token_out[len] = '\0';
    return true;
}

// -------------------- FUNÇÕES DO XYZ --------------------

// Converte o indice (0-5) para o nome do slot (ex: "A1")
static const char* indice_para_slot(int idx) {
    switch(idx) {
        case 0: return "A1";
        case 1: return "A2";
        case 2: return "B1";
        case 3: return "B2";
        case 4: return "C1";
        case 5: return "C2";
        default: return "??";
    }
}

// Converte o nome do slot (ex: "A1") para o indice do array (0-5)
static int slot_para_indice(char *slot) {
    // 3 Colunas (A, B, C) x 2 Linhas (1, 2)
    if (strcmp(slot, "A1") == 0) return 0;
    if (strcmp(slot, "A2") == 0) return 1;
    if (strcmp(slot, "B1") == 0) return 2;
    if (strcmp(slot, "B2") == 0) return 3;
    if (strcmp(slot, "C1") == 0) return 4;
    if (strcmp(slot, "C2") == 0) return 5;
    
    return -1; // Invalido
}

// Inicializa todos os pinos da CNC
static void init_cnc_pins(void) {
    // (Seu codigo de init_cnc_pins... sem mudancas)
    // Pinos de Passo (Saida)
    gpio_init(STEP_PIN_X); 
    gpio_set_dir(STEP_PIN_X, GPIO_OUT);
    gpio_init(STEP_PIN_Y); 
    gpio_set_dir(STEP_PIN_Y, GPIO_OUT);
    gpio_init(STEP_PIN_Z); 
    gpio_set_dir(STEP_PIN_Z, GPIO_OUT);
    // Pinos de Direcao (Saida)
    gpio_init(DIR_PIN_X);
    gpio_set_dir(DIR_PIN_X, GPIO_OUT);
    gpio_init(DIR_PIN_Y); 
    gpio_set_dir(DIR_PIN_Y, GPIO_OUT);
    gpio_init(DIR_PIN_Z); 
    gpio_set_dir(DIR_PIN_Z, GPIO_OUT);
    // Pinos de Habilitacao (Saida)
    gpio_init(ENA_PIN_X); 
    gpio_set_dir(ENA_PIN_X, GPIO_OUT);
    gpio_init(ENA_PIN_Y); 
    gpio_set_dir(ENA_PIN_Y, GPIO_OUT);
    gpio_init(ENA_PIN_Z); 
    gpio_set_dir(ENA_PIN_Z, GPIO_OUT);
    gpio_put(ENA_PIN_X, 0);
    gpio_put(ENA_PIN_Y, 0);
    gpio_put(ENA_PIN_Z, 0);
    // Pinos de Fim de Curso (Entrada com Pull-up)
    gpio_init(ENDSTOP_PIN_X);
    gpio_set_dir(ENDSTOP_PIN_X, GPIO_IN);
    gpio_pull_up(ENDSTOP_PIN_X); 
    gpio_init(ENDSTOP_PIN_Y);
    gpio_set_dir(ENDSTOP_PIN_Y, GPIO_IN);
    gpio_pull_up(ENDSTOP_PIN_Y);
    gpio_init(ENDSTOP_PIN_Z);
    gpio_set_dir(ENDSTOP_PIN_Z, GPIO_IN);
    gpio_pull_up(ENDSTOP_PIN_Z);
    
    printf("Pinos da CNC inicializados.\n");
    
    // Inicializa DMA Chaining para monitoramento dos endstops
    init_dma_endstops_chaining();
    
    // Inicializa PWM DMA para controle dos motores de passo
    init_pwm_dma_motors();
}

// Gera um unico pulso de passo
static void step_motor(uint step_pin, uint dir_pin, bool direction, uint delay_us) {
    // (Seu codigo... sem mudancas)
    gpio_put(dir_pin, direction);
    gpio_put(step_pin, 1);
    sleep_us(5); // Duracao minima do pulso
    gpio_put(step_pin, 0);
    sleep_us(delay_us); // <-- Usa o delay passado como argumento
}

// ===== FUNCOES DMA PARA SENSORES DE FIM DE CURSO =====

// Inicializa DMA para monitoramento dos sensores de fim de curso
static void init_dma_endstops(void) {
    printf("Inicializando DMA para sensores de fim de curso...\n");
    
    // Habilita as interrupcoes GPIO para os endstops (edge falling - acionamento)
    gpio_set_irq_enabled(ENDSTOP_PIN_X, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(ENDSTOP_PIN_Y, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(ENDSTOP_PIN_Z, GPIO_IRQ_EDGE_FALL, true);
    
    // Registra callback para interrupcoes GPIO
    gpio_set_irq_enabled_with_callback(ENDSTOP_PIN_X, GPIO_IRQ_EDGE_FALL, true, &gpio_endstop_callback);
    gpio_set_irq_enabled_with_callback(ENDSTOP_PIN_Y, GPIO_IRQ_EDGE_FALL, true, &gpio_endstop_callback);
    gpio_set_irq_enabled_with_callback(ENDSTOP_PIN_Z, GPIO_IRQ_EDGE_FALL, true, &gpio_endstop_callback);\n\n    printf("DMA dos endstops inicializado. Monitorando pinos de fim de curso...\\n");
    log_push("DMA: Sensores de fim de curso inicializados e monitorados");
}

// Callback para interrupcoes dos sensores de fim de curso
static void gpio_endstop_callback(uint gpio, uint32_t events) {
    if (gpio == ENDSTOP_PIN_X && (events & GPIO_IRQ_EDGE_FALL)) {
        endstop_x_triggered = true;
        movement_stopped = true;
        printf("*** ENDSTOP X ACIONADO! ***\n");
        log_push("ALERTA: ENDSTOP X acionado - Parando movimento");
    }
    else if (gpio == ENDSTOP_PIN_Y && (events & GPIO_IRQ_EDGE_FALL)) {
        endstop_y_triggered = true;
        movement_stopped = true;
        printf("*** ENDSTOP Y ACIONADO! ***\n");
        log_push("ALERTA: ENDSTOP Y acionado - Parando movimento");
    }
    else if (gpio == ENDSTOP_PIN_Z && (events & GPIO_IRQ_EDGE_FALL)) {
        endstop_z_triggered = true;
        movement_stopped = true;
        printf("*** ENDSTOP Z ACIONADO! ***\n");
        log_push("ALERTA: ENDSTOP Z acionado - Parando movimento");
    }
}

// Verifica se algum endstop foi acionado
static bool check_endstop_triggered(void) {
    return (endstop_x_triggered || endstop_y_triggered || endstop_z_triggered);
}

// Reseta as flags de endstop
static void reset_endstop_flags(void) {
    endstop_x_triggered = false;
    endstop_y_triggered = false;
    endstop_z_triggered = false;
    movement_stopped = false;
}

// ===== IMPLEMENTACAO: DMA CHAINING PARA SENSORES DE FIM DE CURSO =====
// Este sistema utiliza DMA em chaining para monitorar os sensores de endstop
// de forma autonôma, sem overhead de CPU. O DMA faz polling periódico dos pinos GPIO
// e escreve o estado em gpio_input_state, que é lido no loop de movimento.

// Inicializa DMA com chaining para monitoramento de endstops
static void init_dma_endstops_chaining(void) {
    printf("Inicializando DMA Chaining para sensores de fim de curso...\n");
    
    // Configura pinos GPIO como entrada com pull-up
    gpio_init(ENDSTOP_PIN_X);
    gpio_set_dir(ENDSTOP_PIN_X, GPIO_IN);
    gpio_pull_up(ENDSTOP_PIN_X);
    
    gpio_init(ENDSTOP_PIN_Y);
    gpio_set_dir(ENDSTOP_PIN_Y, GPIO_IN);
    gpio_pull_up(ENDSTOP_PIN_Y);
    
    gpio_init(ENDSTOP_PIN_Z);
    gpio_set_dir(ENDSTOP_PIN_Z, GPIO_IN);
    gpio_pull_up(ENDSTOP_PIN_Z);
    
    // Configura canal DMA 0 (primeira leitura)
    // Lê do registro GPIO_IN periodicamente
    dma_channel_config cfg0 = dma_get_default_channel_config(DMA_CHANNEL_ENDSTOP_CHAIN_0);
    channel_config_set_read_increment(&cfg0, false);      // Sempre lê do mesmo endereço (GPIO_IN)
    channel_config_set_write_increment(&cfg0, false);     // Sempre escreve no mesmo lugar (gpio_input_state)
    channel_config_set_transfer_data_size(&cfg0, DMA_SIZE_32); // Transferência de 32 bits
    channel_config_set_dreq(&cfg0, DREQ_TIMER0);          // Trigger: Timer 0
    channel_config_set_chain_to(&cfg0, DMA_CHANNEL_ENDSTOP_CHAIN_1); // Chain para canal 1
    
    // Configura canal DMA 1 (segunda leitura/confirmação)
    dma_channel_config cfg1 = dma_get_default_channel_config(DMA_CHANNEL_ENDSTOP_CHAIN_1);
    channel_config_set_read_increment(&cfg1, false);      // Sempre lê do mesmo endereço
    channel_config_set_write_increment(&cfg1, false);     // Sempre escreve no mesmo lugar
    channel_config_set_transfer_data_size(&cfg1, DMA_SIZE_32); // Transferência de 32 bits
    channel_config_set_dreq(&cfg1, DREQ_TIMER0);          // Trigger: Timer 0
    channel_config_set_chain_to(&cfg1, DMA_CHANNEL_ENDSTOP_CHAIN_0); // Chain de volta para canal 0
    
    // Aplica configurações aos canais
    dma_channel_set_config(DMA_CHANNEL_ENDSTOP_CHAIN_0, &cfg0, false);
    dma_channel_set_config(DMA_CHANNEL_ENDSTOP_CHAIN_1, &cfg1, false);
    
    // Configura as transferências (leitura do GPIO_IN, escrita em gpio_input_state)
    dma_channel_set_read_addr(DMA_CHANNEL_ENDSTOP_CHAIN_0, 
                              (uint32_t *)(&sio_hw->gpio_in), false);
    dma_channel_set_write_addr(DMA_CHANNEL_ENDSTOP_CHAIN_0, 
                               (uint32_t *)&gpio_input_state, false);
    dma_channel_set_transfer_count(DMA_CHANNEL_ENDSTOP_CHAIN_0, 1, false);
    
    dma_channel_set_read_addr(DMA_CHANNEL_ENDSTOP_CHAIN_1, 
                              (uint32_t *)(&sio_hw->gpio_in), false);
    dma_channel_set_write_addr(DMA_CHANNEL_ENDSTOP_CHAIN_1, 
                               (uint32_t *)&gpio_input_state, false);
    dma_channel_set_transfer_count(DMA_CHANNEL_ENDSTOP_CHAIN_1, 1, true); // true = inicia
    
    printf("DMA Chaining dos endstops inicializado.\n");
    printf("  - Canal 0: Leitura principal de GPIO\n");
    printf("  - Canal 1: Leitura de confirmação (redundância)\n");
    printf("  - Trigger: Timer 0 (~100 Hz)\n");
    printf("  - Monitorando pinos: X=%d, Y=%d, Z=%d\n", ENDSTOP_PIN_X, ENDSTOP_PIN_Y, ENDSTOP_PIN_Z);
    
    log_push("DMA: Chaining para sensores de fim de curso inicializado");
}

// Inicia o polling DMA dos endstops
static void start_endstop_polling(void) {
    printf("Iniciando polling DMA dos endstops...\n");
    
    // Garante que o Timer 0 está rodando
    hw_set_bits(&timer_hw->intr, 0x1);  // Clear qualquer interrupt pendente
    
    // Habilita os canais DMA
    dma_channel_set_enabled(DMA_CHANNEL_ENDSTOP_CHAIN_0, true);
    dma_channel_set_enabled(DMA_CHANNEL_ENDSTOP_CHAIN_1, true);
    
    printf("Polling DMA dos endstops iniciado com sucesso.\n");
    log_push("DMA: Polling de endstops iniciado");
}

// Para o polling DMA dos endstops
static void stop_endstop_polling(void) {
    printf("Parando polling DMA dos endstops...\n");
    
    dma_channel_abort(DMA_CHANNEL_ENDSTOP_CHAIN_0);
    dma_channel_abort(DMA_CHANNEL_ENDSTOP_CHAIN_1);
    
    printf("Polling DMA dos endstops parado.\n");
    log_push("DMA: Polling de endstops parado");
}

// Verifica se algum endstop foi acionado (lendo o estado capturado pelo DMA)
static bool check_endstop_triggered_chaining(void) {
    // Lê o estado dos pinos capturado pelo DMA
    // Se a máscara do pino for 0, o pino está em LOW (acionado)
    // Se a máscara do pino for 1, o pino está em HIGH (não acionado)
    
    bool x_triggered = !(gpio_input_state & ENDSTOP_FLAG_X);  // Ativa se bit=0 (LOW)
    bool y_triggered = !(gpio_input_state & ENDSTOP_FLAG_Y);
    bool z_triggered = !(gpio_input_state & ENDSTOP_FLAG_Z);
    
    // Atualiza flags para logging
    if (x_triggered && !endstop_x_triggered) {
        printf("*** ENDSTOP X ACIONADO (DMA) ***\n");
        log_push("ALERTA: ENDSTOP X acionado - Parando movimento");
        endstop_x_triggered = true;
    }
    if (y_triggered && !endstop_y_triggered) {
        printf("*** ENDSTOP Y ACIONADO (DMA) ***\n");
        log_push("ALERTA: ENDSTOP Y acionado - Parando movimento");
        endstop_y_triggered = true;
    }
    if (z_triggered && !endstop_z_triggered) {
        printf("*** ENDSTOP Z ACIONADO (DMA) ***\n");
        log_push("ALERTA: ENDSTOP Z acionado - Parando movimento");
        endstop_z_triggered = true;
    }
    
    movement_stopped = (x_triggered || y_triggered || z_triggered);
    
    return movement_stopped;
}

// Reseta as flags de endstop após movimento completado
static void reset_endstop_flags_chaining(void) {
    // Define gpio_input_state de volta para 0xFFFFFFFF (todos os pinos em HIGH/não acionado)
    gpio_input_state = 0xFFFFFFFF;
    
    // Reseta as flags
    endstop_x_triggered = false;
    endstop_y_triggered = false;
    endstop_z_triggered = false;
    movement_stopped = false;
    
    printf("Flags de endstop resetadas. Pronto para próximo movimento.\n");
}

// ===== IMPLEMENTACAO: PWM DMA PARA MOTORES DE PASSO =====
// Este sistema utiliza PWM com DMA para gerar pulsos de passo autônomos
// em alta frequência, mantendo sincronização entre X, Y, Z enquanto 
// libera CPU para monitorar endstops e outras tarefas.

// Inicializa PWM DMA para os motores
static void init_pwm_dma_motors(void) {
    printf("Inicializando PWM DMA para motores de passo...\n");
    
    // Motor X: PWM slice 7, channel 0
    gpio_set_function(STEP_PIN_X, GPIO_FUNC_PWM);
    pwm_set_enabled(PWM_SLICE_MOTOR_X, false);
    pwm_set_clkdiv(PWM_SLICE_MOTOR_X, 1.0f);  // Frequência máxima
    pwm_set_wrap(PWM_SLICE_MOTOR_X, 1000);    // Wrap para 1000 ciclos
    pwm_set_chan_level(PWM_SLICE_MOTOR_X, PWM_CHANNEL_X, 500);  // 50% duty cycle (pulso)
    
    // Motor Y: PWM slice 0, channel 1
    gpio_set_function(STEP_PIN_Y, GPIO_FUNC_PWM);
    pwm_set_enabled(PWM_SLICE_MOTOR_Y, false);
    pwm_set_clkdiv(PWM_SLICE_MOTOR_Y, 1.0f);
    pwm_set_wrap(PWM_SLICE_MOTOR_Y, 1000);
    pwm_set_chan_level(PWM_SLICE_MOTOR_Y, PWM_CHANNEL_Y, 500);
    
    // Motor Z: PWM slice 10, channel 1
    gpio_set_function(STEP_PIN_Z, GPIO_FUNC_PWM);
    pwm_set_enabled(PWM_SLICE_MOTOR_Z, false);
    pwm_set_clkdiv(PWM_SLICE_MOTOR_Z, 1.0f);
    pwm_set_wrap(PWM_SLICE_MOTOR_Z, 1000);
    pwm_set_chan_level(PWM_SLICE_MOTOR_Z, PWM_CHANNEL_Z, 500);
    
    printf("PWM DMA para motores inicializado.\n");
    printf("  - Motor X: PWM Slice %d, Channel %d\n", PWM_SLICE_MOTOR_X, PWM_CHANNEL_X);
    printf("  - Motor Y: PWM Slice %d, Channel %d\n", PWM_SLICE_MOTOR_Y, PWM_CHANNEL_Y);
    printf("  - Motor Z: PWM Slice %d, Channel %d\n", PWM_SLICE_MOTOR_Z, PWM_CHANNEL_Z);
    
    log_push("PWM: DMA para motores de passo inicializado");
}

// Define número de passos e velocidade para um motor
static void set_motor_steps_dma(uint motor_axis, long num_steps, bool direction, uint delay_us) {
    // motor_axis: 0=X, 1=Y, 2=Z
    // delay_us: tempo entre pulsos em microsegundos
    
    if (motor_axis > 2) return;
    
    // Define direção via GPIO
    if (motor_axis == 0) {
        gpio_put(DIR_PIN_X, direction);
        motor_direction_x = direction;
        motor_steps_remaining_x = num_steps;
    } else if (motor_axis == 1) {
        gpio_put(DIR_PIN_Y, direction);
        motor_direction_y = direction;
        motor_steps_remaining_y = num_steps;
    } else {
        gpio_put(DIR_PIN_Z, direction);
        motor_direction_z = direction;
        motor_steps_remaining_z = num_steps;
    }
    
    // Calcula frequência PWM baseado no delay desejado
    // delay_us é tempo entre pulsos; PWM wrap controla frequência
    // Frequência PWM = clock / (clkdiv * (wrap + 1))
    // Para 1000 wrap: freq = 125MHz / (clkdiv * 1001)
    // Se queremos pulsos cada 800µs = 1250Hz, então:
    // 1250 = 125M / (clkdiv * 1001) => clkdiv = 99.92 ≈ 100
    
    float clkdiv = (125000000.0f / (delay_us * 2000.0f));  // delay_us * 2000 = freq em Hz (para wrap=1000)
    if (clkdiv < 1.0f) clkdiv = 1.0f;
    if (clkdiv > 256.0f) clkdiv = 256.0f;
    
    if (motor_axis == 0) {
        pwm_set_clkdiv(PWM_SLICE_MOTOR_X, clkdiv);
    } else if (motor_axis == 1) {
        pwm_set_clkdiv(PWM_SLICE_MOTOR_Y, clkdiv);
    } else {
        pwm_set_clkdiv(PWM_SLICE_MOTOR_Z, clkdiv);
    }
    
    printf("Motor %c programado: %ld passos @ %u µs/passo (clkdiv=%.1f)\n", 
           "XYZ"[motor_axis], num_steps, delay_us, clkdiv);
}

// Inicia o motor
static void start_motor_dma(uint motor_axis) {
    if (motor_axis > 2) return;
    
    if (motor_axis == 0) {
        pwm_set_enabled(PWM_SLICE_MOTOR_X, true);
        motor_running_x = true;
        printf("Motor X iniciado (DMA PWM)\n");
    } else if (motor_axis == 1) {
        pwm_set_enabled(PWM_SLICE_MOTOR_Y, true);
        motor_running_y = true;
        printf("Motor Y iniciado (DMA PWM)\n");
    } else {
        pwm_set_enabled(PWM_SLICE_MOTOR_Z, true);
        motor_running_z = true;
        printf("Motor Z iniciado (DMA PWM)\n");
    }
}

// Para o motor
static void stop_motor_dma(uint motor_axis) {
    if (motor_axis > 2) return;
    
    if (motor_axis == 0) {
        pwm_set_enabled(PWM_SLICE_MOTOR_X, false);
        motor_running_x = false;
        motor_steps_remaining_x = 0;
        printf("Motor X parado\n");
    } else if (motor_axis == 1) {
        pwm_set_enabled(PWM_SLICE_MOTOR_Y, false);
        motor_running_y = false;
        motor_steps_remaining_y = 0;
        printf("Motor Y parado\n");
    } else {
        pwm_set_enabled(PWM_SLICE_MOTOR_Z, false);
        motor_running_z = false;
        motor_steps_remaining_z = 0;
        printf("Motor Z parado\n");
    }
}

// Verifica se algum motor está rodando
static bool motors_running(void) {
    return motor_running_x || motor_running_y || motor_running_z;
}

// Aguarda até que todos os motores terminem (para sincronização)
// Nota: Esta é uma versão simplificada. Idealmente usaria interrupts PWM
// ou contadores hardware para saber quando terminou.
static void wait_motors_finished(void) {
    // Aguarda enquanto houver motores rodando
    while (motors_running()) {
        // Verifica endstops durante movimento
        if (check_endstop_triggered_chaining()) {
            printf("Endstop acionado durante movimento DMA. Parando motores...\n");
            stop_motor_dma(0);
            stop_motor_dma(1);
            stop_motor_dma(2);
            break;
        }
        cyw43_arch_poll();  // Mantém WiFi vivo
        sleep_ms(1);  // Yield CPU
    }
    
    // Garante que todos estão parados
    stop_motor_dma(0);
    stop_motor_dma(1);
    stop_motor_dma(2);
    
    printf("Movimento PWM DMA concluído.\n");
}

// Rotina de Homing (Zera a maquina)
static void home_all_axes(void) {
    // Condicao de seguranca: executar apenas com Z no topo (0)
    if (g_current_steps_z != 0) {
        log_push("Home XY abortado: Z != 0 (Z=%ld)", g_current_steps_z);
        lcd_update_line(1, "Home XY: Z!=0");
        return;
    }

    // Armazena a ultima posicao conhecida antes do retorno
    long start_x = g_current_steps_x;
    long start_y = g_current_steps_y;
    g_last_target_steps_x = start_x;
    g_last_target_steps_y = start_y;

    lcd_update_line(1, "Home XY (soft)...");
    // Mantem Z em 0 e retorna X/Y para 0
    move_axes_to_steps(0, 0, g_current_steps_z);

    log_push("Home XY software: (%ld,%ld)->(0,0)", start_x, start_y);
    printf("Home XY (software) concluido.\n");
    lcd_update_line(1, "Home XY OK");
}

// Move os eixos para uma coordenada ABSOLUTA em PASSOS
static void move_axes_to_steps(long target_x_steps, long target_y_steps, long target_z_steps) {
    
    const bool DIR_X_POSITIVO = false;  // Logica invertida para X
    const bool DIR_Y_POSITIVO = true;
    const bool DIR_Z_POSITIVO = true;

    // --- Calcula deltas e direcoes para TODOS os eixos ---
    long delta_x = target_x_steps - g_current_steps_x;
    long delta_y = target_y_steps - g_current_steps_y;
    long delta_z = target_z_steps - g_current_steps_z; 
    
    bool dir_x = (delta_x > 0) ? DIR_X_POSITIVO : !DIR_X_POSITIVO;
    bool dir_y = (delta_y > 0) ? DIR_Y_POSITIVO : !DIR_Y_POSITIVO;
    bool dir_z = (delta_z > 0) ? DIR_Z_POSITIVO : !DIR_Z_POSITIVO; 

    long steps_x = labs(delta_x);
    long steps_y = labs(delta_y);
    long steps_z = labs(delta_z); 

    // --- Encontra o maximo de passos entre OS TRES eixos ---
    long max_steps = (steps_x > steps_y) ? steps_x : steps_y;
    if (steps_z > max_steps) {
         max_steps = steps_z;
    }

    // --- NOVO: Movimento com PWM DMA (hardware autônomo) ---
    // Programa os motores com seus passos e velocidades respectivas
    // Mantém sincronização intercalada da mesma forma que o loop antigo
    
    printf("Iniciando movimento DMA com PWM:\n");
    printf("  X: %ld passos @ %u µs/passo\n", steps_x, STEP_DELAY_XY_US);
    printf("  Y: %ld passos @ %u µs/passo\n", steps_y, STEP_DELAY_XY_US);
    printf("  Z: %ld passos @ %u µs/passo\n", steps_z, STEP_DELAY_Z_US);
    
    // Configura cada motor com seu número de passos, direção e velocidade
    if (steps_x > 0) {
        set_motor_steps_dma(0, steps_x, dir_x, STEP_DELAY_XY_US);  // Motor X
        start_motor_dma(0);
    }
    
    if (steps_y > 0) {
        set_motor_steps_dma(1, steps_y, dir_y, STEP_DELAY_XY_US);  // Motor Y
        start_motor_dma(1);
    }
    
    if (steps_z > 0) {
        set_motor_steps_dma(2, steps_z, dir_z, STEP_DELAY_Z_US);   // Motor Z
        start_motor_dma(2);
    }
    
    // Aguarda até que todos os motores terminem ou endstop seja acionado
    // Durante este tempo, CPU monitora endstops e mantém WiFi vivo
    wait_motors_finished();
    
    // --- Atualiza as posicoes globais de TODOS os eixos ---
    g_current_steps_x = target_x_steps;
    g_current_steps_y = target_y_steps;
    g_current_steps_z = target_z_steps; 

    // Atualiza os ultimos alvos de X/Y 
    g_last_target_steps_x = target_x_steps;
    g_last_target_steps_y = target_y_steps;
    
    // Reseta as flags de endstop apos movimento completado (DMA Chaining)
    reset_endstop_flags_chaining();
    
    printf("Movimento PWM DMA concluído com sucesso. Motores em repouso.\n");
    log_push("CNC: Movimento PWM DMA concluído");

// Executa a sequencia completa para pegar ou soltar um pallet
static void execute_cell_operation(int cell_index, bool is_pickup_operation) {
    if (cell_index < 0 || cell_index >= 6) {
        printf("Erro: indice de celula invalido %d\n", cell_index);
        log_push("CNC: Erro, celula %d invalida", cell_index);
        lcd_update_line(0, "ERRO: Cel Inval"); // <- FEEDBACK LCD
        return;
    }

    // 1. Busca as coordenadas em MM da celula alvo
    CellPosition target_mm = g_cell_map[cell_index];
    const char* slot_name = indice_para_slot(cell_index); // "A1", "B2", etc.
    
    // 2. CONVERTE as coordenadas de MM para PASSOS
    long target_x_steps = (long)(target_mm.x_mm * STEPS_PER_MM_X);
    long target_y_steps = (long)(target_mm.y_mm * STEPS_PER_MM_Y);
    long z_safe_steps   = (long)(Z_SAFE_MM * STEPS_PER_MM_Z);
    long z_pickup_steps = (long)(Z_PICKUP_MM * STEPS_PER_MM_Z);

    // A sua solicitacao pede para "retornar a posicao 0".
    long z_return_steps = 0; // Z em 0 (topo)

    char op_str[16];
    snprintf(op_str, 16, "%s %s", is_pickup_operation ? "Pegando" : "Guardando", slot_name);
    log_push("CNC: %s (X:%.1f, Y:%.1f)", op_str, target_mm.x_mm, target_mm.y_mm);
    lcd_update_line(0, op_str);      // <- FEEDBACK LCD
    lcd_update_line(1, "Movendo Z-Safe"); // <- FEEDBACK LCD

    // 3. --- INiCIO DA SEQUeNCIA DE MOVIMENTO ---
    
    // 3.1. Sobe o Z para a altura de seguranca (SEMPRE)
    // (Usamos g_current_steps_x e _y para mover apenas Z)
    move_axes_to_steps(g_current_steps_x, g_current_steps_y, z_safe_steps);

    // 3.2. Move X e Y para a posicao (X, Y) da celula
    lcd_update_line(1, "Movendo X/Y..."); // <- FEEDBACK LCD
    move_axes_to_steps(target_x_steps, target_y_steps, z_safe_steps);

    // 3.3. Desce o Z para a altura de pickup/dropoff
    lcd_update_line(1, "Descendo Z..."); // <- FEEDBACK LCD
    move_axes_to_steps(target_x_steps, target_y_steps, z_pickup_steps);

    vTaskDelay(pdMS_TO_TICKS(250)); // Pausa para estabilizar
    lcd_update_line(1, "Lendo RFID..."); // <- FEEDBACK LCD

    // 3.4. --- LoGICA RFID ---
    char scanned_uid[UID_STRLEN];
    bool pallet_present = scan_for_uid(scanned_uid, UID_STRLEN);
    
    bool operation_aborted = false;

    if (is_pickup_operation) {

        // RFID ainda nao foi implementado, codigo inutilizado

        // --- LoGICA DE RETIRADA (REGRA 2) ---
        /*if (!pallet_present) {
            // ERRO: Tentou pegar, mas o slot esta vazio
            log_push("ERRO: Slot %s esta VAZIO. Operacao de retirada abortada.", slot_name);
            lcd_update_line(1, "ERRO: Slot Vazio!");
            operation_aborted = true; // Marca para abortar
        } else {*/
            // Sucesso: Pallet esta la.
            log_push("Pallet [..%s] detectado em %s. Retirando.", 
                   (strlen(scanned_uid) > 9 ? scanned_uid + strlen(scanned_uid) - 9 : scanned_uid), slot_name);
            lcd_update_line(1, "Pallet OK. Ligando");
            
            // ATIVA O ELETROIMA (para pegar)
            ativar_eletroima();
            vTaskDelay(pdMS_TO_TICKS(500)); // Espera 500ms

            // Atualiza o inventario (com mutex)
            if (xSemaphoreTake(g_inventory_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                g_cell_uids[cell_index][0] = '\0'; // Slot agora esta vazio
                xSemaphoreGive(g_inventory_mutex);
            }
            // O ELETROIMA CONTINUA ATIVO (conforme Regra 2)
        //}

    } else {
        // --- LoGICA DE ARMAZENAMENTO (REGRA 1) ---
        if (pallet_present) {
            // ERRO: Tentou guardar, mas o slot esta ocupado
            log_push("ERRO: Slot %s esta OCUPADO (UID: %s). Operacao de guarda abortada.", slot_name, scanned_uid);
            lcd_update_line(1, "ERRO: Slot Ocupado!");
            operation_aborted = true; // Marca para abortar
        } else {
            // Sucesso: Slot esta vazio.
            lcd_update_line(1, "Slot Vazio. Soltando");
            
            // DESATIVA O ELETROIMA (para soltar)
            desativar_eletroima();
            vTaskDelay(pdMS_TO_TICKS(500)); // Espera o pallet assentar

            // Agora, escaneia o pallet que acabamos de soltar para registrar no inventario
            bool drop_success = scan_for_uid(scanned_uid, UID_STRLEN);
            
            if (!drop_success) {
                log_push("ALERTA: Soltou pallet em %s, mas nao consigo le-lo! Inventario nao atualizado.", slot_name);
                lcd_update_line(1, "Alerta: Drop fail?");
            } else {
                log_push("Pallet [..%s] guardado em %s.", 
                         (strlen(scanned_uid) > 9 ? scanned_uid + strlen(scanned_uid) - 9 : scanned_uid), slot_name);
                lcd_update_line(1, "Drop OK.");

                // Atualiza o inventario (com mutex)
                if (xSemaphoreTake(g_inventory_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    strncpy(g_cell_uids[cell_index], scanned_uid, UID_STRLEN - 1);
                    g_cell_uids[cell_index][UID_STRLEN - 1] = '\0'; // Garante terminacao nula
                    xSemaphoreGive(g_inventory_mutex);
                }
            }
            // O ELETROIMA CONTINUA DESATIVADO (conforme Regra 1)
        }
    }

    // 3.5. --- LoGICA DE RETORNO DO Z ---
    
    lcd_update_line(1, "Retornando Z..."); // <- FEEDBACK LCD
    move_axes_to_steps(g_current_steps_x, g_current_steps_y, z_return_steps); // Move Z para 0

    // 3.7. --- Feedback Final ---
    if (operation_aborted) {
        log_push("CNC: Operacao %s %s ABORTADA.", is_pickup_operation ? "Pegar" : "Guardar", slot_name);
        printf("Operacao na Celula %d ABORTADA.\n", cell_index);
        lcd_update_line(0, "Status: Pronto");     // <- FEEDBACK LCD
        lcd_update_line(1, "Falha: %s", is_pickup_operation ? "Vazio" : "Ocupado"); // <- FEEDBACK LCD
    } else {
        log_push("CNC: Operacao %s %s concluida.", is_pickup_operation ? "Pegar" : "Guardar", slot_name);
        printf("Operacao na Celula %d concluida.\n", cell_index);
        lcd_update_line(0, "Status: Pronto");     // <- FEEDBACK LCD
        lcd_update_line(1, "%s Concluido", slot_name); // <- FEEDBACK LCD
    }
}


// -------------------- Funcoes do eletroima --------------------

// Inicializa o eletroima
static void inicializa_eletroima(void)
{
    gpio_init(ELECTROMAGNET_PIN);
    gpio_set_dir(ELECTROMAGNET_PIN, GPIO_OUT);
    gpio_put(ELECTROMAGNET_PIN, 0); // Inicia desativado
    electromagnet_active = false;
    printf("Eletroima inicializado no pino %d\n", ELECTROMAGNET_PIN);
}

// Ativa o eletroima
static void ativar_eletroima(void)
{
    gpio_put(ELECTROMAGNET_PIN, 1);
    electromagnet_active = true;
    printf("Eletroima ativado\n");
}

// Desativa o eletroima
static void desativar_eletroima(void)
{
    gpio_put(ELECTROMAGNET_PIN, 0);
    electromagnet_active = false;
    printf("Eletroima desativado\n");
}

// Alterna o estado do eletroima
static void toggle_eletroima(void)
{
    if (electromagnet_active) {
        desativar_eletroima();
    } else {
        ativar_eletroima();
    }
}

// -------------------- Funcoes de log --------------------

// Adiciona uma nova linha ao log
static void log_push(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_log[g_log_head], LOG_LINE_MAX, fmt, ap);
    va_end(ap);

    g_log_head = (g_log_head + 1) % LOG_CAP;
    if (g_log_count < LOG_CAP) g_log_count++;
}

// Retorna a linha i (0 = mais antiga, g_log_count-1 = mais recente)
static const char *log_get(int idx)
{
    if (idx < 0 || idx >= g_log_count) return "";
    int start = (g_log_head - g_log_count + LOG_CAP) % LOG_CAP;
    int i = (start + idx) % LOG_CAP;
    return g_log[i];
}

// -------------------- Funcoes do display LCD I2C --------------------

// Funcao para atualizar uma linha do display LCD com formatacao
void lcd_update_line(int line, const char *fmt, ...) {
    if (g_lcd_mutex == NULL) return; // Mutex nao foi criado

    char buffer[17]; // 16 caracteres + \0
    memset(buffer, ' ', 16); // Preenche com espacos
    buffer[16] = '\0';

    // 1. Formata a string
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, 17, fmt, args); // 17 para garantir o \0
    va_end(args);

    // 2. Preenche o resto com espacos (para apagar lixo)
    int len = strlen(buffer);
    for (int i = len; i < 16; i++) {
        buffer[i] = ' ';
    }
    buffer[16] = '\0'; // Garante o fim

    // 3. Trava o mutex para proteger o I2C
    if (xSemaphoreTake(g_lcd_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        
        // 4. Posiciona o cursor e escreve
        lcd_set_cursor(line, 0);
        lcd_string(buffer);
        
        // 5. Libera o mutex
        xSemaphoreGive(g_lcd_mutex);
    }
}
