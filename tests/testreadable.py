import socket
import time

s = socket.socket()
s.connect(("localhost", 4444))

s.send(b"hello fucker")
s.send(b"A" * 65535)

print("DONE SEND, NOW WAIT 5 SECONDS")

time.sleep(5)

s.send(b"HELLO AGAIN FUCKER")

input("...")
s.close()
