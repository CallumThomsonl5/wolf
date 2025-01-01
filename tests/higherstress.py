import socket
import time
import multiprocessing
import threading

# def send_some_data(barrier):
#     global count
#     s = socket.socket()
#     s.connect(("localhost", 4444))

#     barrier.wait()

#     # s.send(b"FUCK YOU")

#     time.sleep(100000)

def make_connections(barrier, N):
    connections = []
    for _ in range(N):
        s = socket.socket()
        s.connect(("localhost", 4444))
        connections.append(s)
    # barrier.wait()

    for c in connections: 
        c.send(b"A"*65536 * 4)

    time.sleep(100000)

T = 12
N = T * 1000
barrier = multiprocessing.Barrier(T)

print(N)
for i in range(T):
    multiprocessing.Process(target=make_connections, args=[barrier, int(N/T)]).start()

