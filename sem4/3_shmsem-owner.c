#include <stdio.h>
#include <string.h>
#include <sys/shm.h>
#include <sys/sem.h>

#define SHMEM_SIZE 4096
#define SH_MESSAGE "Hello World!\n"

#define SEM_KEY 2007
#define SHM_KEY 2007

// дополнительный аргумент для управления семафором
union semnum
{
    int val;
    struct semid_ds *buf;
    unsigned short *array;
} sem_arg;

int main(void)
{
    // идентификаторы разделяемой памяти и семафора
    int shm_id, sem_id;

    // указатель на разделяемую память
    char *shm_buf;

    // размер разделяемой памяти
    size_t shm_size;

    // информация о разделяемой памяти
    struct shmid_ds ds;

    // операция над семафором
    struct sembuf sb[1];

    // массив начальных значений семафоров
    unsigned short sem_vals[1];

    // SHM_KEY ключ памяти
    // SHMEM_SIZE размер памяти
    // IPC_CREAT создаёт память
    // IPC_EXCL требует создать новый сегмент
    // 0600 задаёт права владельца
    shm_id = shmget(
        SHM_KEY,
        SHMEM_SIZE,
        IPC_CREAT | IPC_EXCL | 0600
    );

    if (shm_id == -1)
    {
        fprintf(stderr, "shmget() error\n");
        return 1;
    }

    // SEM_KEY ключ семафора
    // 1 количество семафоров
    // 0600 задаёт права владельца
    // IPC_CREAT создаёт набор
    // IPC_EXCL требует создать новый набор
    sem_id = semget(
        SEM_KEY,
        1,
        0600 | IPC_CREAT | IPC_EXCL
    );

    if (sem_id == -1)
    {
        fprintf(stderr, "semget() error\n");
        return 1;
    }

    printf("Semaphore: %d\n", sem_id);

    // начальное значение семафора равно 1
    sem_vals[0] = 1;

    // передаём массив значений в semctl
    sem_arg.array = sem_vals;

    // sem_id идентификатор набора
    // 0 номер первого семафора
    // SETALL устанавливает значения семафоров
    // sem_arg содержит массив значений
    if (semctl(sem_id, 0, SETALL, sem_arg) == -1)
    {
        fprintf(stderr, "semctl() error\n");
        return 1;
    }

    // shm_id идентификатор памяти
    // NULL позволяет системе выбрать адрес
    // 0 подключает память для чтения и записи
    shm_buf = (char *)shmat(shm_id, NULL, 0);

    if (shm_buf == (char *)-1)
    {
        fprintf(stderr, "shmat() error\n");
        return 1;
    }

    // shm_id идентификатор памяти
    // IPC_STAT получает информацию о памяти
    // ds структура для сохранения информации
    if (shmctl(shm_id, IPC_STAT, &ds) == -1)
    {
        fprintf(stderr, "shmctl() error\n");
        return 1;
    }

    // получаем размер разделяемой памяти
    shm_size = ds.shm_segsz;

    // добавляем один байт для символа конца строки
    if (shm_size < strlen(SH_MESSAGE) + 1)
    {
        fprintf(stderr, "error: segsize=%zu\n", shm_size);
        return 1;
    }

    // shm_buf адрес назначения
    // SH_MESSAGE исходная строка
    strcpy(shm_buf, SH_MESSAGE);

    printf("ID: %d\n", shm_id);

    // выбираем первый семафор
    sb[0].sem_num = 0;

    // система отменит операцию при аварийном завершении процесса
    sb[0].sem_flg = SEM_UNDO;

    // уменьшаем значение семафора с 1 до 0
    sb[0].sem_op = -1;

    // sem_id идентификатор набора
    // sb массив операций
    // 1 количество операций
    if (semop(sem_id, sb, 1) == -1)
    {
        fprintf(stderr, "first semop() error\n");
        return 1;
    }

    // пытаемся ещё раз уменьшить семафор
    // при значении 0 программа будет ждать вторую программу
    sb[0].sem_op = -1;

    if (semop(sem_id, sb, 1) == -1)
    {
        fprintf(stderr, "second semop() error\n");
        return 1;
    }

    // удаляем набор семафоров
    // единственный семафор имеет номер 0
    semctl(sem_id, 0, IPC_RMID, sem_arg);

    // отключаем разделяемую память
    shmdt(shm_buf);

    // удаляем разделяемую память
    shmctl(shm_id, IPC_RMID, NULL);

    return 0;
}