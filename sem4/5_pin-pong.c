/* 
  Создать программу(программы) для последовательного вычетания 1 двумя процессами
  из некоторого числа, записанного в разделяемой памяти.
  Очерёдность работы процессов регулируем при помощи семафора.
  Ход выполнения следующий:
  Открываем два терминала и запускаем в них созданную(ые) программы
  В одном из них вводим некоторое число.
  После чего обе программы начинают выводить по очереди уменьшенное на 1,
  полученное значение.
  Предусмотреть задержки для наглядности.

*/
#include <errno.h>
#include <stdio.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <unistd.h>

#define SHM_KEY 3007
#define SEM_KEY 3008

// данные в разделяемой памяти
struct shared_data
{
    int number;
};

// аргумент для semctl
union semnum
{
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

// выполняем операцию над семафором
static int change_sem(int sem_id, unsigned short sem_number, short operation)
{
    struct sembuf action;

    // номер семафора
    action.sem_num = sem_number;

    // операция -1 ожидание +1 разрешение
    action.sem_op = operation;

    // дополнительные флаги не используются
    action.sem_flg = 0;

    // повторяем вызов если его прервал сигнал
    while (semop(sem_id, &action, 1) == -1)
    {
        if (errno != EINTR)
        {
            return -1;
        }
    }

    return 0;
}

int main(void)
{
    int shm_id;
    int sem_id;
    int first_process = 0;
    int my_sem;
    int other_sem;

    struct shared_data *data;

    union semnum sem_arg;
    unsigned short sem_values[2];

    // пытаемся создать новую разделяемую память
    shm_id = shmget(
        SHM_KEY,
        sizeof(struct shared_data),
        IPC_CREAT | IPC_EXCL | 0600
    );

    if (shm_id >= 0)
    {
        // этот процесс запущен первым
        first_process = 1;
    }
    else if (errno == EEXIST)
    {
        // память уже создана первым процессом
        shm_id = shmget(
            SHM_KEY,
            sizeof(struct shared_data),
            0600
        );
    }

    if (shm_id == -1)
    {
        perror("shmget");
        return 1;
    }

    if (first_process)
    {
        // создаём два семафора
        sem_id = semget(
            SEM_KEY,
            2,
            IPC_CREAT | IPC_EXCL | 0600
        );

        if (sem_id == -1)
        {
            perror("semget");
            shmctl(shm_id, IPC_RMID, NULL);
            return 1;
        }

        // первый процесс может работать сразу
        sem_values[0] = 1;

        // второй процесс сначала ожидает
        sem_values[1] = 0;

        sem_arg.array = sem_values;

        // устанавливаем значения обоих семафоров
        if (semctl(sem_id, 0, SETALL, sem_arg) == -1)
        {
            perror("semctl");
            semctl(sem_id, 0, IPC_RMID, sem_arg);
            shmctl(shm_id, IPC_RMID, NULL);
            return 1;
        }
    }
    else
    {
        // даём первому процессу время создать семафоры
        sleep(1);

        // подключаемся к существующим семафорам
        sem_id = semget(SEM_KEY, 2, 0600);

        if (sem_id == -1)
        {
            perror("semget");
            return 1;
        }
    }

    // подключаем разделяемую память
    data = (struct shared_data *)shmat(shm_id, NULL, 0);

    if (data == (void *)-1)
    {
        perror("shmat");
        return 1;
    }

    if (first_process)
    {
        // первый процесс вводит начальное число
        printf("enter number: ");

        if (scanf("%d", &data->number) != 1 || data->number <= 0)
        {
            printf("invalid number\n");
            shmdt(data);
            semctl(sem_id, 0, IPC_RMID, sem_arg);
            shmctl(shm_id, IPC_RMID, NULL);
            return 1;
        }

        // первый процесс использует семафор 0
        my_sem = 0;
        other_sem = 1;

        printf("process 1 started\n");
    }
    else
    {
        // второй процесс использует семафор 1
        my_sem = 1;
        other_sem = 0;

        printf("process 2 started\n");
    }

    for (;;)
    {
        // ожидаем своей очереди
        if (change_sem(sem_id, my_sem, -1) == -1)
        {
            perror("semop");
            break;
        }

        // если число закончилось передаём очередь и выходим
        if (data->number <= 0)
        {
            change_sem(sem_id, other_sem, 1);
            break;
        }

        // уменьшаем общее число
        data->number--;

        // выводим полученное значение
        printf("process %d: %d\n",
               first_process ? 1 : 2,
               data->number);

        fflush(stdout);

        // задержка для наглядности
        sleep(1);

        // передаём очередь другому процессу
        if (change_sem(sem_id, other_sem, 1) == -1)
        {
            perror("semop");
            break;
        }
    }

    // отключаем разделяемую память
    shmdt(data);

    if (first_process)
    {
        // первый процесс удаляет семафоры и память
        semctl(sem_id, 0, IPC_RMID, sem_arg);
        shmctl(shm_id, IPC_RMID, NULL);
    }

    return 0;
}


