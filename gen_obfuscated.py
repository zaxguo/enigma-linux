#!/usr/bin/python3
import os
import sys
import numpy as np
from scipy.spatial.distance import euclidean
from itertools import product
from itertools import combinations
from dtw import dtw
import matplotlib.pyplot as plt
import hashlib
import random
from skbio import Protein
from skbio.util import classproperty
from skbio.sequence  import GrammaredSequence
from skbio.alignment import global_pairwise_align
from skbio.alignment import StripedSmithWaterman
from skbio.alignment import local_pairwise_align_protein
from skbio.alignment import local_pairwise_align
from skbio.alignment import make_identity_substitution_matrix

k = 5

# read 0, write 1, llseek 2, fstat 3    ftruncate 4
# ours 5        6         7        8    ftruncate 9

ops_dic = ["read", "write", "llseek", "fstat", "ftruncate",
        "_read_", "_write_", "_llseek_", "_fstat_", "_ftruncate_", "EOF"]

# implementing malgene..

def bias(a,b):
    bias = 20 # if it is an important syscall
    return bias

def namesim(a,b):
    return


def sim(a, b):
    wa = 2 # same type and same args
    nwa = -2 # same type but different args
    wt = 3 # same type
    nwt = -2 # not same type
    return bias(a, b)*(namesim(a,b) * attribsim(a,b))

class SyscallSequence(GrammaredSequence):
    """
    This class define a custom dictionary of amino acids
    """
    @classproperty
    def degenerate_map(cls):
        return {}

    @classproperty
    def definite_chars(cls):
        #0-9
        return set("0123456789")


    @classproperty
    def default_gap_char(cls):
        return '-'

    @classproperty
    def gap_chars(cls):
        return set('-.')

# We define two sequences x, y as numpy array
# where y is actually a sub-sequence from x
# x = np.array([1,1,9,2,3, 4, 2, 1, 6, 1, 1, 1, 3, 6, 4,1,6, 2, 4,1,2, 2,2,7, 1,4, 1, 2, 8, 3,4, 1, 10, 1])#.reshape(-1, 1)
#x = np.array([9, 6, 6, 6, 8, 7, 10])#.reshape(-1, 1)
x = np.array([8, 5, 5, 5, 7, 6, 9])#.reshape(-1, 1)
orig = x


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
            tmp.append((digest[i] % 4) + 1)
        idx = digest[20] % k
        # tmp.insert(idx, t[0])
        tmp.insert(idx, t)
        x.extend(tmp)
    return x

matrix = make_identity_substitution_matrix(2, 1, '0123456789')
print(matrix)



for i in x:
    # ops.add(i[0])
    ops.add(i)

print("Orig:")
for op in x:
    print(ops_dic[op], end=', ')
print("")

def list_to_str(a):
    return ''.join(map(str, a))

for i in range(5, 9):
    y = gen_y(x, i)
    print("K = ", i, ":")
    for op in y:
        print(ops_dic[op], end=', ')
    print("")

    query = StripedSmithWaterman(list_to_str(y))
    # this is to find all possible sequence  len(comb) = n * n * n ... n = len(ops)
    # comb = product(ops, repeat=len(x))
    # this is to get combinations from a sequence len(comb) = C(m,n)
    comb = combinations(y, len(orig))
    '''
    DTW's approach
    euclidean_norm = lambda x, y: np.abs(x - y)
    d, cost_matrix, acc_cost_matrix, path = dtw(x, y, dist=euclidean_norm)
    '''
    def dtw_calc_best_x(comb):
        best = 99999999999;
        for i in list(comb):
            x = i
            euclidean_norm = lambda x, y: np.abs(x - y)
            d, cost_matrix, acc_cost_matrix, path = dtw(x, y, dist=euclidean_norm)
            if d < best:
                best = d
                best_acc_cost_matrix = acc_cost_matrix
                best_path = path
                _best_x = x
        return _best_x

    def sw_calc_best_x(comb):
        best = -99999999999;
        target = list_to_str(y)
        for i in list(comb):
            x = list_to_str(i)
            print(x)
            print(target)
            alignment, score, pos = local_pairwise_align(SyscallSequence(list_to_str(i)), SyscallSequence(target), gap_open_penalty=-2, gap_extend_penalty=-0.01, substitution_matrix = matrix)
            print(alignment)
            print(score)
            if score > best:
                best = score
                _best_x = x
        return _best_x

    #  best_x = dtw_calc_best_x(comb)
    best_x = sw_calc_best_x(comb)
    # best_x = [6, 9, 6, 8, 6, 10, 6] K = 5
    # best_x = [6, 9, 6, 8, 6, 10, 6] K = 6
    # best_x = [6, 9, 6, 8, 6, 10, 6] K = 7
    # best_x = (6, 9, 9, 6, 8, 6, 10) K = 8
    best, best_cost_matrix, best_acc_cost_matrix, best_path = dtw(best_x, y, dist=euclidean_norm)

    print(best, best_x)

    #plt.imshow(best_acc_cost_matrix.T - acc_cost_matrix.T, origin='lower', cmap='gray', interpolation='nearest')

    fig = plt.gcf()
    plt.plot(path[0], path[1], 'w')
    plt.plot(best_path[0], best_path[1], 'r')
    plt.xticks(np.arange(0, len(x), 1))
    name = "test-dtw-" + str(i) + ".pdf"
    fig.savefig(name, bbox_layout='tight')


