#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <stdio.h>
#include <stdlib.h>


 // программа увеличивает значение семафора на единицу6  после этого ожидающая программа может продолжить работу

int main(int argc, char *argv[], char *envp[])
{

    (void)argc;
    (void)argv;
    (void)envp;

    //идентификатор набора семафоров 
    int semid;

    
    char pathname[] = "1_sem.c";

    //  ключ 
    key_t key;

    // описание операции над семафором 
    struct sembuf mybuf;

    
     // pathname указывает на существующий файл
     // 0 задаёт дополнительный номер проекта
     
    key = ftok(pathname, 0);

    // ftok возвращает -1 при ошибке 
    if (key == (key_t)-1)
    {
        perror("ftok");
        return EXIT_FAILURE;
    }

    
     //key задаёт ключ набора
     //1 задаёт количество семафоров
     // 0666 задаёт права доступа
    // IPC_CREAT создаёт набор если он не существует
     
    semid = semget(key, 1, 0666 | IPC_CREAT);

    // semget возвращает -1 при ошибке 
    if (semid == -1)
    {
        perror("semget");
        return EXIT_FAILURE;
    }

    
     // 0 задаёт первый семафор
     // 1 увеличивает его значение на единицу
     // 0 означает выполнение без дополнительных флагов
     
    mybuf.sem_num = 0;
    mybuf.sem_op = 1;
    mybuf.sem_flg = 0;

    
     // semid задаёт набор семафоров
     // mybuf указывает на операцию
     // 1 задаёт количество операций
     
    if (semop(semid, &mybuf, 1) == -1)
    {
        perror("semop");
        return EXIT_FAILURE;
    }

    printf("The condition is present\n");

    return EXIT_SUCCESS;
}