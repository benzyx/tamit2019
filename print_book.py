#!/usr/bin/env python
import time
import sys

try:
    fp = sys.argv[1]
except:
    fp = 'book.log'

last = None

while True:

    time.sleep(0.001)

    try:
        asdf = open(fp).readlines()
    except: continue

    if asdf != last and len(asdf) > 0 and 'EOF' in asdf[-1]:
        i = asdf.index('bids\n')

        print('\033c' + 'offers\n' + ''.join(asdf[max(0,i-11):min(len(asdf),i+11)]))
        last = asdf
