#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <ctype.h>

#define MAX_FILE_NAME_SIZE 100
#define MAX_WORD_SIZE 100

typedef struct {
    char *file_name;
    int file_id;
} FileInfo;

typedef struct {
    char *word;
    int* file_ids;
    int file_count;
    struct WordInfo *next;
} WordInfo;

typedef struct {
    FileInfo *checked_files;
    int *next_file_index;
    pthread_mutex_t *file_index_mutex;
    WordInfo **unique_words;
    pthread_mutex_t *word_list_mutex;
    int file_count;
} MapperArgs;

void *mapper_function(void *arg);
void *reducer_function(void *arg);
void parse_word(char *str);
void add_word_to_list(WordInfo **word_list, const char *word, int file_id);