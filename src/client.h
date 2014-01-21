#pragma once

extern client_state_t client_start_state;

void client_change_state(int client_fd, client_p client, server_p server, client_state_p state);