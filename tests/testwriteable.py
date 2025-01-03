import socket

s = socket.socket()
s.connect(("localhost", 4444))

got = 0
N = 65535 * 8 + 69
count = 0
while (got < N):
    count+=1
    msg = s.recv(N - got)
    got += len(msg)

print(got, count)

input("...")
s.close()
