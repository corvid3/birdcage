# birdcage
simple memory allocator designed for use with a provided buffer.

in sandboxed systems, such as DSLs for games or user-facing applications,
it's nice to be able to arbitrarily request some memory using
a raw memory API, while still not going over some set memory limit.
this is especially nice when working with a GC, where sometimes
one does not want sandboxed memory to live within a moving GC.

this library solves that problem by providing a simple free-list allocator that
works over a fixed-size buffer. simply pass a buffer of a given bytesize,
get a birdcage, and start m-allocating and freeing.

this is by no means a high performance library, more of a useful tool
for uncommon persistent allocations where the standard method
of allocation in an application would not suffice.
