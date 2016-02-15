#ifndef WRAP_H
#define WRAP_H

#define NAKD_SCRIPT_PATH "/usr/share/nakd/scripts/"
#define NAKD_SCRIPT(filename) (NAKD_SCRIPT_PATH filename)

char *do_command(char *script, char *args[]);
char **build_argv(char *script, char *args[]);
void free_argv(char **argv);

#endif
