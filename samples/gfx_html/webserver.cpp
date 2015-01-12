// This file is part of BOINC.
// http://boinc.berkeley.edu
// Copyright (C) 2014-2015 University of California
//
// BOINC is free software; you can redistribute it and/or modify it
// under the terms of the GNU Lesser General Public License
// as published by the Free Software Foundation,
// either version 3 of the License, or (at your option) any later version.
//
// BOINC is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with BOINC.  If not, see <http://www.gnu.org/licenses/>.

#ifdef _WIN32
#include "boinc_win.h"
#include "win_util.h"
#else
#include <vector>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <cmath>
#include <string>
#include <unistd.h>
#endif

#include "parse.h"
#include "filesys.h"
#include "boinc_api.h"
#include "app_ipc.h"
#include "browserlog.h"
#include "mongoose.h"
#include "webapi.h"
#include "webserver.h"

#if defined(_MSC_VER)
#define snprintf    _snprintf
#define getcwd      _getcwd
#endif


#define WEBSERVER_STATE_UNINIT  0
#define WEBSERVER_STATE_INIT    1
#define WEBSERVER_STATE_POLLING 2
#define WEBSERVER_STATE_EXITING 3

static struct mg_server* webserver;
static int  webserver_state = WEBSERVER_STATE_UNINIT;
static char webserver_listeningport[64];
static char webserver_documentroot[64];
static char webserver_domain[64];
static bool webserver_debugging = false;


static int ev_handler(struct mg_connection *conn, enum mg_event ev) {
    switch (ev) {
        case MG_REQUEST:
            if        (!strcmp(conn->uri, "/api/getInitData")) { 
                handle_get_init_data(conn); 
                return MG_TRUE; 
            } else if (!strcmp(conn->uri, "/api/getGraphicsStatus")) {
                handle_get_graphics_status(conn); 
                return MG_TRUE; 
            } else if (!strcmp(conn->uri, "/api/logMessage")) {
                handle_log_message(conn); 
                return MG_TRUE; 
            } else if (boinc_file_exists(conn->uri+1)) {
                handle_filesystem_request(conn);
                return MG_MORE;
            }
            return MG_FALSE;
        case MG_AUTH:
            return MG_TRUE;
        default:
            return MG_FALSE;
    }
}


static int webserver_handler() {
    int retval = 1;

    switch(webserver_state) {
        case WEBSERVER_STATE_INIT:
            webserver = mg_create_server(NULL, ev_handler);
            mg_set_option(webserver, "listening_port", webserver_listeningport);
            mg_set_option(webserver, "document_root", webserver_documentroot);
            mg_set_option(webserver, "enable_directory_listing", "no");
            mg_set_option(webserver, "index_files", "");
            mg_set_option(webserver, "auth_domain", webserver_domain);
            webserver_state = WEBSERVER_STATE_POLLING;
            break;
        case WEBSERVER_STATE_POLLING:
            mg_poll_server(webserver, 1000);
            break;
        case WEBSERVER_STATE_EXITING:
            mg_destroy_server(&webserver);
            retval = 0;
        default:
            assert(false);
    }

    return retval;
}

#ifdef _WIN32
DWORD WINAPI webserver_thread(void*) {
    while (webserver_handler()) {}
    return 0;
}
#else
static void* webserver_thread(void*) {
    while (webserver_handler()) {}
    return 0;
}
#endif

int start_webserver_thread() {
#ifdef _WIN32
    if (!CreateThread(NULL, 0, webserver_thread, 0, 0, NULL)) {
        return GetLastError();
    }
#else
    int retval = pthread_create(NULL, NULL, webserver_thread, NULL);
    if (retval) {
        return retval;
    }
#endif
    return 0;
}


int webserver_initialize(int port, bool debugging) {
    if (port <= 1024) return 1;

    snprintf(
        webserver_listeningport, sizeof(webserver_listeningport)-1,
        "127.0.0.1:%d",
        port
    );

    getcwd(webserver_documentroot, sizeof(webserver_documentroot)-1);

    snprintf(
        webserver_domain, sizeof(webserver_domain)-1,
        "htmlgfx"
    );

    webserver_debugging = debugging;
    webserver_state = WEBSERVER_STATE_INIT;
    return start_webserver_thread();
}

int webserver_destroy() {
    webserver_state = WEBSERVER_STATE_EXITING;
    return 0;
}

