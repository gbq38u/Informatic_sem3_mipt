#include <stdio.h>
#include <pthread.h>
#include <math.h>

/*
 * Переделать программу для доказательства:
 * sin*sin + cos*cos == 1
 *
 * Квадрат синуса считать в одном потоке, косинуса во втором,
 * а результат суммировать в главной программе.
 */

typedef struct
{
    double angle;
    double result;
} ThreadData;

void *thread_func1(void *arg)
{
    ThreadData *data = (ThreadData *)arg;

    // первый поток вычисляет квадрат синуса
    double value = sin(data->angle);
    data->result = value * value;

    return NULL;
}

void *thread_func2(void *arg)
{
    ThreadData *data = (ThreadData *)arg;

    // второй поток вычисляет квадрат косинуса
    double value = cos(data->angle);
    data->result = value * value;

    return NULL;
}

int main(void)
{
    pthread_t thread1, thread2;
    double angle;

    printf("Введите угол в радианах: ");

    if (scanf("%lf", &angle) != 1)
    {
        fprintf(stderr, "Ошибка ввода\n");
        return 1;
    }

    ThreadData data1 = {angle, 0.0};
    ThreadData data2 = {angle, 0.0};

    if (pthread_create(&thread1, NULL, thread_func1, &data1) != 0)
    {
        fprintf(stderr, "Error (thread1)\n");
        return 1;
    }

    if (pthread_create(&thread2, NULL, thread_func2, &data2) != 0)
    {
        fprintf(stderr, "Error (thread2)\n");
        pthread_join(thread1, NULL);
        return 1;
    }

    // ждём завершения обоих потоков
    pthread_join(thread1, NULL);
    pthread_join(thread2, NULL);

    // складываем результаты в главном потоке
    double sum = data1.result + data2.result;

    printf("sin² = %.15f\n", data1.result);
    printf("cos² = %.15f\n", data2.result);
    printf("sum  = %.15f\n", sum);

    return 0;
}