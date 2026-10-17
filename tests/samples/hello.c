#include <stdio.h>

int main(int argc, char **argv)
{
    printf("Hello from Windows code, %d args, argv[0]=%s\n", argc, argv[0]);
    for (int i = 1; i < argc; i++)
        printf("  arg %d: %s\n", i, argv[i]);
    return 42;
}
