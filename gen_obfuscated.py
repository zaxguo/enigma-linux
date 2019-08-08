#!/usr/bin/python3
import os
import sys
import numpy as np
from scipy.spatial.distance import euclidean
from itertools import product
from dtw import dtw
import matplotlib.pyplot as plt
import hashlib
import random

k = 8

# read 1, write 2, llseek 3, fstat 4    ftruncate 5
# ours 6        7         8        9    ftruncate 10


# We define two sequences x, y as numpy array
# where y is actually a sub-sequence from x
# x = np.array([1,1,9,2,3, 4, 2, 1, 6, 1, 1, 1, 3, 6, 4,1,6, 2, 4,1,2, 2,2,7, 1,4, 1, 2, 8, 3,4, 1, 10, 1])#.reshape(-1, 1)
x = np.array([9, 6, 6, 6, 7, 8, 10])#.reshape(-1, 1)

ops = set()

def gen_y(y, k):
    x = []
    pc = 0
    for t in y:
        # func + pc 
        string = str(t) + str(pc)
        pc += 1
        hash_val = hashlib.sha256(string.encode())
        digest = bytearray(hash_val.digest())
        #   k - 1 dummy ops
        tmp = []
        for i in range(0, k - 1):
            tmp.append((digest[i] % 5) + 1)
        idx = digest[20] % k
        # tmp.insert(idx, t[0])
        tmp.insert(idx, t)
        x.extend(tmp)
    return x



y = gen_y(x, k)

for i in x:
    # ops.add(i[0])
    ops.add(i)

print("The obfuscated syscall is ", y)

comb = product(ops, repeat=len(x))


# You can also visualise the accumulated cost and the shortest path

euclidean_norm = lambda x, y: np.abs(x - y)
d, cost_matrix, acc_cost_matrix, path = dtw(x, y, dist=euclidean_norm)

print(d, x)

#plt.imshow(acc_cost_matrix.T, origin='lower', cmap='gray', interpolation='nearest')
fig = plt.gcf()

def calc_best_x(comb):
    best = 99999999999;
    for i in list(comb):
        x = i
        euclidean_norm = lambda x, y: np.abs(x - y)
        d, cost_matrix, acc_cost_matrix, path = dtw(x, y, dist=euclidean_norm)
        if d < best:
            best = d
            best_acc_cost_matrix = acc_cost_matrix 
            best_path = path
            best_x = x
    return best_x


best_x = calc_best_x(comb)
# best_x = [6, 9, 6, 8, 6, 10, 6] K = 5
# best_x = [6, 9, 6, 8, 6, 10, 6] K = 6
# best_x = [6, 9, 6, 8, 6, 10, 6] K = 7
# best_x = (6, 9, 9, 6, 8, 6, 10) K = 8
best, best_cost_matrix, best_acc_cost_matrix, best_path = dtw(best_x, y, dist=euclidean_norm)

print(best, best_x)

#plt.imshow(best_acc_cost_matrix.T - acc_cost_matrix.T, origin='lower', cmap='gray', interpolation='nearest')
delta = best_acc_cost_matrix.T - acc_cost_matrix.T

plt.imshow(delta, origin='lower', cmap='GnBu', interpolation='nearest')
plt.plot(path[0], path[1], 'w')
plt.plot(best_path[0], best_path[1], 'r')
plt.xticks(np.arange(0, len(x), 1))
#plt.yticks(np.arange(0, len(y), 1))
plt.colorbar()
#plt.show()
fig.savefig("dtw-8.pdf", bbox_layout='tight')


