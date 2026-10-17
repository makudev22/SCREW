#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    const char *base = strrchr(argv[0], '\\');
    printf("Hello from Windows code, %d args, program %s\n", argc, base ? base + 1 : argv[0]);
    for (int i = 1; i < argc; i++)
        printf("  arg %d: %s\n", i, argv[i]);
    return 42;
}
