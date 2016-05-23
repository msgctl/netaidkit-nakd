#ifndef NAKD_SHELL_H
#define NAKD_SHELL_H
#include <json-c/json.h>
#include "command.h"

#define MAX_SHELL_RESULT_LEN 262144

#define NAKD_SCRIPT_PATH "/usr/share/nakd/scripts/"
#define NAKD_SCRIPT(filename) NAKD_SCRIPT_PATH filename

int nakd_shell_exec(const char *cwd, char **output, const char *fmt, ...);
int nakd_shell_exec_argv(const char **argv, const char *cwd, char **output);

typedef int (*nakd_traverse_cb)(const char *path, void *priv);
int nakd_traverse_directory(const char *path, nakd_traverse_cb cb, void *priv);

int nakd_shell_run_scripts(const char *dirpath);

struct cmd_shell_spec {
    const char **argv;
    const char *cwd;
};

#define CMD_SHELL_ARGV(cname, cwd, path, argv...) \
    { .name = cname, \
      .handler = (nakd_cmd_handler)(cmd_shell), \
      .priv = &(struct cmd_shell_spec) \
        { (const char*[]){ path, argv, NULL }, cwd } \
    }
#define CMD_SHELL(cname, cwd, path) \
    { .name = cname, \
      .handler = (nakd_cmd_handler)(cmd_shell), \
      .priv = &(struct cmd_shell_spec) \
        { (const char*[]){ path, NULL }, cwd } \
    }
#define CMD_SHELL_NAKD_ARGV(cname, path, argv...) \
    { .name = cname, \
      .handler = (nakd_cmd_handler)(cmd_shell), \
      .priv = &(struct cmd_shell_spec) \
        { (const char*[]){ NAKD_SCRIPT(path), argv, NULL }, \
                                         NAKD_SCRIPT_PATH } \
    }
#define CMD_SHELL_NAKD(cname, path) \
    { .name = cname, \
      .handler = (nakd_cmd_handler)(cmd_shell), \
      .priv = &(struct cmd_shell_spec) \
        { (const char*[]){ NAKD_SCRIPT(path), NULL }, NAKD_SCRIPT_PATH } \
    }

json_object *cmd_shell(json_object *jcmd, struct cmd_shell_spec *spec);

#endif
