/* Программа 2 для чтения текста исходного файла из разделяемой памяти.*/
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
/* Предполагаем, что размер исходного файла < SIZE */
#define SIZE 65535 

int main()
{
  /* Указатель на разделяемую память */
  char *memory; 
  /* IPC дескриптор для области разделяемой памяти */
  int shmid;
  /* Имя файла, использующееся для генерации ключа.
     Файл с таким именем должен существовать в текущей директории */
  char pathname[] = "QQQ.Q"; 
  /* IPC ключ */ 
  key_t key;
  
  /* Генерируем IPC ключ из имени файла  в текущей директории
     и номера экземпляра области разделяемой памяти 0 */
  if((key = ftok(pathname,0)) < 0)
  {
    printf("Can\'t generate key\n");
    exit(-1);
  }
  
  /* Пытаемся найти разделяемую память по сгенерированному ключу */
  if((shmid = shmget(key, SIZE, 0666|IPC_CREAT)) < 0)
  {
    printf("Can\'t create shared memory\n");
    exit(-1);
  }
  
  /* Пытаемся отобразить разделяемую память в адресное пространство текущего процесса */ 
  if((memory = (char *)shmat(shmid, NULL, 0)) == (char *)(-1))
  {
    printf("Can't attach shared memory\n");
    exit(-1);
  }
  
  /* Печатаем содержимое разделяемой памяти */
  printf ("%s\n", memory);
  
  /* Отсоединяем разделяемую память и завершаем работу */ 
  if(shmdt(memory) < 0)
  {
    printf("Can't detach shared memory\n");
    exit(-1);
  }
  
  /* Удаляем разделяемую память из системы */ 
  (void)shmctl(shmid, IPC_RMID, (struct shmid_ds *)NULL);
  
  return 0;
}