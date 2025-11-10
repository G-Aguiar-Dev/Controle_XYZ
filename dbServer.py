from flask import Flask, request, jsonify
import sqlite3
import datetime
import os
from flask_cors import CORS

app = Flask(__name__)
CORS(app)  # Permite requisições CORS

# Configuração do banco de dados
DB_PATH = 'dbServer.db'

def init_db():
    """Inicializa o banco de dados com as tabelas de logs E inventário"""
    conn = sqlite3.connect(DB_PATH, check_same_thread=False)
    cursor = conn.cursor()
    
    # --- Tabela de Logs ---
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS logs (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
            message TEXT NOT NULL,
            device_ip TEXT,
            level TEXT DEFAULT 'INFO'
        )
    ''')
    cursor.execute('CREATE INDEX IF NOT EXISTS idx_timestamp ON logs(timestamp)')
    
    # --- Tabela de Produtos ---
    # Define os tipos de itens que existem
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS products (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL UNIQUE,
            default_quantity INTEGER DEFAULT 50
        )
    ''')
    
    # --- Tabela de Pallets ---
    # Associa um UID RFID a um produto específico
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS pallets (
            uid TEXT PRIMARY KEY,
            product_id INTEGER NOT NULL,
            current_quantity INTEGER NOT NULL,
            last_seen DATETIME DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (product_id) REFERENCES products (id)
        )
    ''')
    
    # --- Adicionar dados experimentais ---
    # 'INSERT OR IGNORE' para não dar erro se já existirem
    try:
        cursor.execute("INSERT OR IGNORE INTO products (id, name, default_quantity) VALUES (1, 'Açúcar', 50)")
        cursor.execute("INSERT OR IGNORE INTO products (id, name, default_quantity) VALUES (2, 'Feijão', 30)")
        cursor.execute("INSERT OR IGNORE INTO products (id, name, default_quantity) VALUES (3, 'Arroz', 40)")
    except sqlite3.Error as e:
        print(f"Erro ao inserir dados experimentais: {e}")
        
    conn.commit()
    conn.close()
    print(f"Banco de dados inicializado com tabelas de Log e Inventário: {DB_PATH}")

# ===================================================================
# [NOVOS] ENDPOINTS DE INVENTÁRIO
# ===================================================================

@app.route('/api/products', methods=['GET'])
def get_products():
    """Lista todos os produtos mestres (Açúcar, Feijão, etc.)"""
    try:
        conn = sqlite3.connect(DB_PATH, check_same_thread=False)
        conn.row_factory = sqlite3.Row # Facilita a conversão para dict
        cursor = conn.cursor()
        cursor.execute('SELECT id, name, default_quantity FROM products ORDER BY name')
        
        # Converte o resultado para uma lista de dicionários
        products = [dict(row) for row in cursor.fetchall()]
        
        conn.close()
        return jsonify(products)
    except Exception as e:
        print(f"Erro ao buscar produtos: {str(e)}")
        return jsonify({'error': str(e)}), 500

@app.route('/api/pallet/register', methods=['POST'])
def register_pallet():
    """
    Registra ou atualiza um pallet no banco.
    Associa um UID a um nome de produto (ex: "Açúcar").
    Input JSON: { "uid": "12 34 56 78", "product_name": "Açúcar" }
    """
    try:
        data = request.json
        uid = data.get('uid')
        product_name = data.get('product_name')

        if not uid or not product_name:
            return jsonify({'error': 'UID and product_name are required'}), 400

        conn = sqlite3.connect(DB_PATH, check_same_thread=False)
        cursor = conn.cursor()

        # 1. Encontrar o ID e a quantidade padrão do produto pelo nome
        cursor.execute("SELECT id, default_quantity FROM products WHERE name = ?", (product_name,))
        product = cursor.fetchone()

        if not product:
            conn.close()
            return jsonify({'error': f'Produto "{product_name}" não encontrado no banco'}), 404

        product_id = product[0]
        quantity = product[1] # Pega a quantidade padrão (ex: 50)

        # 2. Inserir ou ATUALIZAR (REPLACE) o pallet
        cursor.execute("""
            INSERT OR REPLACE INTO pallets (uid, product_id, current_quantity, last_seen)
            VALUES (?, ?, ?, CURRENT_TIMESTAMP)
        """, (uid, product_id, quantity))
        
        conn.commit()
        conn.close()
        
        print(f"Pallet registrado/atualizado - UID: {uid}, Produto: {product_name}, Qtd: {quantity}")
        return jsonify({
            'status': 'success', 
            'uid': uid, 
            'product_id': product_id, 
            'product_name': product_name, 
            'quantity': quantity
        })

    except sqlite3.IntegrityError as e:
            return jsonify({'error': f'Database integrity error: {str(e)}'}), 409
    except Exception as e:
        print(f"Erro ao registrar pallet: {str(e)}")
        return jsonify({'error': str(e)}), 500

@app.route('/api/pallet/<path:uid>', methods=['GET'])
def get_pallet_info(uid):
    """Consulta os dados de um pallet específico pelo seu UID"""
    try:
        # O UID vem com espaços, o Flask pode tratá-los como caminhos
        # A <path:uid> captura tudo.
        
        conn = sqlite3.connect(DB_PATH, check_same_thread=False)
        conn.row_factory = sqlite3.Row
        cursor = conn.cursor()

        # Faz um JOIN para pegar o nome do produto
        cursor.execute("""
            SELECT p.uid, p.current_quantity, p.last_seen, pr.name as product_name
            FROM pallets p
            JOIN products pr ON p.product_id = pr.id
            WHERE p.uid = ?
        """, (uid,))
        
        pallet = cursor.fetchone()
        conn.close()

        if pallet:
            return jsonify(dict(pallet))
        else:
            return jsonify({'error': 'Pallet UID not found in database'}), 404

    except Exception as e:
        print(f"Erro ao buscar pallet {uid}: {str(e)}")
        return jsonify({'error': str(e)}), 500

# ===================================================================
# ENDPOINTS DE LOG (Seu código original)
# ===================================================================

@app.route('/api/log', methods=['POST'])
def add_log():
    """Adiciona um novo log ao banco de dados"""
    try:
        data = request.json
        if not data or 'message' not in data:
            return jsonify({'error': 'Message is required'}), 400
            
        conn = sqlite3.connect(DB_PATH, check_same_thread=False)
        cursor = conn.cursor()
        cursor.execute(
            'INSERT INTO logs (message, device_ip, level) VALUES (?, ?, ?)',
            (data['message'], request.remote_addr, data.get('level', 'INFO'))
        )
        conn.commit()
        log_id = cursor.lastrowid
        conn.close()
        
        print(f"Log adicionado - ID: {log_id}, Mensagem: {data['message']}")
        return jsonify({'status': 'success', 'id': log_id})
        
    except Exception as e:
        print(f"Erro ao adicionar log: {str(e)}")
        return jsonify({'error': str(e)}), 500

@app.route('/api/logs', methods=['GET'])
def get_logs():
    """Recupera logs do banco de dados"""
    try:
        limit = int(request.args.get('limit', 100))
        level = request.args.get('level', None)
        
        conn = sqlite3.connect(DB_PATH, check_same_thread=False)
        cursor = conn.cursor()
        
        if level:
            cursor.execute(
                'SELECT id, timestamp, message, device_ip, level FROM logs WHERE level = ? ORDER BY timestamp DESC LIMIT ?',
                (level, limit)
            )
        else:
            cursor.execute(
                'SELECT id, timestamp, message, device_ip, level FROM logs ORDER BY timestamp DESC LIMIT ?',
                (limit,)
            )
            
        logs = cursor.fetchall()
        conn.close()
        
        log_list = []
        for log in logs:
            log_list.append({
                'id': log[0],
                'timestamp': log[1],
                'message': log[2],
                'device_ip': log[3],
                'level': log[4]
            })
        
        return jsonify(log_list)
        
    except Exception as e:
        print(f"Erro ao recuperar logs: {str(e)}")
        return jsonify({'error': str(e)}), 500

@app.route('/api/status', methods=['GET'])
def get_status():
    """Retorna o status do servidor e estatísticas do banco"""
    try:
        conn = sqlite3.connect(DB_PATH, check_same_thread=False)
        cursor = conn.cursor()
        
        cursor.execute('SELECT COUNT(*) FROM logs')
        total_logs = cursor.fetchone()[0]
        cursor.execute('SELECT COUNT(*) FROM pallets')
        total_pallets = cursor.fetchone()[0]
        cursor.execute('SELECT COUNT(*) FROM products')
        total_products = cursor.fetchone()[0]
        
        conn.close()
        
        return jsonify({
            'status': 'online',
            'database': DB_PATH,
            'statistics': {
                'total_logs': total_logs,
                'total_pallets_registered': total_pallets,
                'total_products_defined': total_products
            }
        })
        
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/api/clear', methods=['DELETE'])
def clear_logs():
    """Limpa todos os logs (use com cuidado!)"""
    try:
        conn = sqlite3.connect(DB_PATH, check_same_thread=False)
        cursor = conn.cursor()
        cursor.execute('DELETE FROM logs')
        deleted_count = cursor.rowcount
        conn.commit()
        conn.close()
        
        print(f"Todos os logs foram limpos: {deleted_count} registros removidos")
        return jsonify({'status': 'success', 'deleted_count': deleted_count})
        
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/', methods=['GET'])
def home():
    """Página inicial simples"""
    # Atualizado para mostrar os novos endpoints
    return """
    <h1>Controle XYZ - Servidor de Logs e Inventário</h1>
    <h2>Endpoints de Log:</h2>
    <ul>
        <li><strong>POST /api/log</strong> - Adicionar log</li>
        <li><strong>GET /api/logs</strong> - Listar logs</li>
        <li><strong>GET /api/status</strong> - Status do servidor e estatísticas</li>
        <li><strong>DELETE /api/clear</strong> - Limpar todos os logs</li>
    </ul>
    <h2>Endpoints de Inventário:</h2>
    <ul>
        <li><strong>GET /api/products</strong> - Listar todos os produtos (Açúcar, Feijão...)</li>
        <li><strong>POST /api/pallet/register</strong> - Associar um UID a um produto</li>
        <li><strong>GET /api/pallet/&lt;uid&gt;</strong> - Consultar dados de um pallet específico</li>
    </ul>
    """

if __name__ == '__main__':
    print("=== Controle XYZ - Servidor de Logs e Inventário ===")
    print("Inicializando servidor...")
    
    # Usar 'memory' para testes rápidos sem criar arquivo
    # DB_PATH = ':memory:' 
    
    init_db() # Garante que TODAS as tabelas são criadas
    
    print(f"Banco de dados em: {DB_PATH}")
    print("Servidor rodando em: http://0.0.0.0:5000")
    print("Pressione Ctrl+C para parar o servidor")
    
    # debug=True é ótimo para dev, mas use check_same_thread=False no init_db
    app.run(host='0.0.0.0', port=5000, debug=True)