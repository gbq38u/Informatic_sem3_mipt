#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define SHM_KEY_BASE 0x4D490000
#define MAX_USERS 16
#define QUEUE_SIZE 32
#define NAME_SIZE 32
#define TEXT_SIZE 256
#define CHAT_MAGIC 0x43484154u
#define CHAT_VERSION 1u
#define DELAY_NS (100L * 1000L * 1000L)

typedef struct {
    char sender[NAME_SIZE];
    char text[TEXT_SIZE];
    time_t sent_at;
} Message;

typedef struct {
    int active;
    pid_t owner_pid;
    char name[NAME_SIZE];
    unsigned int head;
    unsigned int count;
    Message messages[QUEUE_SIZE];
} UserSlot;

typedef struct {
    uint32_t magic;
    uint32_t version;
    UserSlot users[MAX_USERS];
} SharedChat;

union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

static volatile sig_atomic_t stop_requested = 0;

// обработчик только меняет флаг потому что printf и работа с памятью внутри сигнала ненадёжны
static void stop_handler(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

// type = -1 блокирует общий ресурс type = 1 освобождает
static int change_semaphore(int sem_id, short type)
{
    struct sembuf operation;

    operation.sem_num = 0;
    operation.sem_op = type;
    // SEM_UNDO освобождает блокировку если процесс аварийно завершился
    operation.sem_flg = SEM_UNDO;

    // sem_id задаёт набор operation задаёт операцию 1 задаёт число операций
    while (semop(sem_id, &operation, 1) == -1) {
        if (errno != EINTR) {
            return -1;
        }
    }
    return 0;
}

static int lock_chat(int sem_id)
{
    return change_semaphore(sem_id, -1);
}

static int unlock_chat(int sem_id)
{
    return change_semaphore(sem_id, 1);
}

// создаём семафор до открытия памяти чтобы другие процессы не увидели неинициализированный сегмент
static int open_chat(key_t shm_key, int *shm_id, int *sem_id,
                     SharedChat **chat)
{
    key_t sem_key = (key_t)(shm_key ^ 0x0053454D);
    int semaphore_created = 0;
    union semun argument;

    // sem_key задаёт ключ 1 задаёт один семафор флаги создают его с правами владельца
    *sem_id = semget(sem_key, 1, IPC_CREAT | IPC_EXCL | 0600);
    if (*sem_id >= 0) {
        semaphore_created = 1;
        argument.val = 0;
        // sem_id задаёт набор 0 задаёт первый семафор SETVAL задаёт значение argument
        if (semctl(*sem_id, 0, SETVAL, argument) == -1) {
            return -1;
        }
    } else if (errno == EEXIST) {
        *sem_id = semget(sem_key, 1, 0600);
        if (*sem_id == -1) {
            return -1;
        }
    } else {
        return -1;
    }

    // shm_key задаёт ключ sizeof задаёт размер флаги создают сегмент с правами владельца
    *shm_id = shmget(shm_key, sizeof(SharedChat), IPC_CREAT | 0600);
    if (*shm_id == -1) {
        if (semaphore_created) {
            semctl(*sem_id, 0, IPC_RMID);
        }
        return -1;
    }

    // shm_id задаёт сегмент NULL просит систему выбрать адрес 0 разрешает чтение и запись
    *chat = (SharedChat *)shmat(*shm_id, NULL, 0);
    if (*chat == (void *)-1) {
        *chat = NULL;
        if (semaphore_created) {
            semctl(*sem_id, 0, IPC_RMID);
        }
        return -1;
    }

    if (semaphore_created) {
        // новый семафор означает что старую память нельзя считать согласованной
        memset(*chat, 0, sizeof(**chat));
        (*chat)->magic = CHAT_MAGIC;
        (*chat)->version = CHAT_VERSION;

        argument.val = 1;
        if (semctl(*sem_id, 0, SETVAL, argument) == -1) {
            shmdt(*chat);
            *chat = NULL;
            semctl(*sem_id, 0, IPC_RMID);
            return -1;
        }
    } else {
        if (lock_chat(*sem_id) == -1) {
            shmdt(*chat);
            *chat = NULL;
            return -1;
        }

        if ((*chat)->magic != CHAT_MAGIC ||
            (*chat)->version != CHAT_VERSION) {
            unlock_chat(*sem_id);
            shmdt(*chat);
            *chat = NULL;
            errno = EPROTO;
            return -1;
        }

        if (unlock_chat(*sem_id) == -1) {
            shmdt(*chat);
            *chat = NULL;
            return -1;
        }
    }

    return 0;
}

// освобождаем места пользователей чьи процессы уже завершились
static void remove_dead_users(SharedChat *chat)
{
    int i;

    for (i = 0; i < MAX_USERS; ++i) {
        // pid задаёт проверяемый процесс сигнал 0 только проверяет его существование
        if (chat->users[i].active &&
            kill(chat->users[i].owner_pid, 0) == -1 &&
            errno == ESRCH) {
            memset(&chat->users[i], 0, sizeof(chat->users[i]));
        }
    }
}

static int register_user(SharedChat *chat, int sem_id,
                         const char *name, pid_t owner_pid)
{
    int i;
    int free_index = -1;

    if (lock_chat(sem_id) == -1) {
        return -1;
    }

    remove_dead_users(chat);

    for (i = 0; i < MAX_USERS; ++i) {
        if (chat->users[i].active &&
            strcmp(chat->users[i].name, name) == 0) {
            unlock_chat(sem_id);
            errno = EEXIST;
            return -1;
        }
        if (!chat->users[i].active && free_index == -1) {
            free_index = i;
        }
    }

    if (free_index == -1) {
        unlock_chat(sem_id);
        errno = ENOSPC;
        return -1;
    }

    memset(&chat->users[free_index], 0, sizeof(chat->users[free_index]));
    chat->users[free_index].active = 1;
    chat->users[free_index].owner_pid = owner_pid;
    snprintf(chat->users[free_index].name,
             sizeof(chat->users[free_index].name), "%s", name);

    if (unlock_chat(sem_id) == -1) {
        return -1;
    }
    return 0;
}

static void unregister_user(SharedChat *chat, int sem_id,
                            const char *name, pid_t owner_pid)
{
    int i;

    if (lock_chat(sem_id) == -1) {
        return;
    }

    for (i = 0; i < MAX_USERS; ++i) {
        if (chat->users[i].active &&
            chat->users[i].owner_pid == owner_pid &&
            strcmp(chat->users[i].name, name) == 0) {
            memset(&chat->users[i], 0, sizeof(chat->users[i]));
            break;
        }
    }

    unlock_chat(sem_id);
}

// возвращает 0 при успехе 1 если получатель не найден 2 если его очередь заполнена
static int send_message(SharedChat *chat, int sem_id,
                        const char *sender, const char *recipient,
                        const char *text)
{
    int i;
    int result = 1;

    if (lock_chat(sem_id) == -1) {
        return -1;
    }

    remove_dead_users(chat);

    for (i = 0; i < MAX_USERS; ++i) {
        UserSlot *user = &chat->users[i];

        if (!user->active || strcmp(user->name, recipient) != 0) {
            continue;
        }

        if (user->count == QUEUE_SIZE) {
            result = 2;
            break;
        }

        {
            unsigned int index = (user->head + user->count) % QUEUE_SIZE;
            Message *message = &user->messages[index];

            memset(message, 0, sizeof(*message));
            snprintf(message->sender, sizeof(message->sender), "%s", sender);
            snprintf(message->text, sizeof(message->text), "%s", text);
            message->sent_at = time(NULL);
            ++user->count;
            result = 0;
        }
        break;
    }

    if (unlock_chat(sem_id) == -1) {
        return -1;
    }
    return result;
}

// копируем сообщение под блокировкой а печатаем уже после её снятия
static int receive_message(SharedChat *chat, int sem_id,
                           const char *name, pid_t owner_pid,
                           Message *message)
{
    int i;
    int result = 0;

    if (lock_chat(sem_id) == -1) {
        return -1;
    }

    for (i = 0; i < MAX_USERS; ++i) {
        UserSlot *user = &chat->users[i];

        if (!user->active || user->owner_pid != owner_pid ||
            strcmp(user->name, name) != 0) {
            continue;
        }

        if (user->count > 0) {
            *message = user->messages[user->head];
            user->head = (user->head + 1) % QUEUE_SIZE;
            --user->count;
            result = 1;
        }
        break;
    }

    if (unlock_chat(sem_id) == -1) {
        return -1;
    }
    return result;
}

static void reader_loop(SharedChat *chat, int sem_id,
                        const char *user, pid_t owner_pid)
{
    struct timespec delay = {0, DELAY_NS};

    for (;;) {
        Message message;
        int received;

        if (stop_requested) {
            return;
        }

        // child process: prints incoming messages
        while ((received = receive_message(chat, sem_id, user,
                                           owner_pid, &message)) > 0) {
            char time_text[20] = "unknown";
            struct tm local_time;

            if (localtime_r(&message.sent_at, &local_time) != NULL) {
                strftime(time_text, sizeof(time_text),
                         "%d.%m.%Y %H:%M:%S", &local_time);
            }

            printf("\n[%s] %s: %s\n%s> ", time_text,
                   message.sender, message.text, user);
            fflush(stdout);
        }

        if (received < 0) {
            perror("receive_message");
            return;
        }

        // ребёнок завершается если родитель больше не существует
        // owner_pid задаёт родителя сигнал 0 проверяет его существование без отправки сигнала
        if (kill(owner_pid, 0) == -1 && errno == ESRCH) {
            return;
        }
        nanosleep(&delay, NULL);
    }
}

static void writer_loop(SharedChat *chat, int sem_id, const char *user)
{
    char input[NAME_SIZE + TEXT_SIZE + 4];

    for (;;) {
        char *colon;
        char *text;
        int result;

        // parent process: scans outgoing messages
        printf("%s> ", user);
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL) {
            break;
        }

        if (strchr(input, '\n') == NULL && !feof(stdin)) {
            int symbol;

            // fgets ограничивает размер а остаток слишком длинной строки удаляем из stdin
            while ((symbol = getchar()) != '\n' && symbol != EOF) {
            }
            printf("message is too long\n");
            continue;
        }

        input[strcspn(input, "\n")] = '\0';

        if (strcmp(input, "/quit") == 0) {
            break;
        }

        colon = strchr(input, ':');
        if (colon == NULL) {
            printf("format: recipient: message\n");
            continue;
        }

        text = colon + 1;
        while (colon > input && colon[-1] == ' ') {
            --colon;
        }
        *colon = '\0';
        while (*text == ' ') {
            ++text;
        }

        if (input[0] == '\0' || strlen(input) >= NAME_SIZE ||
            text[0] == '\0' || strlen(text) >= TEXT_SIZE) {
            printf("invalid recipient or message is too long\n");
            continue;
        }

        result = send_message(chat, sem_id, user, input, text);
        if (result == 1) {
            printf("user %s is not online\n", input);
        } else if (result == 2) {
            // отказ от перезаписи важен для надёжности иначе старое сообщение потеряется молча
            printf("queue for %s is full\n", input);
        } else if (result < 0) {
            perror("send_message");
        }

        if (stop_requested) {
            break;
        }
    }
}

