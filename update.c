#include <stdio.h>
#include "wrap.h"
#include "update.h"

char *do_update(char **args) {
    char *response = NULL;

    response = do_command(NAKD_SCRIPT("do_update.sh"), args);

    return response;
}
