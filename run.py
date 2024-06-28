#!/usr/bin/python3

from typing import List
from os import system
from os.path import exists
from re import compile

int_range = compile(r'(\d+)(?:[~-](\d+))?')

def parse_int(range_string: str) -> List[int]:
    arr: List[int] = []
    for match in int_range.finditer(range_string):
        start = int(match.group(1))
        end = int(match.group(2)) if match.group(2) else start
        arr.extend(range(start, end + 1))
    return arr

def main():
    s: str = ''
    while True:
        s = input('Task? <(c)lean | compile-(r)elease | compile-(d)ebug | r(u)n | run-(g)db | e(x)it>: ')
        if s == 'c':
            system('make clean && clear')
        elif s == 'r':
            if system('make -j10') == 0:
                system('./bin/main > nums.txt 2>&1 && clear')
        elif s == 'd':
            if system('make -j10 BUILD=debug') == 0:
                system('./bin/main > nums.txt 2>&1 && clear')
        elif s == 'u' or s == 'g' or s == 'x':
            break
        else:
            continue
    if s == 'x':
        return

    rideables: List[int] = []
    while len(rideables) == 0:
        s = input('Rideable no.?: ')
        rideables = parse_int(s)
    print(f'Running on rideables: {rideables}')

    tests: List[int] = []
    while len(tests) == 0:
        s = input('Test no.?: ')
        tests = parse_int(s)
    print(f'Running on tests: {tests}')

    threads: List[int] = []
    while len(threads) == 0:
        s = input('Thread num.?: ')
        threads = parse_int(s)
    print(f'Running on threads: {threads}')

    if not exists('./bin/main'):
        print('Executable not found. Please compile first.')
        return
    for rideable in rideables:
        for test in tests:
            for thread in threads:
                system(f'./bin/main -r {rideable} -m {test} -t {thread}')

if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        pass
