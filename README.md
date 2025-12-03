# 🏭 Controle XYZ - Sistema Automatizado de Armazém

**Sistema inteligente de armazenagem com CNC 3018, RFID e interface web**

[Início Rápido](#-início-rápido) • [Recursos](#-recursos) • [Hardware](#-hardware) • [Instalação](#-instalação) • [Documentação Completa](./DOCUMENTATION.md)

</div>

---

## 🎯 O que é?

**Controle XYZ** é um sistema automatizado de armazenagem executado em uma **Raspberry Pi Pico W**. Ele utiliza uma máquina CNC 3018 para movimentar pallets entre 6 posições de armazenamento, identificação por RFID, eletroímã para manipulação e uma interface web intuitiva para controle remoto.

O sistema funciona com **FreeRTOS** para multitarefa em tempo real e oferece um backend Flask com banco de dados SQLite para gestão de inventário e logs.

---

## ✨ Recursos Principais

- ✅ **Automação CNC 3D**: Movimento preciso em 3 eixos (X, Y, Z) com 6 posições de armazenamento
- ✅ **Arquitetura Dual-Core**: Core 0 dedicado ao controle de hardware e Core 1 dedicado à rede (Wi-Fi/HTTP)
- ✅ **Identificação RFID**: Leitura automática de UIDs dos pallets (MFRC522)
- ✅ **Eletroímã Inteligente**: Pegar e soltar pallets automaticamente
- ✅ **Interface Web Responsiva**: Painel de controle moderno com real-time updates
- ✅ **Multitarefa em Tempo Real**: FreeRTOS com 2 tasks prioritárias
- ✅ **Display LCD**: Feedback visual no display 16x2 com I2C
- ✅ **Wi-Fi Integrado**: Servidor HTTP nativo no Pico W
- ✅ **Backend Python**: Gerenciamento de banco de dados SQLite
- ✅ **Segurança e Autenticação**: Sistema de Login com tokens JWT e proteção de rotas
- ✅ **Sistema de Log Circular**: Histórico de operações em memória
- ✅ **Thread-Safe**: Proteção com mutexes para operações críticas

---

## 🔧 Hardware Necessário

| Componente | Especificação | Função |
|-----------|---------------|--------|
| **Microcontrolador** | Raspberry Pi Pico W | Cérebro do sistema |
| **Motor XY** | NEMA 17 (2 unidades) | Movimentação horizontal |
| **Motor Z** | NEMA 17 | Movimentação vertical |
| **Drivers** | A4988 (3 unidades) | Controle dos motores |
| **Sensores** | Endstops (6 unidades) | Limites de movimento |
| **RFID** | MFRC522 | Identificação de pallets |
| **Eletroímã** | 24V DC | Pegar/soltar pallets |
| **Display** | LCD 16x2 I2C | Interface visual |
| **Máquina Base** | CNC 3018 | Estrutura mecânica |
| **Wi-Fi** | Integrado (Pico W) | Conectividade |

---

## 🚀 Início Rápido

### 1️⃣ Clonar o Repositório
```bash
git clone https://github.com/G-Aguiar-Dev/Controle_XYZ.git
cd Controle_XYZ
```

### 2️⃣ Compilar o Firmware
```bash
mkdir build && cd build
cmake ..
make -j4
```

### 3️⃣ Fazer Upload para Pico W
- Pressione **BOOTSEL** enquanto conecta o cabo USB
- Copie `Controle_XYZ.uf2` para a unidade PICO montada

### 4️⃣ Iniciar Backend Flask
```bash
python dbServer.py
```

### 5️⃣ Acessar a Interface
- Descobrir IP do Pico: Verifique saída serial
- Abrir navegador: `http://<IP_PICO>`

---

## 📁 Estrutura do Projeto

```
Controle_XYZ/
├── Controle_XYZ.c          # Firmware principal
├── CMakeLists.txt          # Build configuration
├── Index.html              # Interface web
├── dbServer.py             # Backend Flask
├── INSTALL.md              # Guia de instalação
├── README.md               # Este arquivo
├── DOCUMENTATION.md        # Documentação técnica completa
└── lib/
    ├── mfrc522.*           # Driver RFID
    ├── lcd_1602_i2c.*      # Driver LCD
    ├── HTML.*              # Gerador de HTML
    ├── FreeRTOSConfig.h    # Configuração FreeRTOS
    └── sqlite/             # SQLite
```

---

## 🎮 Como Usar

### Interface Web
1. **Armazenar Pallet**
   - Clique em "Iniciar Armazenamento"
   - Clique em uma célula vazia
   - Confirme a operação

2. **Retirar Pallet**
   - Clique em uma célula ocupada (amarela)
   - Confirme a remoção

3. **Controle Manual**
   - Botão de eletroímã para operações manuais
   - Visualize histórico de movimentações

### API HTTP
```bash
# Armazenar em A1
curl -X POST http://192.168.1.XX/store?slot=A1

# Retirar de B2
curl -X POST http://192.168.1.XX/retrieve?slot=B2

# Adicionar ao log
curl "http://192.168.1.XX/api/log?msg=Teste"
```

---

## 📊 Capacidade e Especificações

| Aspecto | Valor |
|--------|-------|
| **Posições de Armazenamento** | 6 (3 colunas × 2 linhas) |
| **Área de Trabalho** | 300mm × 180mm |
| **Velocidade XY** | ~800 µs por passo |
| **Velocidade Z** | ~1200 µs por passo |
| **Histórico de Logs** | 120 entradas (128 chars cada) |
| **Fila de Comandos** | 5 operações máximo |
| **Altura de Pickup** | 45mm |
| **Taxa de Atualização LCD** | ~1 Hz |

---

## 🔌 Configuração de Rede

```c
// Wi-Fi
SSID:  "Armazem XYZ"
PASS:  "tic37#grupo4"

// HTTP Server
PORT:  80 (Pico)
BACKEND: 5000 (Python)
```

---

## 📚 Documentação Completa

Para documentação técnica detalhada sobre funções, variáveis globais, tasks do FreeRTOS e troubleshooting, consulte:

➡️ **[DOCUMENTATION.md](./DOCUMENTATION.md)**

Inclui:
- Referência completa de funções
- Configuração de pinos GPIO
- Constantes de sistema
- Tasks do FreeRTOS
- Backend Flask API
- Exemplos de código

---

## 🐛 Troubleshooting Rápido

| Problema | Solução |
|----------|---------|
| **Wi-Fi não conecta** | Verifique SSID/senha e proximidade do roteador |
| **Motores não se movem** | Verifique alimentação dos drivers A4988 |
| **LCD em branco** | Confirme endereço I2C (0x27) e conexão |
| **RFID não lê cartão** | Verifique posicionamento e pinos SPI |
| **Homing não funciona** | Teste endstops com multímetro |

Mais detalhes em [DOCUMENTATION.md → Troubleshooting](./DOCUMENTATION.md#-troubleshooting)

---

## 📋 Requisitos de Compilação

- **Pico SDK** v1.5.0+
- **CMake** 3.13+
- **ARM GCC Toolchain**
- **FreeRTOS** com suporte RP2040
- **Python 3.8+** (para backend)

Veja [INSTALL.md](./INSTALL.md) para instruções completas de setup.

---

## 🔄 Fluxo de Operação

```
┌─────────────────────────────────────────────────┐
│  Sistema Inicia                                 │
│  1. Inicializa I2C/LCD                          │
│  2. Conecta Wi-Fi                               │
│  3. Inicializa RFID                             │
│  4. Cria Tasks FreeRTOS                         │
│  5. Executa Homing completo (0,0,0)             │
└─────────────────────────────────────────────────┘
                    ↓
┌─────────────────────────────────────────────────┐
│  Aguardando Comandos                            │
│  - Interface Web monitora estado                │
│  - Motor Task aguarda na fila                   │
│  - Polling Task mantém Wi-Fi ativo              │
└─────────────────────────────────────────────────┘
                    ↓
┌─────────────────────────────────────────────────┐
│  Comando Recebido (ex: /store?slot=A1)          │
│  1. Valida slot                                 │
│  2. Enfileira comando                           │
│  3. Motor Task processa:                        │
│     - Move para altura segura Z                 │
│     - Move para célula XY                       │
│     - Ativa eletroímã                           │
│     - Retorna altura segura                     │
│  4. Log registrado                              │
│  5. LCD atualizado                              │
└─────────────────────────────────────────────────┘
```

---

## 🤝 Contribuindo

Sugestões e melhorias são bem-vindas! Para contribuir:

1. Faça um Fork do projeto
2. Crie uma branch para sua feature (`git checkout -b feature/AmazingFeature`)
3. Commit suas mudanças (`git commit -m 'Add some AmazingFeature'`)
4. Push para a branch (`git push origin feature/AmazingFeature`)
5. Abra um Pull Request

---

## 📞 Contato e Suporte

- **Repositório**: [G-Aguiar-Dev/Controle_XYZ](https://github.com/G-Aguiar-Dev/Controle_XYZ)
- **Issues**: [GitHub Issues](https://github.com/G-Aguiar-Dev/Controle_XYZ/issues)

---

## 📜 Licença

Projeto desenvolvido para **EmbarcaTech 2025** pelo **Grupo 4 - Vitória da Conquista**.

</div>
