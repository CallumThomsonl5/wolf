import socket

s = socket.socket()
s.connect(("localhost", 4444))

s.send(b"hello fucker")

input("...")
s.close()
