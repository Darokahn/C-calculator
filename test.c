#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define breakable switch (0)
#define start case 0

int main() {
    srand(time(NULL));
    int val1 = rand() % 10;
    int val2 = rand() % 10;
    breakable {
        start:
        if (val1 < 5) {
            printf("broke at first check\n");
            break;
        }
        if (val2 < 5) {
            printf("broke at second check\n");
            break;
        }
        printf("passed both checks\n");
    }
}
