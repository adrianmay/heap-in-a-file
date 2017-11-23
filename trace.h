// #define DEBUG 1
#ifdef DEBUG
#include <stdio.h>
#define TRACE(params) printf params;
#else
#define TRACE(params)
#endif
