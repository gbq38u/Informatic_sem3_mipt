#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

#define TEST_COUNT 10

/*
 * Модифицировать программу,
 * чтобы замерить среднее время завершения нити после сигнала на завершение.
 */

void *any_func(void *arg)
{
    while (1)
    {
        fprintf(stderr, ".");
        sleep(10); // sleep является точкой отмены потока
    }

    return NULL;
}

int main(void)
{
    double total_time = 0.0;

    for (int i = 0; i < TEST_COUNT; i++)
    {
        pthread_t thread;
        void *result = NULL;
        struct timespec start, end;

        if (pthread_create(&thread, NULL, any_func, NULL) != 0)
        {
            fprintf(stderr, "Error\n");
            return 1;
        }

        // даём потоку начать выполнение
        sleep(1);

        // посылаем потоку сигнал на завершение
        pthread_cancel(thread);

        // начинаем измерение после отправки сигнала
        clock_gettime(CLOCK_MONOTONIC, &start);

        // ожидаем фактического завершения потока
        pthread_join(thread, &result);

        clock_gettime(CLOCK_MONOTONIC, &end);

        double time =
            (end.tv_sec - start.tv_sec) +
            (end.tv_nsec - start.tv_nsec) / 1000000000.0;

        total_time += time;

        if (result == PTHREAD_CANCELED)
        {
            fprintf(stderr, " Canceled: %.6f sec\n", time);
        }
    }

    printf("Average time: %.6f sec\n", total_time / TEST_COUNT);

    return 0;
}
