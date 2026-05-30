#pragma once
#include "config.h"
#include "data.h"

void http_client_init(const char* mac_ip, const char* mac_port);
void http_client_set_server(const char* mac_ip, const char* mac_port);
void http_client_tick(void);
bool http_client_fetch_usage(UsageData* out);
bool http_client_is_connected(void);
void http_client_request_refresh(void);
