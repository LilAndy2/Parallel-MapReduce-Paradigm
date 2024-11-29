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

typedef struct {
    WordInfo **unique_words;
    pthread_mutex_t *word_list_mutex;
    int letters_per_thread;
    int extra_letters;
} ReducerArgs;

typedef struct {
    int thread_id;
    int number_of_mapper_threads;
    int total_number_of_threads;
    MapperArgs *mapper_args;
    ReducerArgs *reducer_args;
    pthread_barrier_t *barrier;
} ThreadArgs;

void *thread_function(void *arg);
void *mapper_function(MapperArgs *mapper_args);
void *reducer_function(ReducerArgs *reducer_args, int thread_id, int number_of_mapper_threads, int total_number_of_threads);
void parse_word(char *str);
void add_word_to_list(WordInfo **word_list, const char *word, int file_id);
int compare_words(const void *a, const void *b);
int compare_file_ids(const void *a, const void *b);