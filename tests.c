#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include "persist.h"

#define NEW(CLS) ((CLS*) palloc(sizeof(CLS)))

typedef struct _TB { struct _TB * next; int i; } Testblob;
typedef struct _TBB { struct _TTB * next; int i; int j; } Testbigblob;

typedef struct { Testblob head; int culled; } Testworld;

static Testworld * W() { return (Testworld*)(*d()); }

static Testblob * newBlobAfter(int big, Testblob * parent, int i) {  
	Testblob * noob;
	if (big)
		noob = (Testblob*) NEW(Testbigblob);
	else
		noob = NEW(Testblob);
	noob->next = parent->next;
	parent->next = noob;
	noob->i = i;
	return noob;
}

static void removeBlobAfter(Testblob * parent) {
	Testblob * victim = parent->next;
	parent->next = victim->next;
	pree(victim);
}

static void cullEvery(int m) {
	Testblob * p = &W()->head; 
	do { 
		if (p->next->i == m) removeBlobAfter(p);
		p=p->next;
	} while (p!=&W()->head);
}

static void show() {
	Testblob * p = &W()->head; 
	do { 
		printf("%d ", p->i);
		p=p->next;
	} while (p!=&W()->head);
	printf("\n");
}

static void newTestWorld() {
	(*d()) = NEW(Testworld);
	W()->culled = 2;
	W()->head.next = &W()->head;
	W()->head.i = 0;
	int i; Testblob * p; 
	for (p=&W()->head,i=0;i<100;i++,p=newBlobAfter((i%2?0:1),p,i%10+1)) ;
	cullEvery(2);
}

static void makeMore(int j, int count) {
	Testblob * p; int i;
	for (p=&W()->head,i=0;i<count;i++,p=newBlobAfter((i%2?0:1),p,j)) ;
}

int main() {
	openHeap();
	if (*d()==0) {
		newTestWorld();
		printf("Made testworld\n");
	} else {
		int i; Testblob * p;
		cullEvery(++(W()->culled));
		makeMore(W()->culled+11, 5);
		show();
	}
	closeHeap();
	return 0;
}



