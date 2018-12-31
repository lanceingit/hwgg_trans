#pragma once

typedef void config_callback(void* param);

bool smartconfig_begin(config_callback* cb);
bool smartconfig_end(void);
