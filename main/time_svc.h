#pragma once
#include <stdbool.h>
#include <time.h>

void time_svc_init(void);          
void time_svc_start_tasks(void);   
bool time_svc_get_localtime(struct tm *out); 
