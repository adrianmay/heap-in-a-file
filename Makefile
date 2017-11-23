all:	test
	rm -rf theheap
	for X in `seq 1 20`; do ./test; done
	
test:	tests.c persist.c persist.h
	gcc -o test *.c

clean:
	rm -rf theheap test
