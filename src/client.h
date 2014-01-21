#pragma once

int client_write_buffer_and_disconnect(int client_fd, client_p client, server_p server);
int client_read_incomming_stream(int client_fd, client_p client, server_p server);
int client_read_http_headers(int client_fd, client_p client, server_p server);