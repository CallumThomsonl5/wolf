import socket
import time
import multiprocessing
import threading

base = "127.0.0."
def get_addr():
    n = 0
    while True:
        n += 1
        if (n == 7):
            n = 1
        yield base + str(n)

def make_connections(N, host):
    connections = []
    for _ in range(N):
        s = socket.socket()
        s.connect((host, 4444))
        connections.append(s)
    
    for c in connections: 
        c.send(b"A"*16384)

    expecting = 16384
    for c in connections:
        got = 0
        while got < expecting:
            msg = c.recv(expecting - got)
            got += len(msg)
    
T = 12
N = T * 1000

print(N)
procs = []
addr = get_addr()
for i in range(T):
    p = multiprocessing.Process(target=make_connections, args=[int(N/T), next(addr)])
    p.start()
    procs.append(p)

start = time.time()

for p in procs:
    p.join()

print("ended", time.time() - start)
