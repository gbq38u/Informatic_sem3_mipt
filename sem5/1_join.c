#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
void *any_func(void *arg)
{
   // arg имеет тип void *, поэтому сначала приводим его к int  а затем разыменовываем. В результате получаем копию целог числа, адрес которого был передан функции
    int a = *(int *)arg;

    a++;

    // intptr_t позволяет  преобразовать число в void *
    return (void *)(intptr_t)a;
}

int main(void)
{
    pthread_t thread;
    int parg = 2007;
    int pdata;
    void *result;

    if (pthread_create(&thread, NULL, any_func, &parg) != 0) {
        fprintf(stderr, "pthread_create error\n");
        return 1;
    }

    // pthread_join записывает возвращённый потоком указатель в переменную типа void *
    if (pthread_join(thread, &result) != 0) {
        fprintf(stderr, "pthread_join error\n");
        return 1;
    }

    pdata = (int)(intptr_t)result;
    printf("%d\n", pdata); //2008

    //вызываем any_func  в основном потоке и функция увеличит 2008 до 2009
    result = any_func(&pdata);
    pdata = (int)(intptr_t)result;
    printf("%d\n", pdata);// 2009

    return 0;
}