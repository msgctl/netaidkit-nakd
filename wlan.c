#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <string.h>
#include <json-c/json.h>
#include <iwinfo.h>
#include "wlan.h"
#include "ubus.h"
#include "log.h"
#include "module.h"
#include "jsonrpc.h"
#include "json.h"
#include "netintf.h"
#include "shell.h"

#define WLAN_NETWORK_LIST_PATH "/etc/nakd/wireless_networks"

#define WLAN_UPDATE_SCRIPT NAKD_SCRIPT("wlan_restart.sh")
#define WLAN_SCAN_SCRIPT NAKD_SCRIPT("wlan_scan.sh")

#define WLAN_SCAN_SERVICE "iwinfo"
#define WLAN_SCAN_METHOD "scan"

#define WLAN_DEFAULT_INTERFACE "wlan0"
#define WLAN_AP_DEFAULT_INTERFACE "wlan0"

static pthread_mutex_t _wlan_mutex;

static const char *_wlan_interface_name;
static const char *_ap_interface_name;

static json_object *_wireless_networks;
static time_t _last_scan;

static json_object *_stored_networks;
static json_object *_current_network;

const char *nakd_wlan_interface_name(void) {
    return _wlan_interface_name;
}

const char *nakd_ap_interface_name(void) {
    return _ap_interface_name;
}

static int __read_stored_networks(void) {
    int result = 0;

    FILE *fp = fopen(WLAN_NETWORK_LIST_PATH, "r");
    if (fp == NULL)
        return 1;

    /* TODO write nakd_json_parse_file, parse 4096b chunks. */
    const size_t networks_buffer_size = 262144;
    char *networks_buffer = malloc(networks_buffer_size);
    size_t size = fread(networks_buffer, 1, networks_buffer_size - 1, fp);
    networks_buffer[size] = 0;

    json_tokener *jtok = json_tokener_new();
    _stored_networks = json_tokener_parse_ex(jtok, networks_buffer, size);
    if (json_tokener_get_error(jtok) != json_tokener_success)
        result = 1;

    fclose(fp);
    json_tokener_free(jtok);
    free(networks_buffer);
    return result;
}

static void __init_stored_networks(void) {
    if (__read_stored_networks()) {
            _stored_networks = json_object_new_array();
    }

    nakd_log(L_INFO, "Read %d known networks.",
        json_object_array_length(_stored_networks)); 
}

static void __cleanup_stored_networks(void) {
    json_object_put(_stored_networks);
}

static int __save_stored_networks(void) {
    FILE *fp = fopen(WLAN_NETWORK_LIST_PATH, "w");
    if (fp == NULL)
        return 1;

    const char *networks = json_object_get_string(_stored_networks); 
    fwrite(networks, strlen(networks), 1, fp);
    fclose(fp);
    return 0;
}

const char *nakd_net_key(json_object *jnetwork) {
    json_object *jkey = NULL;
    json_object_object_get_ex(jnetwork, "key", &jkey);
    if (jkey == NULL || json_object_get_type(jkey) != json_type_string)
        return NULL;

    return json_object_get_string(jkey);
}

const char *nakd_net_ssid(json_object *jnetwork) {
    json_object *jssid = NULL;
    json_object_object_get_ex(jnetwork, "ssid", &jssid);
    if (jssid == NULL || json_object_get_type(jssid) != json_type_string)
        return NULL;

    return json_object_get_string(jssid);
}

static json_object *__get_stored_network(const char *ssid) {
    nakd_assert(_stored_networks != NULL);

    for (int i = 0; i < json_object_array_length(_stored_networks); i++) {
        json_object *jnetwork = json_object_array_get_idx(_stored_networks, i);
        const char *stored_ssid = nakd_net_ssid(jnetwork);

        if (stored_ssid == NULL) { 
            nakd_log(L_WARNING, "Malformed configuration file: " WLAN_NETWORK_LIST_PATH);
            continue;
        }

        if (!strcmp(stored_ssid, ssid))
            return jnetwork;
    } 
    return NULL;
}

