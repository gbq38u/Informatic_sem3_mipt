/* Пример программы печатающей значения PPID и PID для текущего процесса */

#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
 
int main(void)
{
pid_t pid, ppid;
 
pid = getpid();
ppid = getppid();
 
printf("My pid = %d, my ppid = %d\n", (int)pid, (int)ppid);
return 0;
}
