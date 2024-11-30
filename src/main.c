#include "header.h"

int main(int argc, char **argv) {
    // Check if the number of arguments is correct
    if (argc < 4) {
        printf("Not enough arguments!\n");
        exit(-1);    
    }

    // Keep the arguments in variables
    int number_of_mapper_threads = atoi(argv[1]);
    int number_of_reducer_threads = atoi(argv[2]);
    char *input_file = argv[3];
    int total_number_of_threads = number_of_mapper_threads + number_of_reducer_threads;

    pthread_mutex_t word_list_mutex;
    pthread_mutex_init(&word_list_mutex, NULL);
    pthread_barrier_t barrier;
    pthread_barrier_init(&barrier, NULL, total_number_of_threads);

    atomic_int next_file_index = 0;

    // Allocate memory for threads
    pthread_t *threads = malloc(total_number_of_threads * sizeof(pthread_t));
    if (threads == NULL) {
        printf("Failed to allocate memory for threads\n");
        free(threads);
        exit(-1);
    }

    // Open the input file
    FILE *file = fopen(input_file, "r");
    if (file == NULL) {
        printf("Failed to open file %s\n", input_file);
        free(threads);
        exit(-1);
    }

    // Read the first line of the file -> get the number of files to check
    char first_line[MAX_FILE_NAME_SIZE]; // Variable to store the first line of the file
    if (fgets(first_line, sizeof(first_line), file) != NULL) {
        size_t len = strlen(first_line);
        if (len > 0 && first_line[len - 1] == '\n') {
            first_line[len - 1] = '\0';
        }
    }

    // Read the names of the files to be checked
    char buffer[MAX_FILE_NAME_SIZE];
    FileInfo *checked_files = malloc(atoi(first_line) * sizeof(FileInfo)); // Array to store the names of the files to be checked and their ids
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

    // Parsing the input file names
    for (int i = 0; i < file_count; i++) {
        char *prefix = "../checker/";
        char *path = malloc(strlen(prefix) + strlen(checked_files[i].file_name) + 1);

        sprintf(path, "%s%s", prefix, checked_files[i].file_name);
        free(checked_files[i].file_name);
        checked_files[i].file_name = path;
    }

    WordInfo *unique_words = NULL;

    MapperArgs mapper_args = {
        .checked_files = checked_files,
        .next_file_index = &next_file_index,
        .unique_words = &unique_words,
        .word_list_mutex = &word_list_mutex,
        .file_count = file_count,
    };

    int letters_per_thread = 26 / number_of_reducer_threads;
    int extra_letters = 26 % number_of_reducer_threads;

    ReducerArgs reducer_args = {
        .unique_words = &unique_words,
        .word_list_mutex = &word_list_mutex,
        .letters_per_thread = letters_per_thread,
        .extra_letters = extra_letters,
    };

    int r;
    void *status;

    // Create threads
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

    // Join threads
    for (int id = 0; id < total_number_of_threads; id++) {
        r = pthread_join(threads[id], &status);

        if (r) {
            printf("Failed to join thread %d\n", id);
            free(threads);
            exit(-1);
        }
    }

    // Free used memory
    free(threads); // Free memory allocated for threads
    for (int i = 0; i < file_count; i++) {
        free(checked_files[i].file_name); // Free memory allocated for the file names
    }
    free(checked_files); // Free memory allocated for the checked files array

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