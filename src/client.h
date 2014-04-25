#pragma once

#include "common.h"

#define CLIENT_CON_READABLE (1 << 0)
#define CLIENT_CON_WRITABLE (1 << 1)
#define CLIENT_CON_CLEANUP  (1 << 2)

int client_handlers_init();
int client_handler(int client_fd, client_p client, server_p server, int flags);