#ifndef MISC_H
#define MISC_H

#define N_ELEMENTS(arr) (sizeof(arr) / sizeof(arr[0]))
#define ARRAY_END(arr) (arr + N_ELEMENTS(arr))
#define ARRAY_ELEMENT_NUMBER(ptr, arr) ((int)(ptr - arr))

#endif
