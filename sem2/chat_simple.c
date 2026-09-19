#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>


#define FILE_NAME "chat_messages.dat"
#define NAME_SIZE 32
#define TEXT_SIZE 256


typedef struct {
    char sender[NAME_SIZE];
    char recipient[NAME_SIZE];
    char text[TEXT_SIZE];
    time_t sent_at; 
} Message;

// type = F_WRLCK блокирует файл, type = F_UNLCK освобождает
static int set_lock(int fd, short type)
{
    struct flock lock;

    memset(&lock, 0, sizeof(lock));
    lock.l_type = type;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0; // весь файл 

    return fcntl(fd, type == F_UNLCK ? F_SETLK : F_SETLKW, &lock);
}

// проверяем, что файл состоит из целого числа структур Message

static int check_message_file(void)
{
    int fd = open(FILE_NAME, O_RDONLY | O_CREAT, 0600);
    struct stat information;

    if (fd < 0) {
        return -1;
    }
    if (fstat(fd, &information) < 0) {
        close(fd);
        return -1;
    }
    close(fd);

    if (information.st_size % (off_t)sizeof(Message) != 0) {
        fprintf(stderr,
                "%s has an old or damaged format; rename or remove it\n",
                FILE_NAME);
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static int send_message(const Message *message)
{
    // O_APPEND  в конец файла
    int fd = open(FILE_NAME, O_WRONLY | O_CREAT | O_APPEND, 0600);
    ssize_t written;

    if (fd < 0) {
        return -1;
    }
    if (set_lock(fd, F_WRLCK) < 0) {
        close(fd);
        return -1;
    }

    // пишем сразу всю структуру Message
    do {
        written = write(fd, message, sizeof(*message));
    } while (written < 0 && errno == EINTR);

    set_lock(fd, F_UNLCK);
    close(fd);

    if (written != (ssize_t)sizeof(*message)) {
        errno = EIO;
        return -1;
    }
    return 0;
}


 //печатает сообщения для user и сразу удаляет их из файла.
static int receive_messages(const char *user)
{
    int fd = open(FILE_NAME, O_RDWR | O_CREAT, 0600);
    Message message;
    off_t write_position = 0;
    int received_count = 0;
    int failed = 0;

    if (fd < 0) {
        return -1;
    }
    if (set_lock(fd, F_WRLCK) < 0) {
        close(fd);
        return -1;
    }

    for (;;) {
        ssize_t bytes;

        do {
            bytes = read(fd, &message, sizeof(message));
        } while (bytes < 0 && errno == EINTR);

        if (bytes == 0) {
            break;
        }
        if (bytes != (ssize_t)sizeof(message)) {
            fprintf(stderr, "Damaged message file\n");
            failed = 1;
            break;
        }

        if (strcmp(message.recipient, user) == 0) {
            char time_text[20] = "unknown";
            struct tm local_time;

            //переводим time_t в локальную дату и время
            if (localtime_r(&message.sent_at, &local_time) != NULL) {
                strftime(time_text, sizeof(time_text), "%d.%m.%Y %H:%M:%S",
                         &local_time);
            }

            printf("\n[%s] %s: %s\n%s> ", time_text, message.sender,
                   message.text, user);
            fflush(stdout);
            ++received_count;
        } else {
            ssize_t bytes_written;

  // чужое сообщение сохраняем. pwrite() пишет в write_position, но не двигает текущую позицию read()
             
            do {
                bytes_written = pwrite(fd, &message, sizeof(message),
                                       write_position);
            } while (bytes_written < 0 && errno == EINTR);

            if (bytes_written != (ssize_t)sizeof(message)) {
                failed = 1;
                break;
            }
            write_position += (off_t)sizeof(message);
        }
    }

    //обрезаем хвост: в файле остаются только чужие сообщения. 
    if (!failed && ftruncate(fd, write_position) < 0) {
        failed = 1;
    }

    set_lock(fd, F_UNLCK);
    close(fd);

    return failed ? -1 : received_count;
}

static void reader_loop(const char *user)
{
    for (;;) {
        // раз в секунду проверяем общий файл
        if (receive_messages(user) < 0) {
            perror("receive_messages");
        }
        sleep(1);
    }
}

static void writer_loop(const char *user)
{
    char input[NAME_SIZE + TEXT_SIZE + 4];

    for (;;) {
        char *colon;
        char *text;
        Message message;

        printf("%s> ", user);
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL) {
            break;
        }
        input[strcspn(input, "\n")] = '\0';

        if (strcmp(input, "/quit") == 0) {
            break;
        }

        colon = strchr(input, ':');
        if (colon == NULL) {
            printf("Format: recipient: message\n");
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
            printf("Invalid recipient or message is too long\n");
            continue;
        }

        memset(&message, 0, sizeof(message));
        snprintf(message.sender, sizeof(message.sender), "%s", user);
        snprintf(message.recipient, sizeof(message.recipient), "%s", input);
        snprintf(message.text, sizeof(message.text), "%s", text);
        message.sent_at = time(NULL);

        if (send_message(&message) < 0) {
            perror("send_message");
        }
    }
}

int main(int argc, char *argv[])
{
    pid_t child;

    if (argc != 2 || argv[1][0] == '\0' || strlen(argv[1]) >= NAME_SIZE) {
        fprintf(stderr, "Usage: %s USER_NAME\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (check_message_file() < 0) {
        perror("message file");
        return EXIT_FAILURE;
    }

    setvbuf(stdout, NULL, _IONBF, 0);

//fork() создаёт копию процесса child == 0 —это ребёнок-читатель, child > 0  — это родитель-писатель
    child = fork();
    if (child < 0) {
        perror("fork");
        return EXIT_FAILURE;
    }

    if (child == 0) {
        reader_loop(argv[1]);
        return EXIT_SUCCESS;
    }

    printf("Send: recipient: message\nExit: /quit\n");
    writer_loop(argv[1]);

    // после /quit останавливаем читателя 
    kill(child, SIGTERM);
    waitpid(child, NULL, 0);
    return EXIT_SUCCESS;
}
