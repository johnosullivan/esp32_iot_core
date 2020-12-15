#include <stdio.h>

#include "core/core.h"

extern "C" { 
    void app_main(); 
}

void app_main(void)
{   
    /* starts the IOT core */
    core::start_core();
}
