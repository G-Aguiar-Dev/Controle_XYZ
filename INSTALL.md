# Instruções para Executar o Servidor de Logs

## 1. Instalar Python

### Opção A: Via Microsoft Store (Recomendado)
1. Abra a Microsoft Store
2. Procure por "Python 3.11" ou "Python 3.12"
3. Clique em "Instalar"

### Opção B: Via python.org
1. Vá para https://www.python.org/downloads/
2. Baixe a versão mais recente para Windows
3. Execute o instalador
4. **IMPORTANTE**: Marque a opção "Add Python to PATH"

### Opção C: Via Terminal (Ubuntu/Linux)

1. sudo apt update
2. sudo apt install python3 python3-pip python3-venv

### Opção C: Via winget (se disponível)
```powershell
winget install Python.Python.3.11
```

## 2. Verificar Instalação

Após instalar, **feche e reabra o terminal**, então teste:

```powershell
python --version
```

Deve mostrar algo como: `Python 3.11.x`

## 3. Instalar Dependências

```powershell
pip install flask requests
```

## 4. Executar o Servidor

```powershell
python dbServer.py
ou
python3 dbServer.py
```

Você deve ver:
```
=== Controle XYZ - Servidor de Logs ===
Inicializando servidor...
Banco de dados inicializado: controle_xyz.db
Servidor rodando em: http://localhost:5000
Pressione Ctrl+C para parar o servidor
```

## 5. Testar o Servidor

Em outro terminal (deixe o servidor rodando):

```powershell
python test_server.py
```

## 6. Acessar via Navegador

- **Página inicial**: http://localhost:5000/
- **Ver Status e Estatísticas**: http://localhost:5000/api/status
- **Ver Lista de Produtos**: http://localhost:5000/api/products
- **Ver Lista de Logs**: http://localhost:5000/api/logs
- **Ver Pallet Específico**: http://localhost:5000/api/pallet/<uid>

## 7. Testar Manualmente (Testes POST, PUT, DELETE)

## Testar produtos

### POST (Exemplo):
```powershell / bash
curl -X POST http://localhost:5000/api/products \
     -H "Content-Type: application/json" \
     -d "{\"name\": \"Café\", \"default_quantity\": 25}"

```

### PUT (Exemplo):
```powershell / bash
curl -X PUT http://localhost:5000/api/products/4 \
     -H "Content-Type: application/json" \
     -d "{\"name\": \"Café Especial\", \"default_quantity\": 30}"

```

### DELETE (Exemplo):
```powershell / bash
curl -X DELETE http://localhost:5000/api/products/4

```

## Testar pallets

### POST (Exemplo):
```powershell / bash
curl -X POST http://localhost:5000/api/pallet/register \
     -H "Content-Type: application/json" \
     -d "{\"uid\": \"DD EE FF 00\", \"product_name\": \"Arroz\"}"

```

### PUT (Exemplo):
```powershell / bash
curl -X PUT "http://localhost:5000/api/pallet/<uid>" \
     -H "Content-Type: application/json" \
     -d "{\"current_quantity\": 10}"

```

### DELETE (Exemplo):
```powershell / bash
curl -X DELETE "http://localhost:5000/api/pallet/<uid>"

```

## Testando Logs

### POST (Exemplo):
```powershell / bash
curl -X POST http://localhost:5000/api/log \
     -H "Content-Type: application/json" \
     -d "{\"message\":\"Teste manual\", \"level\":\"INFO\"}"

```

### DELETE (Deletar todos os logs):
```powershell / bash
curl -X DELETE http://localhost:5000/api/clear

```

## 8. Integração com o Pico

1. Descubra o IP do seu computador:
   ```powershell
   ipconfig
   ```
   Procure por "Endereço IPv4" na seção da sua rede

2. No código do Pico (`Controle_XYZ.c`), altere:
   ```c
   #define DB_SERVER_IP "192.168.1.XXX"  // Substitua pelo IP do seu PC
   ```

3. Compile e execute o código do Pico

## Resolução de Problemas

### "Python não foi encontrado"
- Reinstale o Python marcando "Add to PATH"
- Reinicie o terminal/VS Code
- Tente `py` em vez de `python`

### "pip não foi encontrado"
```powershell
python -m ensurepip --upgrade
```

### "Servidor não conecta"
- Verifique se o firewall não está bloqueando a porta 5000
- Teste com `http://127.0.0.1:5000` em vez de `localhost`

### Porta já em uso
Altere a porta no `dbServer.py`:
```python
app.run(host='0.0.0.0', port=5001, debug=True)  # Mude para 5001
```

## Estrutura de Arquivos

Após executar, você terá:
```
projeto/
├── dbServer.py          # Servidor Flask
├── test_server.py       # Script de teste
├── controle_xyz.db      # Banco SQLite (criado automaticamente)
├── INSTALL.md           # Este arquivo
└── Controle_XYZ.c       # Código do Pico
```

## Endpoints da API

### Produtos

- `GET /api/products` — Listar todos os produtos
- `POST /api/products` — Criar novo produto
- `PUT /api/products/<id>` — Atualizar produto
- `DELETE /api/products/<id>` — Deletar produto

### Pallets

- `POST /api/pallet/register` — Associar/resetar UID
- `GET /api/pallet/<uid>` — Consultar pallet específico
- `PUT /api/pallet/<uid>` — Atualizar quantidade
- `DELETE /api/pallet/<uid>` — Deletar pallet

### Logs e status

- `GET /` — Página inicial
- `GET /api/status` — Status e estatísticas
- `POST /api/log` — Adicionar log
- `GET /api/logs` — Listar logs
- `DELETE /api/clear` — Limpar todos os logs