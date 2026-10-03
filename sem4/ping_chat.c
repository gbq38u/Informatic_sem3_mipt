#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

// ключи разделяемой памяти и семафоров
#define SHM_KEY 8007
#define SEM_KEY 8008
#define MAGIC 0x50494E47
#define NAME_SIZE 32

struct shared_data
{
    // проверка формата общей памяти
    int magic;

    // общее число и состояние карусели
    int number;
    int ready;
    int quit;

    // ожидаемый и текущий владелец хода
    int turn;
    int holder;

    // состояние двух процессов
    int active[2];
    int paused[2];
    pid_t pid[2];
    char name[2][NAME_SIZE];
};

// аргумент для semctl
union semnum
{
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

// проверка процесса
static int process_alive(pid_t pid)
{
    if (pid <= 0)
    {
        return 0;
    }

    if (kill(pid, 0) == 0)
    {
        return 1;
    }

    return errno != ESRCH;
}

// операция с семафором
static int sem_change(int sem_id, int number, int operation)
{
    struct sembuf action;

    action.sem_num = (unsigned short)number;
    // минус один ожидает ход плюс один передаёт ход
    action.sem_op = (short)operation;
    action.sem_flg = 0;

    while (semop(sem_id, &action, 1) == -1)
    {
        if (errno != EINTR)
        {
            return -1;
        }
    }

    return 0;
}

// значения семафоров
static int get_values(int sem_id, unsigned short values[2])
{
    union semnum argument;

    argument.array = values;
    return semctl(sem_id, 0, GETALL, argument);
}

// передача хода
static int set_turn(int sem_id, int slot)
{
    union semnum argument;
    unsigned short values[2];

    // единица разрешает работу выбранному процессу
    values[0] = slot == 0 ? 1 : 0;
    values[1] = slot == 1 ? 1 : 0;
    argument.array = values;

    return semctl(sem_id, 0, SETALL, argument);
}

// подключение общих объектов
static int attach_objects(int *shm_id, int *sem_id,
                          struct shared_data **data)
{
    int attempt;

    // открываем уже созданную память
    *shm_id = shmget(SHM_KEY, sizeof(struct shared_data), 0600);
    if (*shm_id == -1)
    {
        return -1;
    }

    // подключаем память к процессу
    *data = (struct shared_data *)shmat(*shm_id, NULL, 0);
    if (*data == (void *)-1)
    {
        *data = NULL;
        return -1;
    }

    // ожидание инициализации
    for (attempt = 0; attempt < 50 && (*data)->magic != MAGIC; ++attempt)
    {
        struct timespec delay = {0, 100L * 1000L * 1000L};
        nanosleep(&delay, NULL);
    }

    if ((*data)->magic != MAGIC)
    {
        shmdt(*data);
        *data = NULL;
        errno = EPROTO;
        return -1;
    }

    *sem_id = semget(SEM_KEY, 2, 0600);
    if (*sem_id == -1)
    {
        shmdt(*data);
        *data = NULL;
        return -1;
    }

    return 0;
}

// создание общих объектов
static int open_objects(int *shm_id, int *sem_id,
                        struct shared_data **data)
{
    // первый процесс пытается создать новую память
    *shm_id = shmget(SHM_KEY, sizeof(struct shared_data),
                     IPC_CREAT | IPC_EXCL | 0600);

    if (*shm_id >= 0)
    {
        union semnum argument;
        // оба процесса сначала ожидают ввода числа
        unsigned short values[2] = {0, 0};

        *data = (struct shared_data *)shmat(*shm_id, NULL, 0);
        if (*data == (void *)-1)
        {
            *data = NULL;
            shmctl(*shm_id, IPC_RMID, NULL);
            return -1;
        }

        *sem_id = semget(SEM_KEY, 2, IPC_CREAT | IPC_EXCL | 0600);
        if (*sem_id == -1)
        {
            shmdt(*data);
            shmctl(*shm_id, IPC_RMID, NULL);
            return -1;
        }

        argument.array = values;
        if (semctl(*sem_id, 0, SETALL, argument) == -1)
        {
            semctl(*sem_id, 0, IPC_RMID, argument);
            shmdt(*data);
            shmctl(*shm_id, IPC_RMID, NULL);
            return -1;
        }

        memset(*data, 0, sizeof(**data));
        (*data)->holder = -1;
        (*data)->magic = MAGIC;
        return 0;
    }

    if (errno != EEXIST)
    {
        return -1;
    }

