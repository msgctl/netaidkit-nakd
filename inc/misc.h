#ifndef MISC_H
#define MISC_H

#define N_ELEMENTS(arr) (sizeof(arr) / sizeof(arr[0]))

#define ARRAY_END(arr) (arr + N_ELEMENTS(arr))
#define ARRAY_ELEMENT_NUMBER(ptr, arr) ((int)((ptr - arr)/(sizeof ptr)))

void p_error(const char *ctx, const char *err);

#define nakd_assert(stmt) __nakd_assert((stmt), #stmt, __PRETTY_FUNCTION__)
void __nakd_assert(int stmt, const char *stmt_str, const char *func);

#endif
