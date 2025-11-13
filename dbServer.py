from flask import Flask, request, jsonify
import sqlite3
import datetime
import os
from flask_cors import CORS

app = Flask(__name__)
CORS(app)  # Permite requisicoes CORS

# --- [NOVO] Adicionado para mostrar 'Açúcar' e 'Feijão' corretamente ---
app.config['JSON_AS_ASCII'] = False

# Configuração do banco de dados
DB_PATH = 'dbServer.db'

def init_db():
    """Inicializa o banco de dados com as tabelas de logs E inventário"""
    conn = sqlite3.connect(DB_PATH, check_same_thread=False)
    cursor = conn.cursor()
    
    # Habilita o suporte a chaves estrangeiras (importante para o DELETE)
    cursor.execute("PRAGMA foreign_keys = ON;")
    
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
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS products (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL UNIQUE,
            default_quantity INTEGER DEFAULT 50
        )
    ''')
    
    # --- Tabela de Pallets ---
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS pallets (
            uid TEXT PRIMARY KEY,
            product_id INTEGER NOT NULL,
            current_quantity INTEGER NOT NULL,
            last_seen DATETIME DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (product_id) REFERENCES products (id) ON DELETE RESTRICT
        )
    ''')
    
    # --- Adicionar dados experimentais (Corrigido para UTF-8) ---
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
# ENDPOINTS DE PRODUTOS (Products) - CRUD COMPLETO
# ===================================================================

@app.route('/api/products', methods=['GET'])
def get_products():
    """[GET] Lista todos os produtos mestres (Açúcar, Feijão, etc.)"""
    try:
        conn = sqlite3.connect(DB_PATH, check_same_thread=False)
        conn.row_factory = sqlite3.Row 
        cursor = conn.cursor()
        cursor.execute('SELECT id, name, default_quantity FROM products ORDER BY name')
        products = [dict(row) for row in cursor.fetchall()]
        conn.close()
        return jsonify(products)
    except Exception as e:
        print(f"Erro ao buscar produtos: {str(e)}")
        return jsonify({'error': str(e)}), 500

@app.route('/api/products', methods=['POST'])
def create_product():
    """[POST] Cria um novo produto."""
    try:
        data = request.json
        name = data.get('name')
        qty = data.get('default_quantity', 50) # Padrão 50 se não for fornecido

        if not name:
            return jsonify({'error': 'Name is required'}), 400

        conn = sqlite3.connect(DB_PATH, check_same_thread=False)
        cursor = conn.cursor()
        cursor.execute("INSERT INTO products (name, default_quantity) VALUES (?, ?)", (name, qty))
        conn.commit()
        new_id = cursor.lastrowid
        conn.close()
        
        print(f"Produto criado - ID: {new_id}, Nome: {name}")
        return jsonify({'id': new_id, 'name': name, 'default_quantity': qty}), 201

    except sqlite3.IntegrityError: # Pega a violação da restrição UNIQUE (nome duplicado)
        return jsonify({'error': 'Product name must be unique'}), 409
    except Exception as e:
        print(f"Erro ao criar produto: {str(e)}")
        return jsonify({'error': str(e)}), 500

@app.route('/api/products/<int:id>', methods=['PUT'])
def update_product(id):
    """[PUT] Atualiza um produto existente (nome ou quantidade padrão)."""
    try:
        data = request.json
        name = data.get('name')
        qty = data.get('default_quantity')

        if not name and qty is None:
            return jsonify({'error': 'At least one field (name, default_quantity) is required'}), 400

        conn = sqlite3.connect(DB_PATH, check_same_thread=False)
        cursor = conn.cursor()

        # Constrói a query de atualização dinamicamente
        fields = []
        params = []
        if name:
            fields.append("name = ?")
            params.append(name)
        if qty is not None:
            fields.append("default_quantity = ?")
            params.append(qty)
        
        params.append(id)
        query = f"UPDATE products SET {', '.join(fields)} WHERE id = ?"

        cursor.execute(query, tuple(params))
        count = cursor.rowcount
        conn.commit()
        conn.close()

        if count == 0:
            return jsonify({'error': 'Product not found'}), 404
        
        print(f"Produto atualizado - ID: {id}")
        return jsonify({'status': 'success', 'id': id}), 200

    except sqlite3.IntegrityError:
        return jsonify({'error': 'Product name must be unique'}), 409
    except Exception as e:
        print(f"Erro ao atualizar produto: {str(e)}")
        return jsonify({'error': str(e)}), 500

@app.route('/api/products/<int:id>', methods=['DELETE'])
def delete_product(id):
    """[DELETE] Deleta um produto."""
    try:
        conn = sqlite3.connect(DB_PATH, check_same_thread=False)
        cursor = conn.cursor()
        
        # Habilita FK para esta conexão (necessário para o 'ON DELETE RESTRICT')
        cursor.execute("PRAGMA foreign_keys = ON;")
        
        cursor.execute("DELETE FROM products WHERE id = ?", (id,))
        count = cursor.rowcount
        conn.commit()
        conn.close()

        if count == 0:
            return jsonify({'error': 'Product not found'}), 404
        
        print(f"Produto deletado - ID: {id}")
        return jsonify({'status': 'success', 'deleted_id': id}), 200

    except sqlite3.IntegrityError as e: # Pega a restrição de FK
        return jsonify({'error': 'Cannot delete product, it is in use by pallets. Delete pallets first.'}), 409
    except Exception as e:
        print(f"Erro ao deletar produto: {str(e)}")
        return jsonify({'error': str(e)}), 500

