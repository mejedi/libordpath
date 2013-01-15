#! /usr/bin/python

# RANDLABEL.py
#
# Generates random ORDPATH label in the format compatible with
# ordpath-test.
# 
# -l, --lenght=<integer>     length of generated label
#     --setup=<filename>     read ORDPATH codec setup from the file
#                            specified; each generated component has
#                            equal probability to hit any of the
#                            intervals specified by setup
#     --clamp=<integer>      discard some intervals from setup before
#                            generation; intervals are ordered by
#                            proximity to the "sweet spot"; positive
#                            clamp value limits the length of intervals
#                            list; non-positive clamp value K is
#                            interpreted as a request to discard first
#                            |K| elements from the list

# REFENCODE.py
#
# Reference implementation of the ORDPATH codec. Reads label from stdin
# and writes results to stdout. Input and output formats are compatible
# with ordpath-test.
#
#     --setup=<filename>     read ORDPATH codec setup from the file
#                            specified

import sys, os, math, re, random, getopt

def parseSetup(s = None):
    s = s or """
        0000001 : 48
        0000010 : 32
        0000011 : 16
        000010  : 12
        000011  : 8
        00010   : 6
        00011   : 4
        001     : 3
        01      : 3 : 0 
        100     : 4
        101     : 6
        1100    : 8
        1101    : 12
        11100   : 16
        11101   : 32
        11110   : 48"""

    acc = []
    offset = 0
    cpos = 0
    sweet = 0

    for ind, m in enumerate(re.finditer(
            r'([01]+)\s*:\s*(\d+)(?:\s*:\s*(-?\d+))?', s)):
        pfx, w, orig = m.groups()
        w = int(w)
        sz = 1 << w
        if orig:
            orig = int(orig)
            sweet = ind
            offset = orig - cpos
        acc.append((cpos, cpos + sz, pfx, w))
        cpos += sz

    l = [(abs(i-sweet), b+offset, e+offset, pfx, width) 
                for (i, (b, e, pfx, width)) in enumerate(acc)]
    l.sort(lambda x, y: cmp(x[0], y[0]))
    return [val[1:] for val in l]

def inputOutput(args):
    if len(args) > 2:
        raise Exception('Excess arguments given, expecting at most 2')
    args += ['-'] * (2 - len(args))
    return (sys.stdin if args[0] == '-' else open(args[0], 'rb')), (
        sys.stdout if args[1] == '-' else open(args[1], 'wb'))
    
def randLabel(setup, l):
    return [random.randrange(*random.choice(setup)[:2]) for i in xrange(l)]

def randlabelMain(opts = []):
    length = 10
    setupstr = None 
    clamp = 0

    optlist, args = getopt.getopt(
            opts, 'l:', ['length=', 'setup=', 'clamp='])

    for o, a in optlist:
        if o in ['-l', '--length']: length = int(a)
        elif o in ['--setup']:
            with open(a) as f:
                setupstr = f.read()
        elif o in ['--clamp']: clamp = int(a)

    input, output = inputOutput(args)

    setup = parseSetup(setupstr)
    clamped = setup[-clamp : ] if clamp <= 0 else setup[ : clamp]
    data = randLabel(clamped, length)
    output.write('\n'.join(map(str, data)) + '\n')

def refEncode(setup, label):
    return ''.join([(lambda (b, e, pfx, width):
                    pfx + str.format("{0:0{1}b}", c-b, width)) (
                        next((i for i in setup if c >= i[0] and c < i[1]))) 
            for c in label])

def refencodeMain(opts = []):
    setupstr = None

    optlist, args = getopt.getopt(opts, '', ['setup='])

    for o, a in optlist:
        if o in ['--setup']:
            with open(a) as f:
                setupstr = f.read()

    input, output = inputOutput(args)

    setup = parseSetup(setupstr)
    label = map(int, input.read().split())
    elabel = refEncode(setup, label)
    output.write("%-15d\n" % len(elabel))
    t = elabel + '0' * 7
    encoded = ''.join((chr(int(t[i:i+8], 2)) for i in range(0, len(elabel),8)))
    output.write(encoded)

if __name__ == '__main__':
    {
        'refencode.py': refencodeMain,
        'randlabel.py': randlabelMain

    } [os.path.basename(sys.argv[0])] (sys.argv[1:])

