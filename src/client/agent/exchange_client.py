import socket
import struct
import json

class ExchangeClient:
    def __init__(self, host = '127.0.0.1', port = 12345):
        self.host = host
        self.port = port
        self.sock = None
    
    def connect(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((self.host, self.port))
        print(f"--- [Connected to C++ Server at {self.host} : {self.port}] ---")
    
    def send_request(self, action: str, **kwargs):
        payload = {"action": action}
        payload.update(kwargs)
        json_data = json.dumps(payload).encode('utf-8')

        header = struct.pack('!I', len(json_data))

        self.sock.sendall(header + json_data)
    
    def receive_response(self):
        header_data = self.sock.recv(4)
        if not header_data: return None

        msg_len  = struct.unpack('!I', header_data)[0]

        body_data = b""
        while len(body_data) < msg_len:
            chunk = self.sock.recv(msg_len - len(body_data))
            if not chunk: break
            body_data += chunk
        
        return json.loads(body_data.decode('utf-8'))
    
    def close(self):
        if self.sock: self.sock.close()
    