static void __remove_stored_network(const char *ssid) {
    nakd_assert(_stored_networks != NULL);

    /* TODO json-c: currently there's no way to remove an array element,
     * recheck later or patch in json-c.
     */

    json_object *jupdated = json_object_new_array();

    for (int i = 0; i < json_object_array_length(_stored_networks); i++) {
        json_object *jnetwork = json_object_array_get_idx(_stored_networks, i);
        const char *stored_ssid = nakd_net_ssid(jnetwork);

        if (stored_ssid == NULL) { 
            nakd_log(L_WARNING, "Malformed configuration file: " WLAN_NETWORK_LIST_PATH);
            continue;
        }

        if (!strcmp(stored_ssid, ssid)) {
            continue;
        }

        json_object_array_add(jupdated, jnetwork);
    } 

    json_object_put(_stored_networks);
    _stored_networks = jupdated;

    if (__save_stored_networks())
        nakd_log(L_CRIT, "Couldn't remove stored network credentials: %s", ssid);
}

static json_object *_create_network_entry(json_object *jnetwork, const char *key) {
    const char *ssid = nakd_net_ssid(jnetwork);
    nakd_assert(ssid != NULL);
    json_object *jssid = json_object_new_string(ssid);
    json_object *jkey = json_object_new_string(key);

    json_object *jentry = json_object_new_object(); 
    json_object_object_add(jnetwork, "ssid", jssid);
    json_object_object_add(jnetwork, "key", jkey);
    return jentry;
}

static int __store_network(json_object *jnetwork, const char *key) {
    const char *ssid = nakd_net_ssid(jnetwork);
    if (__get_stored_network(ssid) != NULL)
        __remove_stored_network(ssid);

    json_object *jentry = _create_network_entry(jnetwork, key);
    json_object_array_add(_stored_networks, jentry);

    if (__save_stored_networks()) {
        nakd_log(L_CRIT, "Couldn't store network credentials for %s", ssid);
        return 1;
    }
    return 0;
}

static int __in_range(const char *ssid) {
    if (_wireless_networks == NULL)
        return -1;

    for (int i = 0; i < json_object_array_length(_wireless_networks); i++) {
        json_object *jnetwork = json_object_array_get_idx(_wireless_networks, i);

        const char *iter_ssid = nakd_net_ssid(jnetwork);
        nakd_assert(iter_ssid != NULL);

        if (!strcmp(iter_ssid, ssid))
            return 1;
    }
    return 0;
}

int nakd_wlan_in_range(const char *ssid) {
    pthread_mutex_lock(&_wlan_mutex);
    int s = __in_range(ssid);
    pthread_mutex_unlock(&_wlan_mutex);
    return s;
}

static json_object *__choose_network(void) {
    if (_wireless_networks == NULL)
        return NULL;

    for (int i = 0; i < json_object_array_length(_wireless_networks); i++) {
        json_object *jnetwork = json_object_array_get_idx(_wireless_networks, i);

        const char *ssid = nakd_net_ssid(jnetwork);
        nakd_assert(ssid != NULL);

        json_object *jstored = __get_stored_network(ssid);
        if (jstored != NULL)
            return jstored;
    }
    return NULL;
}

json_object *nakd_wlan_candidate(void) {
    pthread_mutex_lock(&_wlan_mutex);
    json_object *jnetwork = __choose_network();
    pthread_mutex_unlock(&_wlan_mutex);
    return jnetwork;
}

int nakd_wlan_netcount(void) {
    pthread_mutex_lock(&_wlan_mutex);
    int count = json_object_array_length(_wireless_networks);
    pthread_mutex_unlock(&_wlan_mutex);
    return count;
}

static void _wlan_update_cb(struct ubus_request *req, int type,
                                       struct blob_attr *msg) {
    json_tokener *jtok = json_tokener_new();

    char *json_str = blobmsg_format_json(msg, true);
    nakd_assert(json_str != NULL);
    if (strlen(json_str) <= 2)
        goto badmsg;

    json_object *jresponse = json_tokener_parse_ex(jtok, json_str, strlen(json_str));
    if (json_tokener_get_error(jtok) != json_tokener_success)
        goto badmsg;

    json_object *jstate = NULL;
    json_object_object_get_ex(jresponse, "results", &jstate); 
    if (jstate == NULL || json_object_get_type(jstate) != json_type_array)
        goto badmsg;

    if (!json_object_array_length(jstate)) {
        nakd_log(L_INFO, "Received an empty wireless network list, discarding.");
        goto cleanup;
    }

    pthread_mutex_lock(&_wlan_mutex);
    if (_wireless_networks != NULL)
        json_object_put(_wireless_networks);
    _wireless_networks = jstate;
    _last_scan = time(NULL);
    pthread_mutex_unlock(&_wlan_mutex);

    nakd_log(L_INFO, "Updated wireless network list. Available networks: %d",
                                                       nakd_wlan_netcount());
    goto cleanup;

badmsg:
    nakd_log(L_WARNING, "Got unusual response from " WLAN_SCAN_SERVICE 
                              " " WLAN_SCAN_METHOD ": %s.", json_str);
cleanup:
    free(json_str);
    json_tokener_free(jtok);
}