static int read_room_number(void)
{
    char input[64];
    char *end;
    long number;

    if (fgets(input, sizeof(input), stdin) == NULL) {
        return -1;
    }

    errno = 0;
    number = strtol(input, &end, 10);
    while (*end == ' ' || *end == '\t' || *end == '\n') {
        ++end;
    }

    if (errno != 0 || *end != '\0' || number < 0 || number > 65535) {
        return -1;
    }
    return (int)number;
}

int main(int argc, char *argv[])
{
    const char *input_terminal;
    const char *output_terminal;
    int room_number;
    key_t shm_key;
    int shm_id;
    int sem_id;
    SharedChat *chat;
    pid_t owner_pid = getpid();
    pid_t child;
    struct sigaction action;

    if (argc != 2 || argv[1][0] == '\0' ||
        strlen(argv[1]) >= NAME_SIZE || strchr(argv[1], ':') != NULL) {
        fprintf(stderr, "usage: %s user_name\n", argv[0]);
        return EXIT_FAILURE;
    }

    input_terminal = ttyname(fileno(stdin));
    output_terminal = ttyname(fileno(stdout));

    printf(
        "--------------------\n"
        "SHM-Chat 0.2\n"
        "--------------------\n"
        "To send a message type recipient: message and press Enter\n"
        "--------------------\n"
        "stdin: %s\n"
        "stdout: %s\n"
        "--------------------\n"
        "Enter any number from 0 to 65535: ",
        input_terminal != NULL ? input_terminal : "not a terminal",
        output_terminal != NULL ? output_terminal : "not a terminal");
    fflush(stdout);

    room_number = read_room_number();
    if (room_number < 0) {
        fprintf(stderr, "invalid chat number\n");
        return EXIT_FAILURE;
    }

    shm_key = (key_t)(SHM_KEY_BASE + room_number);

    if (open_chat(shm_key, &shm_id, &sem_id, &chat) == -1) {
        // проверка каждого системного вызова важна потому что ipc объекты могут остаться после сбоя
        perror("open_chat");
        return EXIT_FAILURE;
    }

    if (register_user(chat, sem_id, argv[1], owner_pid) == -1) {
        if (errno == EEXIST) {
            fprintf(stderr, "user %s is already online\n", argv[1]);
        } else if (errno == ENOSPC) {
            fprintf(stderr, "chat supports no more than %d users\n",
                    MAX_USERS);
        } else {
            perror("register_user");
        }
        shmdt(chat);
        return EXIT_FAILURE;
    }

    memset(&action, 0, sizeof(action));
    action.sa_handler = stop_handler;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);

    // fork() создаёт копию процесса child == 0 это ребёнок читатель child > 0 это родитель писатель
    child = fork();
    if (child < 0) {
        perror("fork");
        unregister_user(chat, sem_id, argv[1], owner_pid);
        shmdt(chat);
        return EXIT_FAILURE;
    }

    if (child == 0) {
        reader_loop(chat, sem_id, argv[1], owner_pid);
        shmdt(chat);
        return EXIT_SUCCESS;
    }

    printf("--------------------\n\n");
    printf("send: recipient: message\nexit: /quit\n");
    writer_loop(chat, sem_id, argv[1]);

    // после quit останавливаем читателя
    // child задаёт процесс SIGTERM задаёт сигнал завершения
    kill(child, SIGTERM);
    // child задаёт ожидаемый процесс NULL игнорирует статус 0 задаёт обычное ожидание
    waitpid(child, NULL, 0);
    unregister_user(chat, sem_id, argv[1], owner_pid);

    if (shmdt(chat) == -1) {
        perror("shmdt");
        return EXIT_FAILURE;
    }

    // сегмент и семафор не удаляются чтобы другие терминалы продолжали работать
    // после аварийного завершения их можно посмотреть через ipcs и удалить через ipcrm
    return EXIT_SUCCESS;
}
