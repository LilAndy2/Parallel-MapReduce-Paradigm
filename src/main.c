#include "header.h"

int main(int argc, char **argv) {
    /* Check if the number of arguments is correct.
        There must always be 4 arguments.
        */
    if (argc != 4) {
        printf("Wrong number of arguments!\n");
        exit(-1);    
    }

    /* Keep the arguments in variables 
        The code is compiled in the following way: 
        ./executable NUM_MAPPERS NUM_REDUCERS input_file
                       argv[1]       argv[2]   argv[3]
        */
    int number_of_mapper_threads = atoi(argv[1]);
    int number_of_reducer_threads = atoi(argv[2]);
    char *input_file = argv[3];
    int total_number_of_threads = number_of_mapper_threads + number_of_reducer_threads;

    /* Synchronization variables */
    pthread_mutex_t word_list_mutex;
    pthread_mutex_init(&word_list_mutex, NULL);
    pthread_barrier_t barrier;
    pthread_barrier_init(&barrier, NULL, total_number_of_threads);

    atomic_int next_file_index = 0;

    /* Allocate memory for threads */
    pthread_t *threads = malloc(total_number_of_threads * sizeof(pthread_t));
    if (!threads) {
        printf("Failed to allocate memory for threads\n");
        free(threads);
        exit(-1);
    }

    /* Open the input file.
        The file has the following content:
        1. number_of_files_to_be_processed 
        2. file1
        3. file2 
        ...
        */
    FILE *file = fopen(input_file, "r");
    if (!file) {
        printf("Failed to open file %s\n", input_file);
        free(threads);
        exit(-1);
    }

    /* Read the first line of the input file -> get the number of files to processed. */
    char first_line[MAX_FILE_NAME_SIZE];
    if (fgets(first_line, sizeof(first_line), file) != NULL) {
        size_t len = strlen(first_line);
        if (len > 0 && first_line[len - 1] == '\n') {
            first_line[len - 1] = '\0';
        }
    }

    /* Read the names of the files to be processed.
        Store the files in an array of FileInfo struct with the fields:
        1. name
        2. id
        */
    char buffer[MAX_FILE_NAME_SIZE];
    FileInfo *checked_files = malloc(atoi(first_line) * sizeof(FileInfo));
    int file_count = 0;
    while (fgets(buffer, sizeof(buffer), file) != NULL && file_count < atoi(first_line)) {
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n') {
            buffer[len - 1] = '\0';
        }

        checked_files[file_count].file_name = malloc((strlen(buffer) + 1) * sizeof(char));
        checked_files[file_count].file_id = file_count + 1;
        strcpy(checked_files[file_count].file_name, buffer);
        file_count++;
    }

    fclose(file);

    /* Parsing the input file names.
        Add the prefix ../checker/ to specify the absolute path of the files. 
        */
    for (int i = 0; i < file_count; i++) {
        char *prefix = "../checker/";
        char *path = malloc(strlen(prefix) + strlen(checked_files[i].file_name) + 1);

        sprintf(path, "%s%s", prefix, checked_files[i].file_name);
        free(checked_files[i].file_name);
        checked_files[i].file_name = path;
    }

    WordInfo *unique_words = NULL;

    /* Define arguments for the mapper function. */
    MapperArgs mapper_args = {
        .checked_files = checked_files,
        .next_file_index = &next_file_index,
        .unique_words = &unique_words,
        .word_list_mutex = &word_list_mutex,
        .file_count = file_count,
    };

    int letters_per_thread = 26 / number_of_reducer_threads;
    int extra_letters = 26 % number_of_reducer_threads;

    /* Define arguments for the reducer function. */
    ReducerArgs reducer_args = {
        .unique_words = &unique_words,
        .word_list_mutex = &word_list_mutex,
        .letters_per_thread = letters_per_thread,
        .extra_letters = extra_letters,
    };

    int r;
    void *status;

    /* Create the threads */
    for (int id = 0; id < total_number_of_threads; id++) {
        ThreadArgs *thread_args = malloc(sizeof(ThreadArgs));

        thread_args->thread_id = id;
        thread_args->number_of_mapper_threads = number_of_mapper_threads;
        thread_args->total_number_of_threads = total_number_of_threads;
        thread_args->mapper_args = &mapper_args;
        thread_args->reducer_args = &reducer_args;
        thread_args->barrier = &barrier;

        r = pthread_create(&threads[id], NULL, thread_function, thread_args);
            
        if (r) {
            printf("Failed to create thread %d\n", id);
            free(thread_args);
            free(threads);
            exit(-1);
        }
    }

    /* Join the threads */
    for (int id = 0; id < total_number_of_threads; id++) {
        r = pthread_join(threads[id], &status);

        if (r) {
            printf("Failed to join thread %d\n", id);
            free(threads);
            exit(-1);
        }
    }

    /* Free used memory for:
        - threads
        - file names
        - processed files array
        - unique words array 
        */
    free(threads);
    for (int i = 0; i < file_count; i++) {
        free(checked_files[i].file_name);
    }
    free(checked_files);

    WordInfo *current = unique_words;
    while (current) {
        WordInfo *temp = current;
        free(current->word);
        free(current->file_ids);
        current = current->next;
        free(temp);
    }

    pthread_mutex_destroy(&word_list_mutex);
    pthread_barrier_destroy(&barrier);

    return 0;
}