# ===================================================================
# ENDPOINTS DE PALLETS (Pallets) - CRUD COMPLETO
# ===================================================================

@app.route('/api/pallet/register', methods=['POST'])
def register_pallet(uid=None):
    """[POST] Registra um UID a um produto (ou atualiza se já existe). Reseta a quantidade para o padrão."""
    try:
        data = request.json
        uid = data.get('uid')
        product_name = data.get('product_name')

        if not uid or not product_name:
            return jsonify({'error': 'UID and product_name are required'}), 400

        conn = sqlite3.connect(DB_PATH, check_same_thread=False)
        cursor = conn.cursor()

        # 1. Encontrar o ID e a quantidade padrão do produto
        cursor.execute("SELECT id, default_quantity FROM products WHERE name = ?", (product_name,))
        product = cursor.fetchone()

        if not product:
            conn.close()
            return jsonify({'error': f'Produto "{product_name}" não encontrado no banco'}), 404

        product_id = product[0]
        quantity = product[1] # Pega a quantidade padrão

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
    """[GET] Consulta os dados de um pallet específico pelo seu UID"""
    try:
        conn = sqlite3.connect(DB_PATH, check_same_thread=False)
        conn.row_factory = sqlite3.Row
        cursor = conn.cursor()
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

@app.route('/api/pallet/<path:uid>', methods=['PUT'])
def update_pallet_quantity(uid):
    """[PUT] Atualiza a quantidade (current_quantity) de um pallet existente."""
    try:
        data = request.json
        qty = data.get('current_quantity')

        if qty is None:
            return jsonify({'error': 'current_quantity is required'}), 400
        
        try:
            qty = int(qty)
        except ValueError:
            return jsonify({'error': 'current_quantity must be an integer'}), 400

        conn = sqlite3.connect(DB_PATH, check_same_thread=False)
        cursor = conn.cursor()
        cursor.execute("UPDATE pallets SET current_quantity = ?, last_seen = CURRENT_TIMESTAMP WHERE uid = ?", (qty, uid))
        count = cursor.rowcount
        conn.commit()
        conn.close()

        if count == 0:
            return jsonify({'error': 'Pallet not found'}), 404
        
        print(f"Quantidade do pallet atualizada - UID: {uid}, Qtd: {qty}")
        return jsonify({'status': 'success', 'uid': uid, 'current_quantity': qty}), 200

    except Exception as e:
        print(f"Erro ao atualizar pallet: {str(e)}")
        return jsonify({'error': str(e)}), 500

@app.route('/api/pallet/<path:uid>', methods=['DELETE'])
def delete_pallet(uid):
    """[DELETE] Deleta um pallet do inventário."""
    try:
        conn = sqlite3.connect(DB_PATH, check_same_thread=False)
        cursor = conn.cursor()
        cursor.execute("DELETE FROM pallets WHERE uid = ?", (uid,))
        count = cursor.rowcount
        conn.commit()
        conn.close()

        if count == 0:
            return jsonify({'error': 'Pallet not found'}), 404
        
        print(f"Pallet deletado - UID: {uid}")
        return jsonify({'status': 'success', 'deleted_uid': uid}), 200

    except Exception as e:
        print(f"Erro ao deletar pallet: {str(e)}")
        return jsonify({'error': str(e)}), 500

# ===================================================================
# ENDPOINTS DE LOG (Seu código original, sem mudanças)
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
    # [MODIFICADO] Atualizado para mostrar TODOS os endpoints
    return """
    <h1>Controle XYZ - Servidor de Logs e Inventário</h1>
    
    <h2>Endpoints de Produtos (Products)</h2>
    <ul>
        <li><strong>GET /api/products</strong> - Listar todos os produtos</li>
        <li><strong>POST /api/products</strong> - Criar um novo produto</li>
        <li><strong>PUT /api/products/&lt;id&gt;</strong> - Atualizar um produto pelo ID</li>
        <li><strong>DELETE /api/products/&lt;id&gt;</strong> - Deletar um produto pelo ID</li>
    </ul>

    <h2>Endpoints de Pallets (Pallets)</h2>
    <ul>
        <li><strong>POST /api/pallet/register</strong> - Associar um UID a um produto (ou resetar)</li>
        <li><strong>GET /api/pallet/&lt;uid&gt;</strong> - Consultar dados de um pallet específico</li>
        <li><strong>PUT /api/pallet/&lt;uid&gt;</strong> - Atualizar a quantidade de um pallet</li>
        <li><strong>DELETE /api/pallet/&lt;uid&gt;</strong> - Deletar um pallet</li>
    </ul>

    <h2>Endpoints de Logs e Status</h2>
    <ul>
        <li><strong>POST /api/log</strong> - Adicionar log</li>
        <li><strong>GET /api/logs</strong> - Listar logs</li>
        <li><strong>GET /api/status</strong> - Status do servidor e estatísticas</li>
        <li><strong>DELETE /api/clear</strong> - Limpar todos os logs</li>
    </ul>
    """

if __name__ == '__main__':
    print("=== Controle XYZ - Servidor de Logs e Inventário ===")
    print("Inicializando servidor...")
    
    init_db() # Garante que TODAS as tabelas são criadas
    
    print(f"Banco de dados em: {DB_PATH}")
    print("Servidor rodando em: http://0.0.0.0:5000")
    print("Pressione Ctrl+C para parar o servidor")
    
    app.run(host='0.0.0.0', port=5000, debug=True)
