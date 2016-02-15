#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <iwlib.h>
#include <wifi.h>
#include "message.h"
#include "wrap.h"

/*  scan_wifi()
 *  Expects 0 arguments.
 */
char *scan_wifi(char **args) {
    char *response = NULL;
    char *arglist[] = {"wlan0", "scan", NULL}; // MUST END WITH NULL!

    response = do_command(NAKD_SCRIPT("iwinfo.sh"), arglist);

    return response;
}

/* Expects 2 arguments. */
char *ap_config(char **args) {
    char *response = NULL;

    response = do_command(NAKD_SCRIPT("setup_ap.sh"), args);

    return response;
}

char *get_ap_name(char **args) {
    char *response = NULL;

    response = do_command(NAKD_SCRIPT("get_ap_name.sh"), args);

    return response;
}

char *connect_wifi(char **args) {
    char *response = NULL;

    response = do_command(NAKD_SCRIPT("setup_wan.sh"), args);

    return response;
}

char *wlan_info(char **args) {
    char *response = NULL;

    response = do_command(NAKD_SCRIPT("wlan_info.sh"), args);

    return response;
}

char *toggle_broadcast(char **args) {
    char *response = NULL;

    response = do_command(NAKD_SCRIPT("toggle_broadcast.sh"), args);

    return response;
}
