from flask import Flask, request, jsonify
import sqlite3
import datetime
import os
from flask_cors import CORS
import hashlib
import secrets
from datetime import datetime, timedelta
import jwt
from functools import wraps

app = Flask(__name__)
CORS(app)  # Permite requisicoes CORS

# Mostra JSON com caracteres UTF-8 corretamente
app.config['JSON_AS_ASCII'] = False

JWT_SECRET = "password1234"  # Chave de acesso (Modificar)
JWT_ALGORITHM = "HS256"
JWT_EXPIRATION_HOURS = 8

# Configuração do banco de dados
DB_PATH = 'dbServer.db'

def init_db():
    """Inicializa o banco de dados com as tabelas de logs E inventário"""
    conn = sqlite3.connect(DB_PATH, check_same_thread=False)
    cursor = conn.cursor()
    
    # Tabela de Usuários
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            username TEXT NOT NULL UNIQUE,
            email TEXT NOT NULL UNIQUE,
            password_hash TEXT NOT NULL,
            role TEXT DEFAULT 'operator',
            is_active BOOLEAN DEFAULT 1,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            last_login DATETIME,
            failed_attempts INTEGER DEFAULT 0,
            locked_until DATETIME
        )
    ''')
    
    # Tabela de Sessões
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS sessions (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id INTEGER NOT NULL,
            token TEXT NOT NULL UNIQUE,
            ip_address TEXT,
            user_agent TEXT,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            expires_at DATETIME NOT NULL,
            is_active BOOLEAN DEFAULT 1,
            FOREIGN KEY (user_id) REFERENCES users (id) ON DELETE CASCADE
        )
    ''')
    
    # Tabela de Auditoria
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS audit_logs (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id INTEGER,
            action TEXT NOT NULL,
            resource TEXT,
            details TEXT,
            ip_address TEXT,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (user_id) REFERENCES users (id) ON DELETE SET NULL
        )
    ''')
    
    # Criar usuário admin padrão
    try:
        admin_password = hash_password("admin123")  # Modificar
        cursor.execute(
            "INSERT OR IGNORE INTO users (id, username, email, password_hash, role) VALUES (?, ?, ?, ?, ?)",
            (1, "admin", "admin@armazem.local", admin_password, "admin")
        )
    except:
        pass
    
    conn.commit()
    conn.close()
    print(f"Banco de dados inicializado com tabelas de Log e Inventário: {DB_PATH}")

def hash_password(password):
    """Hash de senha com salt"""
    salt = secrets.token_hex(32)
    pwd_hash = hashlib.pbkdf2_hmac('sha256', password.encode(), salt.encode(), 100000)
    return f"{salt}${pwd_hash.hex()}"

def verify_password(password, password_hash):
    """Verifica senha contra hash"""
    try:
        salt, pwd_hash = password_hash.split('$')
        new_hash = hashlib.pbkdf2_hmac('sha256', password.encode(), salt.encode(), 100000)
        return new_hash.hex() == pwd_hash
    except:
        return False

def generate_token(user_id):
    """Gera JWT token"""
    payload = {
        'user_id': user_id,
        'exp': datetime.utcnow() + timedelta(hours=JWT_EXPIRATION_HOURS),
        'iat': datetime.utcnow()
    }
    return jwt.encode(payload, JWT_SECRET, algorithm=JWT_ALGORITHM)

def verify_token(token):
    """Valida JWT token"""
    try:
        payload = jwt.decode(token, JWT_SECRET, algorithms=[JWT_ALGORITHM])
        return payload
    except:
        return None

def token_required(f):
    """Decorator para rotas protegidas"""
    @wraps(f)
    def decorated(*args, **kwargs):
        token = request.headers.get('Authorization')
        if not token:
            return jsonify({'error': 'Token ausente'}), 401
        
        # Remove 'Bearer ' se presente
        if token.startswith('Bearer '):
            token = token[7:]
        
        payload = verify_token(token)
        if not payload:
            return jsonify({'error': 'Token inválido ou expirado'}), 401
        
        request.user_id = payload['user_id']
        return f(*args, **kwargs)
    
    return decorated

def audit_log(action, resource=None, details=None):
    """Registra ação na auditoria"""
    try:
        user_id = getattr(request, 'user_id', None)
        ip_address = request.remote_addr
        
        conn = sqlite3.connect(DB_PATH, check_same_thread=False)
        cursor = conn.cursor()
        cursor.execute(
            '''INSERT INTO audit_logs (user_id, action, resource, details, ip_address) 
               VALUES (?, ?, ?, ?, ?)''',
            (user_id, action, resource, details, ip_address)
        )
        conn.commit()
        conn.close()
    except:
        pass

# ===================================================================
# ENDPOINTS DE PRODUTOS (Products)
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

# ===================================================================
# ENDPOINTS DE AUTENTICAÇÃO
# ===================================================================

@app.route('/api/auth/register', methods=['POST'])
def register():
    """[POST] Registra novo usuário (apenas admin)"""
    # Verificar token do admin
    token = request.headers.get('Authorization')
    if not token or not verify_token(token.replace('Bearer ', '')):
        return jsonify({'error': 'Não autorizado'}), 401
    
    try:
        data = request.json
        username = data.get('username')
        email = data.get('email')
        password = data.get('password')
        role = data.get('role', 'operator')
        
        if not all([username, email, password]):
            return jsonify({'error': 'Campos obrigatórios ausentes'}), 400
        
        password_hash = hash_password(password)
        
        conn = sqlite3.connect(DB_PATH, check_same_thread=False)
        cursor = conn.cursor()
        cursor.execute(
            '''INSERT INTO users (username, email, password_hash, role) 
               VALUES (?, ?, ?, ?)''',
            (username, email, password_hash, role)
        )
        conn.commit()
        new_id = cursor.lastrowid
        conn.close()
        
        audit_log("USER_CREATED", f"user_id:{new_id}", f"username:{username}")
        
        return jsonify({
            'status': 'success',
            'user_id': new_id,
            'username': username
        }), 201
    
    except sqlite3.IntegrityError:
        return jsonify({'error': 'Usuário ou email já existe'}), 409
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/api/auth/login', methods=['POST'])
def login():
    """[POST] Login de usuário"""
    try:
        data = request.json
        username = data.get('username')
        password = data.get('password')
        
        if not all([username, password]):
            return jsonify({'error': 'Username e password são obrigatórios'}), 400
        
        conn = sqlite3.connect(DB_PATH, check_same_thread=False)
        cursor = conn.cursor()
        cursor.execute(
            'SELECT id, password_hash, is_active, role, failed_attempts, locked_until FROM users WHERE username = ?',
            (username,)
        )
        user = cursor.fetchone()
        
        if not user:
            audit_log("LOGIN_FAILED", "user_unknown", f"username:{username}")
            return jsonify({'error': 'Credenciais inválidas'}), 401
        
        user_id, pwd_hash, is_active, role, failed_attempts, locked_until = user
        
        # Verifica se usuário está bloqueado
        if locked_until:
            if datetime.fromisoformat(locked_until) > datetime.utcnow():
                audit_log("LOGIN_BLOCKED", f"user_id:{user_id}", "account locked")
                return jsonify({'error': 'Conta bloqueada temporariamente'}), 403
        
        if not is_active:
            audit_log("LOGIN_FAILED", f"user_id:{user_id}", "account inactive")
            return jsonify({'error': 'Conta desativada'}), 403
        
        if not verify_password(password, pwd_hash):
            # Incrementa tentativas falhadas
            new_attempts = failed_attempts + 1
            locked_until_val = None
            
            if new_attempts >= 5:
                locked_until_val = (datetime.utcnow() + timedelta(minutes=30)).isoformat()
            
            cursor.execute(
                'UPDATE users SET failed_attempts = ?, locked_until = ? WHERE id = ?',
                (new_attempts, locked_until_val, user_id)
            )
            conn.commit()
            
            audit_log("LOGIN_FAILED", f"user_id:{user_id}", f"attempt {new_attempts}")
            return jsonify({'error': 'Credenciais inválidas'}), 401
        
        # Login bem-sucedido
        token = generate_token(user_id)
        
        # Reseta tentativas falhadas
        cursor.execute(
            '''UPDATE users SET failed_attempts = 0, locked_until = NULL, last_login = CURRENT_TIMESTAMP 
               WHERE id = ?''',
            (user_id,)
        )
        
        # Cria sessão
        cursor.execute(
            '''INSERT INTO sessions (user_id, token, ip_address, expires_at) 
               VALUES (?, ?, ?, ?)''',
            (user_id, token, request.remote_addr, 
             (datetime.utcnow() + timedelta(hours=JWT_EXPIRATION_HOURS)).isoformat())
        )
        conn.commit()
        conn.close()
        
        audit_log("LOGIN_SUCCESS", f"user_id:{user_id}", f"username:{username}")
        
        return jsonify({
            'status': 'success',
            'token': token,
            'user_id': user_id,
            'username': username,
            'role': role
        }), 200
    
    except Exception as e:
        print(f"Erro ao fazer login: {str(e)}")
        return jsonify({'error': str(e)}), 500

@app.route('/api/auth/logout', methods=['POST'])
@token_required
def logout():
    """[POST] Logout de usuário"""
    try:
        token = request.headers.get('Authorization', '').replace('Bearer ', '')
        
        conn = sqlite3.connect(DB_PATH, check_same_thread=False)
        cursor = conn.cursor()
        cursor.execute('UPDATE sessions SET is_active = 0 WHERE token = ?', (token,))
        conn.commit()
        conn.close()
        
        audit_log("LOGOUT", f"user_id:{request.user_id}")
        
        return jsonify({'status': 'success'}), 200
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/api/auth/me', methods=['GET'])
@token_required
def get_current_user():
    """[GET] Obtém dados do usuário atual"""
    try:
        conn = sqlite3.connect(DB_PATH, check_same_thread=False)
        cursor = conn.cursor()
        cursor.execute(
            'SELECT id, username, email, role, last_login FROM users WHERE id = ?',
            (request.user_id,)
        )
        user = cursor.fetchone()
        conn.close()
        
        if not user:
            return jsonify({'error': 'Usuário não encontrado'}), 404
        
        return jsonify({
            'id': user[0],
            'username': user[1],
            'email': user[2],
            'role': user[3],
            'last_login': user[4]
        }), 200
    except Exception as e:
        return jsonify({'error': str(e)}), 500