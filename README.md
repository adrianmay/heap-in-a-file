# heap-in-a-file
Persistent storage with palloc and pree

This is a very basic allocator over a memory mapped file.

It works like the glibc malloc but simplified:

There's a master state structure containing the "root" pointer (see below), the address of the "top" chunk and lots of singly-linked free lists, a.k.a. "bins".

The sizes of chunks (including overhead) are rounded up to a multiple of 16 bytes. 

There's a bin for each size. This means we can always pull from the head of a bin so it may be singly linked.

Right now, there's a brutal maximum chunk size but that could be fixed with an oversize bin, or by dynamically creating a bin for each huge size requested.

Bins are sorted by the location of the chunk so we tend to serve up free chunks from earlier in the file and free up the later parts. That's a headstart on trying to shrink the file when possible, but that has not yet been implemented.

The chunk header consists of the size of the previous chunk (only if said previous chunk is free, otherwise available for the user), the size of this chunk ORed with the in-use flag for the previous chunk at bit 0, and a pointer to the next free chunk in the same bin if this chunk is free. The overhead for in-use chunks is just one int for the size and prev-in-use flag.

That peculiar arrangement is useful for "consolidating" chunks, i.e., fusing a newly freed chunk with any free neighbouring chunks. This is just a stub function at the moment, but the above scheme supports it nicely.

The reason it's useful is that consolidation involves navigating from the recently freed chunk to the ones immediately before and after it in memory. It's easy to get to the one after, but to find the header of the one before, you need to know its length - if the length was only in its header, there'd be a chicken-and-egg problem. Therefore we put it in the "footer" of the chunk who's length we are talking about; this footer is the first field in the header of the next chunk. For in-use chunks, the footer is not necessary and we offer the space to the user, so before we can use the footer to get back to the header, we must have a flag within reach to check that the footer is valid. That's why the in-use flag for a chunk is in the subsequent chunk.

glibc is very keen to consolidate, but in this allocator we argue that if the program often allocates chunks of sizes A and B, there's reason to think it will do so again, but no reason to think it will ask for chunks of size A+B or 2A. If there's a tendency to allocate small chunks by dividing bigger ones, then consolidation becomes more useful, but that risks leaving lots of loose ends. A continuous row of free chunks at the end of the file offers a chance to truncate the file.

Anyway, in a new file there'll be a master state structure followed by a single "top" chunk starting and ending on a 16 byte boundary. The address of this top chunk is in the master state structure. The length of the file is in RAM. Newly requested chunks are carved off the top chunk, and if the top chunk gets too small, more file space will be created in chunks of 4k. 

The file is always mapped at the same address so pointers to objects in this heap can be trusted as usual.

Oh, the root pointer: this is a single persistent pointer for the use of the user. It's imagined that the user's data model consists of a singleton object with everything else dangling under it, and the root would be set to that singleton. It will be null in a newly created file, and the user is expected to detect that and perform first-time initialisation.
