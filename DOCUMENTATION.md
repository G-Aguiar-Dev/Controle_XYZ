# 📚 Documentação Detalhada - Controle XYZ

Documentação completa das funções, variáveis globais e arquitetura do sistema automatizado de armazém.

## 📋 Índice

1. [Arquitetura do Sistema](#arquitetura-do-sistema)
2. [Configuração de Hardware](#configuração-de-hardware)
3. [Constantes de Funcionamento](#constantes-de-funcionamento)
4. [API de Funções](#api-de-funções)
5. [Tasks do FreeRTOS](#tasks-do-freertos)
6. [Variáveis Globais](#variáveis-globais)
7. [Backend Flask](#backend-flask)
8. [Interface Web](#interface-web)
9. [Exemplos de Uso](#exemplos-de-uso)
10. [Troubleshooting](#troubleshooting)

---

## 🏗️ Arquitetura do Sistema

### Hardware
- **Microcontrolador**: Raspberry Pi Pico W (RP2040 com Wi-Fi)
- **Motor de Movimento**: CNC 3018 com 3 eixos (X, Y, Z)
- **Sensores de Fim de Curso**: Endstops para cada eixo
- **Leitor RFID**: MFRC522 via SPI
- **Manipulador**: Eletroímã para pegar/soltar pallets
- **Display**: LCD 16x2 com I2C
- **Conectividade**: Wi-Fi integrada, Servidor HTTP

### Software
- **SO**: FreeRTOS em tempo real
- **Linguagem Principal**: C (Pico SDK)
- **Servidor Backend**: Python com Flask (dbServer.py)
- **Interface Web**: HTML5 + JavaScript

### Estrutura do Armazém
```
Área de Trabalho CNC 3018:
- Dimensão Total: 300mm (X) × 180mm (Y)
- Zona Útil: 250mm (X) × 130mm (Y)
- Distribuição: 3 colunas (A, B, C) × 2 linhas (1, 2)
- Margem: 25mm em cada lado

Posições das Células:
┌─────────────────────────────────┐
│  A1       B1       C1           │
│  (37.84,  (100.51, (163.18,     │
│   18.25)   18.25)   18.25)       │
│                                 │
│  A2       B2       C2           │
│  (37.84,  (100.51, (163.18,     │
│   53.75)   53.75)   53.75)       │
└─────────────────────────────────┘
```

---

## 🔧 Configuração de Hardware

### Configuração de Rede
- **SSID**: `Armazem XYZ`
- **Senha**: `tic37#grupo4`
- **Porta HTTP (Pico)**: 80
- **Porta Backend Flask**: 5000

### Configuração de Pinos

#### Eixo X (Motor Passo-Passo)
| Função | Pino |
|--------|------|
| STEP | 14 |
| DIR | 15 |
| ENA | 16 |
| Endstop MIN | 10 |
| Endstop MAX | 13 |

#### Eixo Y (Motor Passo-Passo)
| Função | Pino |
|--------|------|
| STEP | 1 |
| DIR | 2 |
| ENA | 0 |
| Endstop MIN | 11 |
| Endstop MAX | 17 |

#### Eixo Z (Motor Passo-Passo)
| Função | Pino |
|--------|------|
| STEP | 21 |
| DIR | 22 |
| ENA | 20 |
| Endstop MIN | 12 |
| Endstop MAX | 18 |

#### Periféricos
| Periférico | Tipo | Pino/Barramento |
|-----------|------|-----------------|
| Eletroímã | GPIO | 7 |
| RFID MFRC522 | SPI | (definido em mfrc522.h) |
| LCD 16x2 | I2C | SDA: 8, SCL: 9 |

---

## ⚙️ Constantes de Funcionamento

```c
// Velocidade de Movimento
STEP_DELAY_XY_US = 800  // Microssegundos entre pulsos (X e Y)
STEP_DELAY_Z_US = 1200  // Microssegundos entre pulsos (Z)

// Resolução de Movimento
STEPS_PER_MM_X = 50.0   // 50 passos por mm
STEPS_PER_MM_Y = 70.0   // 70 passos por mm
STEPS_PER_MM_Z = 50.0   // 50 passos por mm

// Altitudes do Eixo Z
Z_SAFE_MM = 0.0         // Altura segura (sem colisão)
Z_PICKUP_MM = 45.0      // Altura de pegar/soltar pallet
Z_TRAVEL_MAX_MM = 45.0  // Curso máximo

// I2C Configuration
I2C_PORT = i2c0
I2C_SDA = 8
I2C_SCL = 9
I2C_ADDR = 0x27         // Endereço do LCD

// Log circular
LOG_CAP = 120           // Máximo de linhas
LOG_LINE_MAX = 128      // Máximo de caracteres por linha

// RFID
UID_STRLEN = 32         // Tamanho da string UID
```

---

## 🚀 API de Funções

### Funções de Inicialização

#### `init_cnc_pins()`
**Propósito**: Inicializa todos os pinos GPIO dos motores e sensores  
**Retorno**: `void`  
**Detalhes**:
- Configura pinos de STEP, DIR e ENA como saída
- Configura pinos de endstops como entrada
- Inicializa pino do eletroímã

---

#### `start_http_server()`
**Propósito**: Inicia o servidor HTTP para comunicação com interface web  
**Retorno**: `void`  
**Detalhes**:
- Vincula a callback de conexão TCP
- Aguarda requisições HTTP na porta 80
- Processa rotas de API (GET, POST)

---

### Funções de Movimento

#### `step_motor(uint step_pin, uint dir_pin, bool direction, uint delay_us)`
**Propósito**: Gera um pulso de passo único para um motor  
**Parâmetros**:
- `step_pin`: GPIO do sinal de STEP
- `dir_pin`: GPIO do sinal de DIREÇÃO
- `direction`: true = frente, false = trás
- `delay_us`: Delay em microssegundos entre pulsos

**Detalhes**:
- Define direção no pino DIR
- Envia pulso HIGH-LOW no pino STEP
- Aguarda o tempo especificado

---

#### `home_all_axes()`
**Propósito**: Posiciona a máquina no ponto zero (0,0,0)  
**Retorno**: `void`  
**Detalhes**:
- Move cada eixo em direção ao endstop MIN
- Para quando endstop é acionado
- Atualiza posição global para (0, 0, 0)
- **Ordem**: Z primeiro, depois X, depois Y

---

#### `move_axes_to_steps(long target_x, long target_y, long target_z)`
**Propósito**: Move os eixos para posições específicas (em passos)  
**Parâmetros**:
- `target_x`: Posição-alvo X em passos
- `target_y`: Posição-alvo Y em passos
- `target_z`: Posição-alvo Z em passos

**Detalhes**:
- Calcula diferença entre posição atual e-alvo
- Move eixos independentemente até atingir alvo
- Verifica endstops durante movimento
- Atualiza `g_current_steps_*` globais

---

#### `execute_cell_operation(int cell_index, bool is_pickup_operation)`
**Propósito**: Executa operação completa de pegar ou guardar pallet  
**Parâmetros**:
- `cell_index`: Índice da célula (0-5)
- `is_pickup_operation`: true = pegar, false = guardar

**Detalhes**:
- Move para altura segura Z
- Move para posição X,Y da célula
- Lowera Z até altura de pickup
- Ativa/desativa eletroímã
- Retorna para altura segura
- Log de operação

---

### Funções de Conversão de Posição

#### `indice_para_slot(int idx)`
**Propósito**: Converte índice do array para nome do slot  
**Retorno**: String (ex: "A1", "B2", "C1")  
**Mapeamento**:
```
Índice → Slot
0      → A1
1      → A2
2      → B1
3      → B2
4      → C1
5      → C2
```

---

#### `slot_para_indice(char *slot)`
**Propósito**: Converte nome do slot para índice do array  
**Parâmetros**: `slot` - String (ex: "A1")  
**Retorno**: Índice (0-5) ou -1 se inválido  
**Exemplos**:
```c
"A1" → 0
"B2" → 3
"C1" → 4
"X5" → -1 (inválido)
```

---

### Funções do Eletroímã

#### `inicializa_eletroima()`
**Propósito**: Inicializa GPIO do eletroímã  
**Retorno**: `void`

---

#### `ativar_eletroima()`
**Propósito**: Ativa o eletroímã (coloca pino em HIGH)  
**Retorno**: `void`  
**Efeito**: Define `electromagnet_active = true`

---

#### `desativar_eletroima()`
**Propósito**: Desativa o eletroímã (coloca pino em LOW)  
**Retorno**: `void`  
**Efeito**: Define `electromagnet_active = false`

---

#### `toggle_eletroima()`
**Propósito**: Alterna estado do eletroímã (ativa ↔ desativa)  
**Retorno**: `void`

---

### Funções de Log

#### `log_push(const char *fmt, ...)`
**Propósito**: Adiciona mensagem formatada ao histórico circular  
**Parâmetros**: `fmt` - String com formato printf, seguido de argumentos  
**Detalhes**:
- Armazena até 120 linhas
- Máximo 128 caracteres por linha
- Buffer circular (sobrescreve entradas antigas)
- Thread-safe (usa mutex interno)

**Exemplos**:
```c
log_push("CNC: Iniciando Homing...");
log_push("Pallet %s moved to %s", uid, slot);
log_push("Erro: Slot inválido %d", cell_index);
```

---

#### `const char* log_get(int idx)`
**Propósito**: Recupera mensagem de log por índice  
**Parâmetros**: `idx` - Índice (0 = mais antigo)  
**Retorno**: Ponteiro para string do log  
**Detalhes**:
- Índices válidos: 0 a `g_log_count - 1`
- Retorna mensagens em ordem cronológica

---

### Funções RFID

#### `scan_for_uid(char* uid_buffer, size_t buffer_len)`
**Propósito**: Escaneia um cartão RFID e obtém sua UID  
**Parâmetros**:
- `uid_buffer`: Buffer para armazenar UID em string
- `buffer_len`: Tamanho máximo do buffer

**Retorno**: `true` se cartão lido com sucesso, `false` caso contrário  
**Detalhes**:
- Formato da UID: "12 34 56 78" (hex com espaços)
- Tenta por ~100ms
- Para o cartão automaticamente após leitura
- Thread-safe (com pausas para FreeRTOS)

**Exemplo**:
```c
char uid[32];
if (scan_for_uid(uid, sizeof(uid))) {
    printf("UID lida: %s\n", uid);
}
```

---

### Funções LCD

#### `lcd_update_line(int line, const char *fmt, ...)`
**Propósito**: Atualiza uma linha do display LCD (thread-safe)  
**Parâmetros**:
- `line`: Número da linha (0 ou 1)
- `fmt`: String com formato printf

**Detalhes**:
- Usa mutex para evitar conflito de acesso
- Trunca automaticamente para 16 caracteres
- Formata argumentos como `printf`

**Exemplos**:
```c
lcd_update_line(0, "Sistema Online");
lcd_update_line(1, "IP: 192.168.1.10");
lcd_update_line(0, "Celula %s", slot);
```

---

### Funções HTTP

#### `http_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)`
**Propósito**: Callback para receber requisições HTTP  
**Retorno**: `ERR_OK` ou código de erro LWIP  
**Rotas Processadas**:

| Rota | Método | Descrição |
|------|--------|-----------|
| `/api/log` | GET | Adiciona mensagem ao log |
| `/api/history` | GET | Retorna histórico em JSON |
| `/store` | POST | Guarda pallet em célula |
| `/retrieve` | POST | Retira pallet de célula |
| `/toggle-electromagnet` | POST | Alterna eletroímã |
| `/home` | POST | Retorna ao ponto zero |
| `/api/inventory` | GET | Retorna status das células |

**Formato de Query**:
```
/store?slot=A1      → Guarda em célula A1
/retrieve?slot=B2   → Retira de célula B2
/api/log?msg=Teste  → Log "Teste"
```

---

#### `query_param(const char *req, const char *key, char *out, size_t outsz)`
**Propósito**: Extrai parâmetro de query string  
**Parâmetros**:
- `req`: String com requisição HTTP completa
- `key`: Chave do parâmetro (ex: "slot")
- `out`: Buffer de saída
- `outsz`: Tamanho máximo

**Retorno**: `true` se parâmetro encontrado, `false` caso contrário  
**Detalhes**:
- Decodifica URL-encoded values automaticamente
- Busca após "?" na requisição

**Exemplo**:
```c
char slot[10];
if (query_param(req, "slot", slot, sizeof(slot))) {
    printf("Slot solicitado: %s\n", slot);
}
```

---

#### `url_decode_inplace(char *s)`
**Propósito**: Decodifica string URL-encoded (modifica in-place)  
**Parâmetros**: `s` - String para decodificar  
**Exemplos**:
```
"A%201" → "A 1"
"Test%20123" → "Test 123"
```

---

### Funções de Controle de Endstops

#### `check_endstop(uint pin)`
**Propósito**: Verifica se endstop está acionado  
**Parâmetros**: `pin` - GPIO do endstop  
**Retorno**: `true` se acionado, `false` caso contrário

---

#### Helpers para Endstops
- `check_endstop_x_min()` / `check_endstop_x_max()`
- `check_endstop_y_min()` / `check_endstop_y_max()`
- `check_endstop_z_min()` / `check_endstop_z_max()`

---

## 📋 Tasks do FreeRTOS

### `vPollingTask`
**Prioridade**: 1 (baixa)  
**Pilha**: 512 bytes  
**Função**:
- Executa `cyw43_arch_poll()` a cada 1 segundo
- Mantém conexão Wi-Fi ativa
- Loop infinito

---

### `vMotorControlTask`
**Prioridade**: 3 (alta)  
**Pilha**: 1024 bytes  
**Função**:
- Inicializa pinos da CNC
- Executa homing completo na inicialização
- Aguarda comandos na fila `g_movement_queue`
- Executa operações de movimento

**Fluxo**:
```
1. Inicializa pinos
2. Executa home_all_axes()
3. Move para posição segura Z
4. Loop:
   - Aguarda comando da fila (bloqueante)
   - Se comando de home: move para (0,0,0)
   - Senão: executa operação de célula
```

---

## 📊 Variáveis Globais

### Posição Atual
```c
volatile long g_current_steps_x;  // Posição X em passos
volatile long g_current_steps_y;  // Posição Y em passos
volatile long g_current_steps_z;  // Posição Z em passos
```

### Mapas de Posição
```c
CellPosition g_cell_map[6];       // Coordenadas das 6 células
char g_cell_uids[6][32];          // UIDs armazenadas em cada célula
```

### Sincronização
```c
SemaphoreHandle_t g_inventory_mutex;  // Protege g_cell_uids
SemaphoreHandle_t g_lcd_mutex;        // Protege LCD
QueueHandle_t g_movement_queue;       // Fila de comandos (5 itens)
```

### Estado
```c
bool electromagnet_active;        // true = ativo
MFRC522Ptr_t g_mfrc;             // Instância RFID
```

### Log
```c
char g_log[120][128];            // Buffer circular de logs
int g_log_head;                  // Índice da próxima escrita
int g_log_count;                 // Número de logs válidos
```

---

## 🌐 Backend Flask (dbServer.py)

O servidor Python gerencia o banco de dados SQLite com inventário e logs.

### Endpoints de Produtos
| Rota | Método | Descrição |
|------|--------|-----------|
| `/api/products` | GET | Lista todos os produtos |
| `/api/products` | POST | Cria novo produto |
| `/api/products/<id>` | PUT | Atualiza produto |
| `/api/products/<id>` | DELETE | Deleta produto |

### Endpoints de Pallets
| Rota | Método | Descrição |
|------|--------|-----------|
| `/api/pallet/register` | POST | Associa UID a produto |
| `/api/pallet/<uid>` | GET | Consulta pallet |
| `/api/pallet/<uid>` | PUT | Atualiza quantidade |
| `/api/pallet/<uid>` | DELETE | Deleta pallet |

### Endpoints de Logs
| Rota | Método | Descrição |
|------|--------|-----------|
| `/api/log` | POST | Adiciona log |
| `/api/logs` | GET | Lista logs |
| `/api/status` | GET | Status do servidor |
| `/api/clear` | DELETE | Limpa todos os logs |

---

## 🎨 Interface Web

A interface web (`Index.html`) fornece:
- **Visualização do Layout**: Grid com 6 células (A1-C2)
- **Operações de Armazenagem**: Botão para ativar modo "guardar"
- **Operações Manuais**: Controle do eletroímã
- **Histórico**: Visualização de movimentações
- **Filtro**: Filtrar logs por data

### Funcionalidades JavaScript
- Requisições FETCH para endpoints HTTP/API
- Atualização dinâmica do visual das células
- Log local com timestamp
- Comunicação em tempo real com servidor

---

## 🔍 Exemplos de Uso

### Guardar Pallet na Célula A1
```javascript
fetch('/store?slot=A1', { method: 'POST' })
  .then(res => res.json())
  .then(data => console.log(data));
```

### Retirar Pallet da Célula B2
```javascript
fetch('/retrieve?slot=B2', { method: 'POST' })
  .then(res => res.json())
  .then(data => console.log(data));
```

### Adicionar Mensagem ao Log
```javascript
fetch('/api/log?msg=Teste%20manual', { method: 'GET' });
```

### Ativar Eletroímã
```javascript
fetch('/toggle-electromagnet', { method: 'POST' })
  .then(res => res.json());
```

---

## 🐛 Troubleshooting

### Problema: "Falha ao conectar Wi-Fi"
- Verifique SSID e senha em `Controle_XYZ.c`
- Confirme que o roteador está ativo
- Verifique distância/interferência

### Problema: "Endstop não acionado durante homing"
- Verifique conexão dos endstops
- Teste pinos com multímetro
- Revise pinos em `Controle_XYZ.c`

### Problema: "LCD não exibe mensagens"
- Verifique endereço I2C (padrão 0x27)
- Teste com exemplo de LCD I2C
- Confirme conexão SDA/SCL

### Problema: "RFID não detecta cartão"
- Verifique pinos SPI (MOSI, MISO, CLK, CS, RST)
- Tente ler versão: `PCD_DumpVersionToSerial()`
- Confirme cartão é compatível (ISO 14443A)

---

## 📝 Notas Importantes

1. **Thread Safety**: Sempre use `xSemaphoreTake()` ao acessar `g_cell_uids`
2. **Altitude Z Segura**: Sempre retorne a `Z_SAFE_MM` entre operações
3. **Homing Obrigatório**: Sistema sempre faz homing no startup
4. **Buffer de Log**: Máximo 120 linhas; entradas antigas são sobrescritas
5. **Velocidade de Movimento**: Ajuste `STEP_DELAY_*_US` para otimizar

---

## 📞 Referências Externas

- **Pico SDK**: https://github.com/raspberrypi/pico-sdk
- **FreeRTOS**: https://www.freertos.org/
- **LWIP**: https://savannah.nongnu.org/projects/lwip/
- **MFRC522**: http://www.nxp.com/documents/data_sheet/MFRC522.pdf
- **LCD 1602 I2C**: https://lastminuteengineers.com/lcd-i2c-address-scanner/
