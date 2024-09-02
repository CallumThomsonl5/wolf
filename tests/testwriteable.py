import socket

s = socket.socket()
s.connect(("localhost", 4444))

input("...")
msg = s.recv(1024)
print(msg)

input("...")
s.close()
