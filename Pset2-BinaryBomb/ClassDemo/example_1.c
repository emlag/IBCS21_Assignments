#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// example 1  print string

// example 2 call function
int add_nums(int x, int y)
{
    int answer = x + y;
    return answer;
}

// example 3 call function with loop in it

int main(int argc, char const *argv[])
{/////////////////////////////
    int ans = add_nums(6, 10);
    printf("The number is: %d \n", ans);
    return 0;
}

