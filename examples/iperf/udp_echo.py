# save as udp_echo.py
import socket
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(('', 9000))
while True:
    data, addr = s.recvfrom(2048)
    s.sendto(data, addr)