static int _wlan_scan_rpcd(void) {
    json_object *jparam = json_object_new_object();
    json_object *jdevice = json_object_new_string(_wlan_interface_name);
    json_object_object_add(jparam, "device", jdevice);
    const char *param = json_object_get_string(jparam);

    nakd_log(L_INFO, "Scanning for wireless networks."); 
    int s = nakd_ubus_call(WLAN_SCAN_SERVICE, WLAN_SCAN_METHOD, param,
                                               _wlan_update_cb, NULL);
    json_object_put(jparam);
    /* returns UBUS_STATUS_ */
    return s;
}

static int _wlan_scan_iwinfo(void) {
    int len, status = 0;
    struct iwinfo_scanlist_entry *netbuf = malloc(IWINFO_BUFSIZE);
    nakd_assert(netbuf != NULL);

    const struct iwinfo_ops *iwctx = iwinfo_backend(_wlan_interface_name);
    if (iwctx == NULL) {
        nakd_terminate("Couldn't initialize iwinfo backend (intf: %s)",
                                                 _wlan_interface_name);
    }

    if (iwctx->scanlist(_wlan_interface_name, (void *)(netbuf), &len)) {
        nakd_log(L_CRIT, "Scanning not possible");
        status = 1;
        goto cleanup;
    } else if (len <= 0) {
        nakd_log(L_DEBUG, "No scan results");
        goto cleanup;
    }

    const int count = len/(sizeof(struct iwinfo_scanlist_entry));
    json_object *jresults = json_object_new_array();
    for (struct iwinfo_scanlist_entry *e = netbuf; e < netbuf + count; e++) {
        json_object *jnetwork = json_object_new_object();
        json_object *jssid = json_object_new_string(e->ssid);
        json_object_object_add(jnetwork, "ssid", jssid); 
        json_object_array_add(jresults, jnetwork);
    }

    pthread_mutex_lock(&_wlan_mutex);
    if (_wireless_networks != NULL)
        json_object_put(_wireless_networks);
    _wireless_networks = jresults;
    _last_scan = time(NULL);
    pthread_mutex_unlock(&_wlan_mutex);

cleanup:
    free(netbuf);
    iwinfo_finish();
    return status;
}

int nakd_wlan_scan(void) {
    _wlan_scan_iwinfo();
}

static const char *_get_encryption(json_object *jnetwork) {
    /* TODO */
    return "psk2";
}

static int _update_wlan_config_ssid(struct uci_option *option, void *priv) {
    struct interface *intf = priv;
    struct uci_section *ifs = option->section;
    struct uci_context *ctx = ifs->package->ctx;
    struct uci_package *pkg = ifs->package;
    json_object *jnetwork = priv;     

    const char *pkg_name = pkg->e.name;
    const char *section_name = ifs->e.name;

    const char *ssid = nakd_net_ssid(jnetwork);
    struct uci_ptr ssid_ptr = {
        .package = pkg_name,
        .section = section_name,
        .option = "ssid",
        .value = ssid 
    };
    /* this function is called from nakd_uci_, no locking required for uci_set */
    nakd_assert(!uci_set(ctx, &ssid_ptr));

    const char *key = nakd_net_key(jnetwork);
    struct uci_ptr key_ptr = {
        .package = pkg_name,
        .section = section_name,
        .option = "key",
        .value = key
    };
    nakd_assert(!uci_set(ctx, &key_ptr));

    const char *encryption = _get_encryption(jnetwork);
    struct uci_ptr enc_ptr = {
        .package = pkg_name,
        .section = section_name,
        .option = "encryption",
        .value = encryption
    };
    nakd_assert(!uci_set(ctx, &enc_ptr));

    struct uci_ptr disabled_ptr = {
        .package = pkg_name,
        .section = section_name,
        .option = "disabled",
        .value = "0"
    };
    nakd_assert(!uci_set(ctx, &disabled_ptr));
    return 0;
}

