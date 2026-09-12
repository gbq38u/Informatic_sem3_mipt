/* Программа, иллюстрирующая использование системных вызовов open(), read() и close() для чтения информации из файла */

#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    int fd;
    size_t size;
    char string[60];

    /* Попытаемся открыть файл с именем в первом параметре выззова только
    для операций чтения */
    if ((fd = open(argv[1], O_RDONLY)) < 0)
    {
        /* Если файл открыть не удалось, печатаем об этом сообщение и прекращаем работу */
        printf("Can't open file\n");
        exit(-1);
    }

    /* Читаем файл, пока не кончится, и печатаем */
    while ((size = read(fd, string, sizeof(string) - 1)) > 0)
    {
        string[size] = '\0';
        printf("%s\n", string);
    }

    /* Закрываем файл */
    if (close(fd) < 0)
    {
        printf("Can't close file\n");
    }

    return 0;
} 
