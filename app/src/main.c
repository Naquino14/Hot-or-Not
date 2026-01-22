#include <stdio.h>
#include <zephyr/kernel.h>

int main(void) {
    k_msleep(500);
    printf("Hello, Carlson!\n");
    return 0;
}
