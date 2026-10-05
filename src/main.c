/* main.c — WiChat entry point */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"

int main(int argc, char **argv) {
    printf("WiChat v%s\n", WICHAT_VERSION);
    printf("Channel: %d\n", WICHAT_DEFAULT_CH);
    return 0;
}
