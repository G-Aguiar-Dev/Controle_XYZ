from flask import Flask, request, jsonify
import sqlite3
import datetime
import os

app = Flask(__name__)

# Configuração do banco de dados
DB_PATH = 'controle_xyz.db'

def init_db():
    """Inicializa o banco de dados com a tabela de logs"""
    conn = sqlite3.connect(DB_PATH)
    cursor = conn.cursor()
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS logs (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
            message TEXT NOT NULL,
            device_ip TEXT,
            level TEXT DEFAULT 'INFO'
        )
    ''')
    
    # Criar índice para melhor performance
    cursor.execute('''
        CREATE INDEX IF NOT EXISTS idx_timestamp ON logs(timestamp)
    ''')
    
    conn.commit()
    conn.close()
    print(f"Banco de dados inicializado: {DB_PATH}")

@app.route('/api/log', methods=['POST'])
def add_log():
    """Adiciona um novo log ao banco de dados"""
    try:
        data = request.json
        if not data or 'message' not in data:
            return jsonify({'error': 'Message is required'}), 400
            
        conn = sqlite3.connect(DB_PATH)
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
        
        conn = sqlite3.connect(DB_PATH)
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
        
        # Converter para lista de dicionários
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
        conn = sqlite3.connect(DB_PATH)
        cursor = conn.cursor()
        
        # Contar total de logs
        cursor.execute('SELECT COUNT(*) FROM logs')
        total_logs = cursor.fetchone()[0]
        
        # Logs por nível
        cursor.execute('SELECT level, COUNT(*) FROM logs GROUP BY level')
        logs_by_level = dict(cursor.fetchall())
        
        # Último log
        cursor.execute('SELECT timestamp, message FROM logs ORDER BY timestamp DESC LIMIT 1')
        last_log = cursor.fetchone()
        
        conn.close()
        
        return jsonify({
            'status': 'online',
            'database': DB_PATH,
            'total_logs': total_logs,
            'logs_by_level': logs_by_level,
            'last_log': {
                'timestamp': last_log[0] if last_log else None,
                'message': last_log[1] if last_log else None
            } if last_log else None
        })
        
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/api/clear', methods=['DELETE'])
def clear_logs():
    """Limpa todos os logs (use com cuidado!)"""
    try:
        conn = sqlite3.connect(DB_PATH)
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
    return """
    <h1>Controle XYZ - Servidor de Logs</h1>
    <h2>Endpoints disponíveis:</h2>
    <ul>
        <li><strong>POST /api/log</strong> - Adicionar log</li>
        <li><strong>GET /api/logs</strong> - Listar logs (parâmetros: limit, level)</li>
        <li><strong>GET /api/status</strong> - Status do servidor</li>
        <li><strong>DELETE /api/clear</strong> - Limpar todos os logs</li>
    </ul>
    <h2>Exemplo de uso:</h2>
    <p>Para adicionar um log: <code>curl -X POST http://localhost:5000/api/log -H "Content-Type: application/json" -d '{"message":"Teste", "level":"INFO"}'</code></p>
    <p>Para ver logs: <a href="/api/logs">/api/logs</a></p>
    <p>Para ver status: <a href="/api/status">/api/status</a></p>
    """

if __name__ == '__main__':
    print("=== Controle XYZ - Servidor de Logs ===")
    print("Inicializando servidor...")
    
    init_db()
    
    print("Servidor rodando em: http://localhost:5000")
    print("Pressione Ctrl+C para parar o servidor")
    
    app.run(host='0.0.0.0', port=5000, debug=True)