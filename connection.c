#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/un.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "connection.h"
#include "message.h"
#include "log.h"

int nakd_handle_connection(int sock) {
    struct sockaddr_un client_addr;
    socklen_t client_len = sizeof(struct sockaddr_un);
    char message_buf[MAX_MSG_LEN];
    int nb_read = 0, nb_sent;
    json_tokener *jtok;
    int nb_parse, parse_offset;
    json_object *jmsg = NULL;
    json_object *jresponse = NULL;
    const char *jrstr;
    int nb_resp;
    enum json_tokener_error jerr;
    int rval = 0;

    jtok = json_tokener_new();

    /* TODO: GRAB CREDENTIALS OF CONNECTING PROCESS */

    for (;;) {
        do {
            /* If there was another JSON string in the middle of the buffer,
             * start with an offset and don't call recvfrom() just yet.
             */
            parse_offset = jtok->char_offset < nb_read ? jtok->char_offset : 0;
            if (!parse_offset) {
                if ((nb_read = recvfrom(sock, message_buf, MAX_MSG_LEN, 0,
                          (struct sockaddr *) &client_addr, &client_len)) < 0) { 
                    nakd_log(L_DEBUG, "recvfrom() < 0, closing connection.");
                    goto ret;
                }
                /* remaining bytes to parse */
                nb_parse = nb_read;
            } else {
                nb_parse = nb_read - jtok->char_offset;
            }

            /* partial JSON strings are stored in tokener context */
            jmsg = json_tokener_parse_ex(jtok, message_buf + parse_offset,
                                                               nb_parse);
        } while ((jerr = json_tokener_get_error(jtok)) == json_tokener_continue);

        if (jerr == json_tokener_success) {
            jresponse = nakd_handle_message(jmsg);
            jrstr = json_object_get_string(jresponse);

            while (nb_resp = strlen(jrstr)) {
                nb_sent = sendto(sock, jrstr, nb_resp, 0,
                        (struct sockaddr *) &client_addr,
                                             client_len);
                if (nb_sent < 0) {
                    nakd_log(L_NOTICE,
                        "Couldn't send response, closing connection.");
                    rval = 1;
                    goto ret;
                }
                jrstr += nb_sent;
            }

            json_object_put(jresponse), jresponse = NULL;
        } else {
            nakd_log(L_NOTICE, "Couldn't parse client JSON message: %s."
                                                 " Closing connection.",
                                         json_tokener_error_desc(jerr));
            rval = 1;
            goto ret;
        }

        json_object_put(jmsg), jmsg = NULL;
    }

ret:
    /* NULL-safe functions */
    json_object_put(jresponse);
    json_object_put(jmsg);
    json_tokener_free(jtok);
    return rval;
}