    // второй процесс подключается к готовым объектам
    return attach_objects(shm_id, sem_id, data);
}

// очистка завершившихся процессов
static void clear_dead(struct shared_data *data)
{
    int i;

    for (i = 0; i < 2; ++i)
    {
        if (data->active[i] && !process_alive(data->pid[i]))
        {
            data->active[i] = 0;
            data->paused[i] = 0;

            if (data->holder == i)
            {
                data->holder = -1;
            }
        }
    }
}

// регистрация процесса
static int register_process(int sem_id, struct shared_data *data,
                            const char *name)
{
    unsigned short values[2];
    int slot;

    clear_dead(data);

    if (!data->active[0] && !data->active[1])
    {
        // первый процесс начинает новую карусель
        memset(data->active, 0, sizeof(data->active));
        memset(data->paused, 0, sizeof(data->paused));
        data->number = 0;
        data->ready = 0;
        data->quit = 0;
        data->turn = 0;
        data->holder = -1;
        set_turn(sem_id, -1);
        slot = 0;
    }
    else if (!data->active[0])
    {
        // занимаем освободившееся место первого процесса
        slot = 0;
    }
    else if (!data->active[1])
    {
        // занимаем место второго процесса
        slot = 1;
    }
    else
    {
        errno = ENOSPC;
        return -1;
    }

    data->active[slot] = 1;
    data->paused[slot] = 0;
    data->pid[slot] = getpid();
    snprintf(data->name[slot], sizeof(data->name[slot]), "%s", name);

    // восстановление потерянного хода
    if (data->ready && get_values(sem_id, values) == 0 &&
        values[0] == 0 && values[1] == 0 && data->holder == -1)
    {
        set_turn(sem_id, data->turn);
    }

    return slot;
}

// ожидание второго процесса
static void wait_for_second(struct shared_data *data, int slot)
{
    int other = 1 - slot;

    printf("waiting for the second terminal\n");

    while (!data->active[other] || !process_alive(data->pid[other]))
    {
        sleep(1);
        clear_dead(data);
    }
}

// вывод состояния
static void print_status(int sem_id, const struct shared_data *data)
{
    unsigned short values[2];
    int i;

    if (get_values(sem_id, values) == -1)
    {
        perror("semctl");
        return;
    }

    printf("semaphore 0: %u\n", values[0]);
    printf("semaphore 1: %u\n", values[1]);
    printf("number: %d\n", data->number);
    printf("quit: %d\n", data->quit);
    printf("turn: %d\n", data->turn);
    printf("holder: %d\n", data->holder);

    for (i = 0; i < 2; ++i)
    {
        if (!data->active[i])
        {
            printf("terminal %d: free\n", i);
        }
        else
        {
            printf("terminal %d: %s pid=%d %s\n",
                   i, data->name[i], (int)data->pid[i],
                   !process_alive(data->pid[i]) ? "dead" :
                   data->paused[i] ? "paused" : "alive");
        }
    }

    if (values[0] == 0 && values[1] == 0)
    {
        if (data->holder >= 0)
        {
            printf("state: terminal %s holds the turn\n",
                   data->name[data->holder]);
        }
        else
        {
            printf("state: turn is lost or number is not entered\n");
        }
    }
    else
    {
        printf("state: turn belongs to terminal %d\n",
               values[0] == 1 ? 0 : 1);
    }
}

// ввод нового числа
static int read_number(int *number)
{
    char input[64];

    for (;;)
    {
        char *end;
        long value;

        printf("enter number or quit: ");
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL)
        {
            return 0;
        }

        input[strcspn(input, "\n")] = '\0';

        if (strcmp(input, "quit") == 0)
        {
            return 0;
        }

        errno = 0;
        value = strtol(input, &end, 10);

        if (errno == 0 && *end == '\0' &&
            value > 0 && value <= INT_MAX)
        {
            *number = (int)value;
            return 1;
        }

        printf("invalid number\n");
    }
}

