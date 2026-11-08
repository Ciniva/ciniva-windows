#pragma once

namespace ciniva::timing {

void init();
double since_process_start_ms();
void mark(const char* what);
void log(const char* fmt, ...);

}
