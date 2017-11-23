#define _GNU_SOURCE

#include <stddef.h>
#include <malloc.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <errno.h>
#include "trace.h"

#define MASK_PREVINUSE 1

#define GRANULARITY 16
#define NUMBINS 1024
#define MAXCHUNK ((NUMBINS-1)*GRANULARITY)
#define MORECORE_BATCH 4096
#define MORECORE_HEADROOM 64
#define ROUNDUP_GRAN(X) ( ((X-1)/GRANULARITY+1)*GRANULARITY )
#define ROUNDUP_MORECORE(X) ( ((X-1)/MORECORE_BATCH+1)*MORECORE_BATCH )
#define INIT_TOP_SIZE GRANULARITY
#define INIT_HEAP_SIZE ROUNDUP_GRAN(INIT_TOP_SIZE + sizeof(PallocState))
// Forbid bigger chunks for now, maybe add ad-hoc/oversize bins later
// Committing to one size per bin so no need for bk pointers

typedef struct Chunk_ {
	int prevSize;
	int sizeAndFlags;
	struct Chunk_ * fd; //In in-use chunks, this space is available to programmer
} Chunk;

#define HEADER_NUDGE ( offsetof(Chunk, fd) )

typedef struct {
	void * root; //User's data model all hangs under here
	Chunk * top; //Address of final huge chunk
	Chunk * bins[NUMBINS]; //Free lists by chunk size
} PallocState;

static void * heap = 0;  //Where the file is mapped
static PallocState * pallocState() { return (PallocState*) heap; } 

static int fdheap=-1; //For mmapped heap
const char MAPFILE[] = "theheap";
static void * mapHeapAt=(void*)0x300000000000;
#define HEAPMAX (GRANULARITY*1000000L) //Reserves this much address space, but not wasteful
static int heapsize=0; //Current size of file

static int grains(int size) { return ((size+sizeof(int))-1)/GRANULARITY+1; } //See next two functions
static int roundedSizeIncHeader(int size) { return grains(size)*GRANULARITY; }
static int binIndexForTotSize(int size) { return size <=MAXCHUNK ? grains(size) : 0; }
static int justSize(int sizeandflags) { return sizeandflags & -2; }
static int justFlags(int sizeandflags) { return sizeandflags & 1; }
static Chunk * successor(Chunk * pCh) { return ((Chunk*)(((void*)pCh) + justSize(pCh->sizeAndFlags))); }
static void setInUse(Chunk * pCh) { successor(pCh)->sizeAndFlags |= 1; }
static void clearInUse(Chunk * pCh) { successor(pCh)->sizeAndFlags &= -2; }

static Chunk * removeHead(Chunk ** bin) /*Dont call if bin empty*/ { 
	Chunk * victim = *bin;
	*bin = (*bin)->fd;
	setInUse(victim);
	return victim;
}

// Sort free chunks in a given bin by location hoping to curtail end sometimes
static void insertSorted(Chunk * pCh, Chunk ** bin) { 
	while (*bin && *bin < pCh) bin=&((*bin)->fd);
	pCh->fd = (*bin);
	(*bin) = pCh;
}

static void * consolidate(void * p) { return p; } //TBC

// Analogous to sbrk
static void * myMoreCore(int s) {
	int wantheapsize = heapsize+s;
	void * ret = heap + heapsize;
	TRACE( ("Asked for another %x when got %x\n", (unsigned int) s, (unsigned int) heapsize) )
	int newsize = ROUNDUP_MORECORE(wantheapsize);
	ftruncate(fdheap, newsize);
	if (newsize>heapsize)
		memset(heap+heapsize, 0, newsize-heapsize);
	heapsize = newsize;
	TRACE( ("Resized heap file\n") )
	return ret; //Yes it should be the old end+1
}

static int getMoreCore(int size) {
	int tograb = ROUNDUP_MORECORE(size);
	myMoreCore(tograb);
	pallocState()->top->sizeAndFlags += tograb;
}

static Chunk * fromTop(int size) {
	Chunk * ret = pallocState()->top;
	int topprevinuse = ret->sizeAndFlags & 1;
	int topsize = ret->sizeAndFlags & -2;
	if (topsize < size+MORECORE_HEADROOM) 
		getMoreCore(size); 
	topsize = ret->sizeAndFlags & -2;
	pallocState()->top = (Chunk*)( ((void*)(pallocState()->top))+size );
	pallocState()->top->sizeAndFlags = topsize-size|1; //In use flag from new chunk.
	ret->sizeAndFlags = size|topprevinuse;
}

void * palloc(int size) {
	if (size > MAXCHUNK) return 0;
	Chunk ** bin = pallocState()->bins + binIndexForTotSize(roundedSizeIncHeader(size));
	Chunk * pCh = (*bin) ? removeHead(bin) : fromTop(roundedSizeIncHeader(size));
	TRACE( ("Allocated chunk at %p\n", pCh) )
	return ((void*)pCh) + HEADER_NUDGE;
}

void pree(void * p) {
	Chunk * pCh = p-HEADER_NUDGE;
	TRACE( ("Freeing chunk at %p\n", pCh) )
	Chunk * pCons = consolidate(pCh);
	int size = justSize(pCons->sizeAndFlags);
	Chunk * pSucc = (Chunk*)(((void*)pCons)+size);
	pSucc->prevSize = size;
	insertSorted(pCons, pallocState()->bins+binIndexForTotSize(size));
	clearInUse(pCh);
}

void closeHeap() { if (heap) munmap(heap, heapsize); if (fdheap!=-1) close(fdheap); }
void die() { closeHeap(); exit(1); }

int openHeap() {  
	struct stat st; 
	int exists=(stat(MAPFILE, &st)==0);
	if (exists) heapsize=st.st_size;
	fdheap = open(MAPFILE, O_RDWR | O_CREAT, S_IRWXU);
	if (fdheap == -1) { printf("Can't open heap file %s\n", MAPFILE); die(); }
	if (!exists) { heapsize=INIT_HEAP_SIZE; ftruncate(fdheap, heapsize); }
	heap = mmap(mapHeapAt, HEAPMAX, PROT_READ|PROT_WRITE, MAP_SHARED, fdheap, 0);
	if (heap ==(void*) -1) { printf("Failed to map heap file %s\n", MAPFILE); die(); }
	if (!exists) { 
		memset(heap, 0, heapsize); 
		int psr = ROUNDUP_GRAN(sizeof(PallocState));
		pallocState()->top = heap + psr; 
		pallocState()->top->sizeAndFlags = heapsize-psr;
	}
	TRACE( ("OPened heap file at %p\n", heap) )
	return heapsize;
}

void ** d() { return &pallocState()->root; }