// основной режим
static int run_process(const char *name)
{
    int shm_id;
    int sem_id;
    int slot;
    int other;
    struct shared_data *data;

    if (open_objects(&shm_id, &sem_id, &data) == -1)
    {
        perror("open_objects");
        return 1;
    }

    slot = register_process(sem_id, data, name);
    if (slot == -1)
    {
        perror("register_process");
        shmdt(data);
        return 1;
    }

    other = 1 - slot;
    printf("name: %s\nterminal: %d\n", name, slot);
    wait_for_second(data, slot);

    if (slot == 0 && !data->ready)
    {
        // число всегда вводит первый процесс
        if (!read_number(&data->number))
        {
            data->quit = 1;
            data->turn = other;
            set_turn(sem_id, other);
        }
        else
        {
            data->ready = 1;
            data->turn = 0;
            set_turn(sem_id, 0);
        }
    }
    else
    {
        // второй процесс ожидает первое число
        while (!data->ready && !data->quit)
        {
            sleep(1);
        }
    }

    if (data->quit)
    {
        data->active[slot] = 0;
        data->paused[slot] = 0;
        shmdt(data);
        return 0;
    }

    for (;;)
    {
        int value;

        // ожидание своего хода
        if (sem_change(sem_id, slot, -1) == -1)
        {
            perror("semop");
            break;
        }

        data->holder = slot;

        if (data->quit)
        {
            data->holder = -1;
            break;
        }

        if (data->number <= 0)
        {
            if (slot == 0)
            {
                // после нуля первый процесс запускает новый круг
                if (!read_number(&data->number))
                {
                    data->quit = 1;
                    data->holder = -1;
                    data->turn = other;
                    sem_change(sem_id, other, 1);
                    break;
                }
            }
            else
            {
                // второй процесс возвращает ход для нового ввода
                data->holder = -1;
                data->turn = 0;
                sem_change(sem_id, 0, 1);
                continue;
            }
        }

        // уменьшаем общее число только в свой ход
        value = --data->number;
        printf("%s: %d\n", name, value);
        fflush(stdout);

        // задержка для наглядности
        sleep(1);

        data->holder = -1;
        data->turn = other;

        // передача хода
        if (sem_change(sem_id, other, 1) == -1)
        {
            perror("semop");
            break;
        }
    }

    data->active[slot] = 0;
    data->paused[slot] = 0;
    shmdt(data);
    return 0;
}

// режим состояния
static int status_mode(void)
{
    int shm_id;
    int sem_id;
    struct shared_data *data;

    if (attach_objects(&shm_id, &sem_id, &data) == -1)
    {
        perror("status");
        return 1;
    }

    print_status(sem_id, data);
    shmdt(data);
    return 0;
}

// поиск процесса по имени
static int find_process(const struct shared_data *data, const char *name)
{
    int i;

    for (i = 0; i < 2; ++i)
    {
        if (data->active[i] && strcmp(data->name[i], name) == 0)
        {
            return i;
        }
    }

    return -1;
}

// остановка или продолжение процесса
static int control_mode(const char *name, int signal_number)
{
    int shm_id;
    int sem_id;
    int slot;
    struct shared_data *data;

    if (attach_objects(&shm_id, &sem_id, &data) == -1)
    {
        perror("control");
        return 1;
    }

    slot = find_process(data, name);
    if (slot == -1)
    {
        fprintf(stderr, "process was not found\n");
        shmdt(data);
        return 1;
    }

    data->paused[slot] = signal_number == SIGSTOP;

    if (kill(data->pid[slot], signal_number) == -1)
    {
        perror("kill");
        data->paused[slot] = 0;
        shmdt(data);
        return 1;
    }

    printf("%s was %s\n", name,
           signal_number == SIGSTOP ? "paused" : "resumed");
    print_status(sem_id, data);
    shmdt(data);
    return 0;
}

// восстановление хода
static int repair_mode(const char *name)
{
    int shm_id;
    int sem_id;
    int slot;
    struct shared_data *data;

    if (attach_objects(&shm_id, &sem_id, &data) == -1)
    {
        perror("repair");
        return 1;
    }

    slot = find_process(data, name);
    if (slot == -1)
    {
        fprintf(stderr, "process was not found\n");
        shmdt(data);
        return 1;
    }

    data->holder = -1;
    data->turn = slot;

    if (set_turn(sem_id, slot) == -1)
    {
        perror("repair");
        shmdt(data);
        return 1;
    }

    printf("turn was passed to %s\n", name);
    print_status(sem_id, data);
    shmdt(data);
    return 0;
}

int main(int argc, char *argv[])
{
    // просмотр положения семафоров
    if (argc == 2 && strcmp(argv[1], "--status") == 0)
    {
        return status_mode();
    }

    // искусственная остановка процесса
    if (argc == 3 && strcmp(argv[1], "--hang") == 0)
    {
        return control_mode(argv[2], SIGSTOP);
    }

    // продолжение остановленного процесса
    if (argc == 3 && strcmp(argv[1], "--resume") == 0)
    {
        return control_mode(argv[2], SIGCONT);
    }

    // принудительная передача потерянного хода
    if (argc == 3 && strcmp(argv[1], "--repair") == 0)
    {
        return repair_mode(argv[2]);
    }

    if (argc != 2 || strlen(argv[1]) >= NAME_SIZE)
    {
        fprintf(stderr,
                "usage: %s name\n"
                "       %s --status\n"
                "       %s --hang name\n"
                "       %s --resume name\n"
                "       %s --repair name\n",
                argv[0], argv[0], argv[0], argv[0], argv[0]);
        return 1;
    }

    return run_process(argv[1]);
}
