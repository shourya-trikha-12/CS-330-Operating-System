#include <stdio.h>
#include <unistd.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

void printError()
{
	printf("Unable to execute\n");
	exit(-1);
}

bool is_Number(char* str1) {
    int n, m;
    
    while (*str1 == ' ') str1 ++;
    n = m = 0;
	if (*str1 == '-') return false;
    if (*str1 == '+') str1 ++;
    while (*str1 >= '0' && *str1 <= '9') {
        n ++;
        str1 ++;
    }
	if(*str1 == '.' || *str1 == 'e' || *str1 == 'E' || !n) return false;
	return true;
}

int main(int argc, char *argv[])
{

	if (argc == 2)
	{
		if(!is_Number(argv[argc-1])){
			printError();
		}
		long d = strtol(argv[argc-1], 0, 10);
		printf("%ld\n", 2 * d);
	}
	else
	{
		if(!is_Number(argv[argc-1])){
			printError();
		}
		long d = strtol(argv[argc - 1], 0, 10);
		long x = 2 * d;
		char *path = (char *)malloc(50 * sizeof(char));
		path[0] = '.';
		path[1] = '/';
		strcat(path, argv[1]);
		char *result_str = (char *)malloc(50 * sizeof(char));
		sprintf(result_str, "%ld", x);
		argv[argc - 1] = result_str;
		char **args = (char **)malloc((argc) * sizeof(char *));
		for (int i = 0; i < argc - 1; i++)
		{
			int size = strlen(argv[i + 1]) + 1;
			args[i] = (char *)malloc(size);
			strcpy(args[i], argv[i + 1]);
		}
		if (execv(path, args) == -1)
		{
			printError();
		}
	}
	return 0;
}
