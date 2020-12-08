#include <stdio.h>

#include "core/core.h"

extern "C" { 
    void app_main(); 
}

void app_main(void)
{   
    // start the mihome service
    core::start_core();
}