static int _reload_wireless_config(void) {
    int status = 0;

    /* avoid spurious state updates */
    nakd_netintf_disable_updates();

    nakd_log(L_INFO, "Restarting WLAN.");
    char *output;
    if (nakd_do_command(NAKD_SCRIPT_PATH, &output, WLAN_UPDATE_SCRIPT)) {
        nakd_log(L_CRIT, "Error while running " WLAN_UPDATE_SCRIPT);
        status = 1;
        goto unlock;
    }

    nakd_log(L_DEBUG, WLAN_UPDATE_SCRIPT " output: %s", output);
    free(output);

unlock:
    nakd_netintf_enable_updates();
    return status;
}

static void __swap_current_network(json_object *jnetwork) {
    if (_current_network != NULL)
        json_object_put(_current_network);
    if (jnetwork != NULL)
        json_object_get(jnetwork);
    _current_network = jnetwork;
}

json_object *nakd_wlan_current(void) {
    pthread_mutex_lock(&_wlan_mutex);
    if (_current_network != NULL)
        json_object_get(_current_network);
    json_object *jnetwork = _current_network;
    pthread_mutex_unlock(&_wlan_mutex);
    return jnetwork;
}

static int _wlan_connect(json_object *jnetwork) {
    const char *ssid = nakd_net_ssid(jnetwork);
    const char *key = nakd_net_key(jnetwork);
    if (ssid == NULL || key == NULL)
        return 1;

    /* TODO revisit.
     * Probably an OpenWRT bug: dirty configuration prevents hostapd from
     * starting if interfaces share the same phy.
     */
    int in_range = __in_range(ssid);
    if (!in_range) {
        nakd_log(L_NOTICE, "Network \"%s\" is not in range.", ssid);
        return 1;
    } else if (in_range == -1) {
        nakd_log(L_NOTICE, "Please scan before connecting!");
        return 1;
    }

    nakd_log(L_INFO, "Connecting to \"%s\" wireless network.", ssid);
    nakd_log(L_INFO, "Updating WLAN configuration.");

    /* Continue if exactly one UCI section was found and updated. */
    if (nakd_update_iface_config(NAKD_WLAN, _update_wlan_config_ssid,
                                                    jnetwork) != 1) {
        return 1;
    }

    __swap_current_network(jnetwork);
    return _reload_wireless_config();
}

int nakd_wlan_connect(json_object *jnetwork) {
    pthread_mutex_lock(&_wlan_mutex);
    int status = _wlan_connect(jnetwork);
    pthread_mutex_unlock(&_wlan_mutex);
    return status;
}

int nakd_wlan_disconnect(void) {
    int status = 0;

    nakd_log(L_INFO, "Disabling WLAN.");
    pthread_mutex_lock(&_wlan_mutex);

    if (nakd_disable_interface(NAKD_WLAN)) {
        status = 1;
        goto unlock;
    }

    __swap_current_network(NULL);
    status = _reload_wireless_config();

unlock:
    pthread_mutex_unlock(&_wlan_mutex);
    return status;
}

static int _wlan_init(void) {
    pthread_mutex_init(&_wlan_mutex, NULL);
    if ((_wlan_interface_name = nakd_interface_name(NAKD_WLAN)) == NULL) {
        nakd_log(L_WARNING, "Couldn't get %s interface name from UCI, "
                     "continuing with default " WLAN_DEFAULT_INTERFACE,
                                       nakd_interface_type[NAKD_WLAN]);
        _wlan_interface_name = WLAN_DEFAULT_INTERFACE;
    }

    if ((_ap_interface_name = nakd_interface_name(NAKD_AP)) == NULL) {
        nakd_log(L_WARNING, "Couldn't get %s interface name from UCI, "
                     "continuing with default " WLAN_DEFAULT_INTERFACE,
                                         nakd_interface_type[NAKD_AP]);
        _wlan_interface_name = WLAN_AP_DEFAULT_INTERFACE;
    }

    __init_stored_networks();

    /* An out-of-range wireless network can cause erratic AP interface
     * operation if both interfaces are one the same chip, as in ar71xx case.
     *
     * This may be an OpenWRT or hardware issue.
     */
    nakd_wlan_disconnect();

    return 0;
}

