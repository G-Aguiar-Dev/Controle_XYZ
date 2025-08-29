// Bibliotecas
#include "pico/stdlib.h"        // Biblioteca padrão do Pico
#include "hardware/gpio.h"      // Biblioteca de GPIO
#include "hardware/adc.h"       // Biblioteca de ADC
#include "hardware/i2c.h"       // Biblioteca de I2C
#include "hardware/pwm.h"       // Biblioteca de PWM

#include "lwip/tcp.h"           // Biblioteca de TCP
#include "lwip/err.h"           // Biblioteca de erros lwIP
#include "lwip/pbuf.h"          // Biblioteca de buffers lwIP
#include "lwip/ip_addr.h"       // Biblioteca de endereços IP lwIP
#include "pico/cyw43_arch.h"    // Biblioteca do módulo Wireless CYW43439

#include "lib/ssd1306.h"        // Biblioteca de display OLED
#include "lib/font.h"           // Biblioteca de fontes
#include "lib/HTML.h"           // Biblioteca de HTML

#include "FreeRTOS.h"           // Biblioteca de FreeRTOS
#include "task.h"               // Biblioteca de tasks

#include <stdio.h>              // Biblioteca de entrada e saída padrão
#include <string.h>             // Biblioteca de manipulação de strings

//-----------------------------------------HTML----------------------------------------

//----------------------------------VÁRIAVEIS GLOBAIS----------------------------------

// Variáveis para o JSON do sistema de armazém
static char timestamp[16] = "00:00:00";
static char system_status[16] = "Online";
static char pallet_on_entry[16] = "ABC-123";
static char pallet_on_exit[16] = "";
static bool electromagnet_active = false;
static int current_x = 0, current_y = 0, current_z = 0;
static char rack_status[20][8] = {"A1", "A2", "A3", "A4", "A5", "B1", "B2", "B3", "B4", "B5", 
                                  "C1", "C2", "C3", "C4", "C5", "D1", "D2", "D3", "D4", "D5"};
static bool rack_occupied[20] = {true, false, false, true, false, false, true, false, false, false,
                                false, false, true, false, false, true, false, false, true, false};

// Struct para manter o estado da conexão HTTP
struct http_state
{
    char response[8192];
    size_t len;
    size_t sent;
    size_t offset; // bytes já enfileirados para envio
};

//---------------------------------------FUNÇÕES---------------------------------------

static void send_next_chunk(struct tcp_pcb *tpcb, struct http_state *hs);

static err_t http_sent(void *arg, struct tcp_pcb *tpcb, u16_t len);

static err_t http_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);

static err_t connection_callback(void *arg, struct tcp_pcb *newpcb, err_t err);

static void start_http_server(void);

//----------------------------------------TASKS----------------------------------------

//----------------------------------------MAIN-----------------------------------------

int main()
{
    stdio_init_all();

    // Inicializar Wi-Fi
    if (cyw43_arch_init()) {
        printf("Falha ao inicializar Wi-Fi\n");
        return -1;
    }

    // Conectar ao Wi-Fi (substitua pelos seus dados)
    cyw43_arch_enable_sta_mode();
    
    // Iniciar servidor HTTP
    start_http_server();

    vTaskStartScheduler();
    panic_unsupported();
}

//---------------------------------DECLARAÇÃO DAS FUNÇÕES-----------------------------

#define CHUNK_SIZE 1024

// Função para enviar o próximo pedaço de resposta se houver espaço na janela
static void send_next_chunk(struct tcp_pcb *tpcb, struct http_state *hs)
{
    if (hs->offset >= hs->len) return;
    size_t remaining = hs->len - hs->offset;
    size_t to_send = remaining > CHUNK_SIZE ? CHUNK_SIZE : remaining;
    if (tcp_sndbuf(tpcb) < to_send) return; // Aguardar janela
    err_t err = tcp_write(tpcb, hs->response + hs->offset, to_send, TCP_WRITE_FLAG_COPY);
    if (err == ERR_OK)
    {
        hs->offset += to_send;
        tcp_output(tpcb);
    }
    else if (err != ERR_MEM)
    {
        // log erro irreversível
        printf("tcp_write fatal: %d\n", err);
    }
}

// Função de callback para enviar dados HTTP
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

// Função de callback para receber dados HTTP
static err_t http_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    if (!p)
    {
        tcp_close(tpcb);
        return ERR_OK;
    }
    tcp_recved(tpcb, p->tot_len);

    char *req = (char *)p->payload;
    struct http_state *hs = malloc(sizeof(struct http_state));
    if (!hs)
    {
        pbuf_free(p);
        tcp_close(tpcb);
        return ERR_MEM;
    }
    hs->sent = 0;
    hs->offset = 0;

    // Endpoint JSON para /web
    if (strstr(req, "GET /web"))
    {
        char json[1024];
        char rack_json[512] = "[";
        
        // Construir array JSON dos racks
        for(int i = 0; i < 20; i++) {
            char rack_item[32];
            snprintf(rack_item, sizeof(rack_item), 
                "{\"position\":\"%s\",\"occupied\":%s}%s", 
                rack_status[i], 
                rack_occupied[i] ? "true" : "false",
                i < 19 ? "," : "");
            strcat(rack_json, rack_item);
        }
        strcat(rack_json, "]");
        
        int jsonlen = snprintf(json, sizeof(json),
            "{"
            "\"timestamp\":\"%s\","
            "\"system_status\":\"%s\","
            "\"pallet_on_entry\":\"%s\","
            "\"pallet_on_exit\":\"%s\","
            "\"electromagnet_active\":%s,"
            "\"current_position\":{\"x\":%d,\"y\":%d,\"z\":%d},"
            "\"rack_layout\":%s"
            "}", 
            timestamp,
            system_status,
            pallet_on_entry,
            pallet_on_exit,
            electromagnet_active ? "true" : "false",
            current_x, current_y, current_z,
            rack_json
        );
        
        hs->len = snprintf(hs->response, sizeof(hs->response),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n"
            "%s",
            jsonlen, json);
        tcp_arg(tpcb, hs);
        tcp_sent(tpcb, http_sent);
        send_next_chunk(tpcb, hs);
        pbuf_free(p);
        return ERR_OK;
    }

    else
    {
        // Primeiro, preencher o HTML
        preencher_html();
        
        hs->len = strlen(html);
        if (hs->len >= sizeof(hs->response)) {
            hs->len = sizeof(hs->response) - 1;
        }
        memcpy(hs->response, html, hs->len);
    }

    tcp_arg(tpcb, hs);
    tcp_sent(tpcb, http_sent);

    // inicia envio por fatias
    send_next_chunk(tpcb, hs);

    pbuf_free(p);
    return ERR_OK;
}

// Função de callback para aceitar novas conexões TCP
static err_t connection_callback(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    tcp_recv(newpcb, http_recv);
    return ERR_OK;
}

// Função para iniciar o servidor HTTP
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