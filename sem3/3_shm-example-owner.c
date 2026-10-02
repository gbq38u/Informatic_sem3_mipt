
 // для совместной работы программа 4 должна получить выведенный shm_id и подключиться к этой области до нажатия Enter
 

#include <stdio.h>
#include <string.h>
#include <sys/shm.h>

// размер создаваемой области разделяемой памяти в байтах 
#define SHMEM_SIZE 4096

// сообщение, которое будет записано в разделяемую память 
#define SH_MESSAGE "Poglad Kota!\n"

int main(void)
{
    // идентификатор области разделяемой памяти 
    int shm_id;

    // адрес подключённой области 
    char *shm_buf;

    // размер области 
    size_t shm_size;

    struct shmid_ds ds;

    
     /* сздаём новую область разделяемой памяти
     * IPC_PRIVATE создать новую уникальную область памяти
     * SHMEM_SIZE размер области в байтах
     * IPC_CREAT  создать область памяти
     * IPC_EXCL потребовать создания новой области
     * 0600 права: чтение и запись только владельцу
     * возвращается идентификатор области или -1 при ошибке*/
    shm_id = shmget(
        IPC_PRIVATE,
        SHMEM_SIZE,
        IPC_CREAT | IPC_EXCL | 0600
    );

    if (shm_id == -1)
    {
        perror("shmget");
        return 1;
    }

    
    /* подключаем разделяемую память к адресному пространству процесса
     * shm_id идентификатор созданной области
     * Возвращается адрес памяти или (void *)-1 при ошибке*/
    shm_buf = (char *)shmat(
        shm_id,
        NULL,
        0
    );

    if (shm_buf == (void *)-1)
    {
        perror("shmat");

        
        /* shm_id идентификатор удаляемой области
         * IPC_RMID пометить область для удаления */
        shmctl(shm_id, IPC_RMID, NULL);
        return 1;
    }

    /* получаем информацию об области разделяемой памяти
     * IPC_STAT записать информацию об области в структуру ds
     * &ds адрес структуры для сохранения информации*/
    if (shmctl(shm_id, IPC_STAT, &ds) == -1)
    {
        perror("shmctl IPC_STAT");
        shmdt(shm_buf);
        shmctl(shm_id, IPC_RMID, NULL);
        return 1;
    }


    shm_size = ds.shm_segsz;

    /* Добавляем 1 к длине строки, поскольку strcpy() также записывает завершающий нулевой символ '\0'*/
    if (shm_size < strlen(SH_MESSAGE) + 1)
    {
        fprintf(stderr, "error: segsize=%zu\n", shm_size);
        shmdt(shm_buf);
        shmctl(shm_id, IPC_RMID, NULL);
        return 1;
    }

    
    /* Копируем сообщение в разделяемую память.
     * shm_buf адрес назначения;
     * SH_MESSAGE исходная строка */
    strcpy(shm_buf, SH_MESSAGE);

    /*
     * Выводим идентификатор */
    printf("ID: %d\n", shm_id);
    printf("Press <Enter> to exit...");
    fflush(stdout);


    fgetc(stdin);

    /*
     * Отключаем область памяти.
     * shm_buf адрес полученный от shmat()*/
    if (shmdt(shm_buf) == -1)
    {
        perror("shmdt");
    }

    
     //Помечаем область разделяемой памяти для удаления

    if (shmctl(shm_id, IPC_RMID, NULL) == -1)
    {
        perror("shmctl IPC_RMID");
        return 1;
    }

    return 0;
}