static int _wlan_cleanup(void) {
    __cleanup_stored_networks();
    pthread_mutex_destroy(&_wlan_mutex);
    return 0;
}

json_object *cmd_wlan_list(json_object *jcmd, void *arg) {
    json_object *jresponse;

    pthread_mutex_lock(&_wlan_mutex);
    if (_wireless_networks == NULL) {
        jresponse = nakd_jsonrpc_response_error(jcmd, INTERNAL_ERROR,
                          "Internal error - please try again later");
        goto unlock;
    }

    jresponse = nakd_jsonrpc_response_success(jcmd,
           nakd_json_deepcopy(_wireless_networks));

unlock:
    pthread_mutex_unlock(&_wlan_mutex);
    return jresponse;
}

json_object *cmd_wlan_list_stored(json_object *jcmd, void *arg) {
    json_object *jresponse;

    pthread_mutex_lock(&_wlan_mutex);
    jresponse = nakd_jsonrpc_response_success(jcmd,
             nakd_json_deepcopy(_stored_networks));

unlock:
    pthread_mutex_unlock(&_wlan_mutex);
    return jresponse;
}

json_object *cmd_wlan_scan(json_object *jcmd, void *arg) {
    json_object *jresponse;

    if (nakd_wlan_scan()) {
        jresponse = nakd_jsonrpc_response_error(jcmd, INTERNAL_ERROR,
           "Internal error - couldn't update wireless network list");
        return jresponse;
    }

    int netcount = nakd_wlan_netcount();

    json_object *jresult = json_object_new_object();
    json_object *jnetcount = json_object_new_int(netcount);
    json_object *jlastscan = json_object_new_int(_last_scan);
    json_object_object_add(jresult, "netcount", jnetcount);
    json_object_object_add(jresult, "last_scan", jlastscan);

    return nakd_jsonrpc_response_success(jcmd, jresult);
}

json_object *cmd_wlan_connect(json_object *jcmd, void *arg) {
    json_object *jresponse;
    json_object *jparams;

    pthread_mutex_lock(&_wlan_mutex);
    if (_wireless_networks == NULL) {
        jresponse = nakd_jsonrpc_response_error(jcmd, INTERNAL_ERROR,
                          "Internal error - please try again later");
        goto unlock;
    }

    if ((jparams = nakd_jsonrpc_params(jcmd)) == NULL ||
        json_object_get_type(jparams) != json_type_object) {
        goto params;
    }

    const char *ssid = nakd_net_ssid(jparams);
    const char *key = nakd_net_key(jparams);
    if (ssid == NULL || key == NULL)
        goto params;

    if (_wlan_connect(jparams)) {
        jresponse = nakd_jsonrpc_response_error(jcmd, INTERNAL_ERROR,
                 "Internal error - couldn't connect to the network");
        goto unlock;
    }

    json_object *jstore = NULL;
    json_object_object_get_ex(jparams, "store", &jstore);
    if (jstore != NULL) {
       if (json_object_get_type(jstore) != json_type_boolean) {
            jresponse = nakd_jsonrpc_response_error(jcmd, INTERNAL_ERROR,
                     "Internal error - couldn't connect to the network");
            goto unlock;
       }

       int store = json_object_get_boolean(jstore); 
       if (store) {
           if (__store_network(jparams, key)) {
                jresponse = nakd_jsonrpc_response_error(jcmd, INTERNAL_ERROR,
                     "Internal error - couldn't store network credentials.");
                goto unlock;
            }
       }
    }

    json_object *jresult = json_object_new_string("OK");
    jresponse = nakd_jsonrpc_response_success(jcmd, jresult);
    goto unlock;

params:
    jresponse = nakd_jsonrpc_response_error(jcmd, INVALID_PARAMS,
                "Invalid parameters - params should be an object"
                           " with \"ssid\" and \"key\" members");
unlock:
    pthread_mutex_unlock(&_wlan_mutex);
    return jresponse;
}

static struct nakd_module module_wlan = {
    .name = "wlan",
    .deps = (const char *[]){ "uci", "ubus", "netintf", NULL },
    .init = _wlan_init,
    .cleanup = _wlan_cleanup 
};

NAKD_DECLARE_MODULE(module_wlan);
