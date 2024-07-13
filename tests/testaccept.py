import socket

s = socket.socket()
s.connect(("localhost", 4444))
input("...")
s.close()
