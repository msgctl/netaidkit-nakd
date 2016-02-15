#include <stdio.h>
#include "wrap.h"
#include "stage.h"

char *get_stage(char **args) {
    char *response = NULL;

    response = do_command(NAKD_SCRIPT("get_stage.sh"), args);

    return response;
}

char *set_stage(char **args) {
    char *response = NULL;

    response = do_command(NAKD_SCRIPT("set_stage.sh"), args);

    return response;
}

char *toggle_tor(char **args) {
    char *response = NULL;

    response = do_command(NAKD_SCRIPT("toggle_tor.sh"), args);

    return response;
}

char *toggle_vpn(char **args) {
    char *response = NULL;

    response = do_command(NAKD_SCRIPT("toggle_vpn.sh"), args);

    return response;
}
