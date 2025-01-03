import socket
import time

s = socket.socket()
s.connect(("localhost", 4444))

s.send(b"hello world")
s.send(b"A" * 65535)

print("waiting 5 seconds")

time.sleep(5)

s.send(b"hello again world")

input("...")
s.close()
