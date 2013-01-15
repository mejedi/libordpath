libordpath
==========

a codec for compression of special integer sequences

# Ordpath

This is an implementation of [ORDPATH](http://www.cs.umb.edu/~poneil/ordpath.pdf)
encoding. ORDPATH was invented by Microsoft and was used in SQL
server for compact storage of XML node labels.

Node label is a variable length octet string.  The label encodes certain
information instantly available without traversing the XML tree. In particular:

1. The label is a node identity.  Queries against XML data requires the
   elimination of duplicates according to XPath/XQuery spec.

2. Given nodes N1 and N2 it is possible to determine if N1 precedes N2 in
   the XML document using the labels alone.

3. The same holds true for the following/sibbling/parent/child/ancestor relations as well.

4. Sorting a sequence of nodes using their labels as a key results in a sequence sorted
   in the document order.

### Label Generation and Storage

In order to support these usefull properties labels are ought to be verbose. And they really are.
Basically a label is a variable length array of 64 bit numbers. Usually the length of a label is
the same as the depth of the corresponding node. However even longer labels may emerge under
certain circumstances.

Document root is assigned the label of 1. The immediate descendants of the root node could be labeled 
as 1.-5, 1.1, 1.3 and 1.7 for instance. Basically when a node is inserted into XML tree
the label is generated according to the following algorithm:

1. Use parent's label for the prefix.
2. Prepend period and another number N.
   Certain restrictions apply when selecting N, see the
   [article](http://www.cs.umb.edu/~poneil/ordpath.pdf) for
   details.

A compression is applied to labels on the fly reducing their size dramatically. That's why an efficient codec
is paramount for the practical ORDPATH implementation.

# Implementation

The implementation was thoroughly coded to exhibit the top performance.
Implementation is portable. Portions of the code were specifically
writen to take advantage of SSE2 instruction set. SSE2 code is enabled
at configuration time (ORDPATH_SSE2_BITBUF, ORDPATH_SSE2_SEARCHTREE
configuration variables). By default portable standard-conformant code
is used instead of SSE2-powered one.

Currently GCC is the only compiler supported. Support for CL (the
Microsoft compiler) is planned. We provide CMake project for building
the library and running tests. (CMake is a cross-platform Makefile
generator similar to GNU autotools).

The library was writen by Nick Zavaritsky (mejedi@gmail.com) and is
licensed under Apache License v2.0.
