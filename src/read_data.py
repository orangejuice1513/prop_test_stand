import socket

UDP_IP = "0.0.0.0"  # Listen on all network interfaces
UDP_PORT = 4210

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((UDP_IP, UDP_PORT))

print(f"Waiting for thrust data on port {UDP_PORT}...")

while True:
    data, addr = sock.recvfrom(1024)
    payload = data.decode('utf-8')
    
    try:
        t, left, right = payload.split(',')
        total_thrust = float(left) + float(right)
        torque_val = float(left) - float(right)
        
        print(f"Time: {t}ms | Total: {total_thrust:.3f}kg | Torque: {torque_val:.3f}")
    except ValueError:
        pass