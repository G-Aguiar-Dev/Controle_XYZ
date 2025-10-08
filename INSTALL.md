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

- **Página inicial**: http://localhost:5000
- **Ver logs**: http://localhost:5000/api/logs
- **Status**: http://localhost:5000/api/status

## 7. Testar Manualmente

### Adicionar um log:
```powershell
curl -X POST http://localhost:5000/api/log -H "Content-Type: application/json" -d "{\"message\":\"Teste manual\", \"level\":\"INFO\"}"
```

### Ver logs:
```powershell
curl http://localhost:5000/api/logs
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

- `POST /api/log` - Adicionar log
- `GET /api/logs` - Listar logs (parâmetros: limit, level)
- `GET /api/status` - Status do servidor
- `DELETE /api/clear` - Limpar logs
- `GET /` - Página inicial