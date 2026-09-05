#include <stdio.h>   
#include <stdlib.h> 

#define MAX_TEXT 10000
#define MAX_WORDS 500

// обрабатываем и выводим одну строку
void print_line(char line[], int length, int reverse_word, int *words_seen)
{
    int start[MAX_WORDS];
    int end[MAX_WORDS];
    int number[MAX_WORDS];
    int word_count = 0;
    int i = 0;
    int j;

    // находим все слова в текущей строке
    while (i < length)
    {
        // пропускаем пробелы
        while (i < length &&
               (line[i] == ' ' || line[i] == '\t'))
        {
            i++;
        }
        if (i < length)
        {
            start[word_count] = i;
            while (i < length &&
                   line[i] != ' ' &&
                   line[i] != '\t')
            {
                i++;
            }
            end[word_count] = i - 1;
            *words_seen = *words_seen + 1;
            number[word_count] = *words_seen;
            word_count++;
        }
    }

    // выводим слова строки от последнего к первому
    for (i = word_count - 1; i >= 0; i--)
    {
        // если слово с номером из параметра 
        if (number[i] == reverse_word)
        {
            for (j = end[i]; j >= start[i]; j--)
            {
                printf("%c", line[j]);
            }
        }
        else
        {
            for (j = start[i]; j <= end[i]; j++)
            {
                printf("%c", line[j]);
            }
        }

        // добавляем пробел 
        if (i > 0)
        {
            printf(" ");
        }
    }
    printf("\n");
}

int main(int argc, char *argv[])
{
    char text[MAX_TEXT];
    int length = 0;
    int symbol;
    int reverse_word;
    int words_seen = 0;
    int line_start = 0;
    int i;
    // проверяем что один параметр
    if (argc != 2)
    {
        printf("Use: ./test WORD_NUMBER\n");
        return 1;
    }
    // параметр из строки в целое число
    reverse_word = atoi(argv[1]);
    if (reverse_word <= 0)
    {
        printf("Word number must be greater than zero.\n");
        return 1;
    }
    printf("Enter all lines. Finish with #:\n");
    symbol = getchar();

    // сохраняем весь текст до символа # 
    while (symbol != '#' && symbol != EOF)
    {
        if (length < MAX_TEXT)
        {
            text[length] = symbol;
            length++;
        }
        symbol = getchar();
    }
    printf("Result:\n");
    // обрабатываем текст построчно
    for (i = 0; i < length; i++)
    {
  //конец строки
        if (text[i] == '\n')
        {
            print_line(
                text + line_start,  
                i - line_start,     
                reverse_word,      
                &words_seen             
            );
            line_start = i + 1;
        }
    }

    // смотрим последнюю строку, если перед # не было /n
    if (line_start < length)
    {
        print_line(
            text + line_start,       
            length - line_start,     
            reverse_word,            
            &words_seen              
        );
    }
    return 0;
}