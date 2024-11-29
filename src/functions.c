#include "header.h"

void *thread_function(void *arg) {
    ThreadArgs *thread_args = (ThreadArgs *) arg;
    if (thread_args->thread_id < thread_args->number_of_mapper_threads) {
        mapper_function(thread_args->mapper_args);
    }

    //printf("Thread %d reached the barrier.\n", thread_args->thread_id);
    pthread_barrier_wait(thread_args->barrier);

    if (thread_args->thread_id >= thread_args->number_of_mapper_threads) {
        reducer_function(thread_args->reducer_args, thread_args->thread_id, thread_args->number_of_mapper_threads, thread_args->total_number_of_threads);
    }

    free(thread_args);
    pthread_exit(NULL);
}

void *mapper_function(MapperArgs *mapper_args) {
    while(1) {
        int current_file_index;

        pthread_mutex_lock(mapper_args->file_index_mutex);
        if (*(mapper_args->next_file_index) >= mapper_args->file_count) {
            pthread_mutex_unlock(mapper_args->file_index_mutex);
            break;
        }
        current_file_index = (*(mapper_args->next_file_index))++;
        pthread_mutex_unlock(mapper_args->file_index_mutex);

        char *file_name = mapper_args->checked_files[current_file_index].file_name;
        FILE *file = fopen(file_name, "r");
        if (!file) {
            printf("Failed to open file %s\n", file_name);
            continue;
        }

        char buffer[MAX_WORD_SIZE];
        while (fscanf(file, "%s", buffer) == 1) {
            parse_word(buffer);
            pthread_mutex_lock(mapper_args->word_list_mutex);
            add_word_to_list(mapper_args->unique_words, buffer, mapper_args->checked_files[current_file_index].file_id);
            pthread_mutex_unlock(mapper_args->word_list_mutex);
        }

        fclose(file);
    }
}

void *reducer_function(ReducerArgs *reducer_args, int thread_id, int number_of_mapper_threads, int total_number_of_threads) {
    int reducer_id = thread_id - number_of_mapper_threads;
    int start_letter = reducer_id * reducer_args->letters_per_thread;
    int end_letter = start_letter + reducer_args->letters_per_thread - 1;

    // Add extra letters to the last thread if applicable
    if (reducer_id == total_number_of_threads - number_of_mapper_threads - 1) {
        end_letter += reducer_args->extra_letters;
    }

    WordInfo *local_buckets[26] = {NULL};

    pthread_mutex_lock(reducer_args->word_list_mutex); // Lock while accessing the shared list
    WordInfo *current = *(reducer_args->unique_words);

    // Group words into local buckets by their starting letter
    while (current) {
        char first_letter = tolower(current->word[0]);
        if (first_letter >= 'a' && first_letter <= 'z') {
            int letter_index = first_letter - 'a';
            if (letter_index >= start_letter && letter_index <= end_letter) {
                // Add the word to the corresponding bucket
                WordInfo *new_entry = malloc(sizeof(WordInfo));
                new_entry->word = strdup(current->word);
                new_entry->file_count = current->file_count;
                new_entry->file_ids = malloc(current->file_count * sizeof(int));
                memcpy(new_entry->file_ids, current->file_ids, current->file_count * sizeof(int));
                new_entry->next = local_buckets[letter_index];
                local_buckets[letter_index] = new_entry;
            }
        }
        current = current->next;
    }
    pthread_mutex_unlock(reducer_args->word_list_mutex);

    // Process each letter bucket assigned to this thread
    for (int i = start_letter; i <= end_letter; i++) {
        // Open the output file for this letter
        char output_file_name[50];
        if (number_of_mapper_threads == 1 && total_number_of_threads - number_of_mapper_threads == 1) {
            //snprintf(output_file_name, sizeof(output_file_name), "../checker/test_sec/%c.txt", 'a' + i);
        } else {
            //snprintf(output_file_name, sizeof(output_file_name), "../checker/test_par/%c.txt", 'a' + i);
        }
        snprintf(output_file_name, sizeof(output_file_name), "%c.txt", 'a' + i);
        FILE *output_file = fopen(output_file_name, "w");
        if (!output_file) {
            perror("Failed to open output file");
            continue;
        }

        if (local_buckets[i]) {
            // Count entries in the bucket
            int bucket_size = 0;
            WordInfo *tmp = local_buckets[i];
            while (tmp) {
                bucket_size++;
                tmp = tmp->next;
            }

            // Create an array for sorting
            WordInfo **bucket_array = malloc(bucket_size * sizeof(WordInfo *));
            tmp = local_buckets[i];
            for (int j = 0; j < bucket_size; j++) {
                bucket_array[j] = tmp;
                tmp = tmp->next;
            }

            // Sort the bucket
            qsort(bucket_array, bucket_size, sizeof(WordInfo *), compare_words);

            // Write sorted words to the output file
            for (int j = 0; j < bucket_size; j++) {
                WordInfo *word_info = bucket_array[j];

                qsort(word_info->file_ids, word_info->file_count, sizeof(int), compare_file_ids);

                fprintf(output_file, "%s:[", word_info->word);
                for (int k = 0; k < word_info->file_count; k++) {
                    if (k == word_info->file_count - 1) {
                        fprintf(output_file, "%d", word_info->file_ids[k]);
                    } else {
                        fprintf(output_file, "%d ", word_info->file_ids[k]);
                    }
                }
                fprintf(output_file, "]\n");
            }

            // Free the bucket array
            free(bucket_array);
        }

        fclose(output_file);

        // Free the local bucket list
        while (local_buckets[i]) {
            WordInfo *to_free = local_buckets[i];
            local_buckets[i] = local_buckets[i]->next;
            free(to_free->word);
            free(to_free->file_ids);
            free(to_free);
        }
    }
}

void parse_word(char *word) {
    char *write_ptr = word; // Pointer to overwrite the input string in place
    for (char *read_ptr = word; *read_ptr; read_ptr++) {
        if (isalpha(*read_ptr)) {
            // If it's a letter, convert to lowercase and copy
            *write_ptr = tolower(*read_ptr);
            write_ptr++;
        }
        // Ignore all other characters (punctuation, spaces, etc.)
    }
    *write_ptr = '\0'; // Null-terminate the processed string
}

void add_word_to_list(WordInfo **word_list, const char *word, int file_id) {
    // Check if the word is already in the list
    WordInfo *current = *word_list;
    while (current) {
        if (strcmp(current->word, word) == 0) {
            // Check if the file ID is already in the list
            for (int i = 0; i < current->file_count; i++) {
                if (current->file_ids[i] == file_id) {
                    return;
                }
            }

            // Add the file ID to the list
            current->file_ids = realloc(current->file_ids, (current->file_count + 1) * sizeof(int));
            current->file_ids[current->file_count] = file_id;
            current->file_count++;
            return;
        }

        current = current->next;
    }

    // If the word is not in the list, create a new entry
    WordInfo *new_word = malloc(sizeof(WordInfo));
    new_word->word = malloc((strlen(word) + 1) * sizeof(char));
    strcpy(new_word->word, word);
    new_word->file_ids = malloc(sizeof(int));
    new_word->file_ids[0] = file_id;
    new_word->file_count = 1;
    new_word->next = NULL;

    // Add the new entry to the list
    if (*word_list == NULL) {
        *word_list = new_word;
    } else {
        current = *word_list;
        while (current->next) {
            current = current->next;
        }

        current->next = new_word;
    }
}

int compare_words(const void *a, const void *b) {
    WordInfo *word_a = *(WordInfo **)a;
    WordInfo *word_b = *(WordInfo **)b;

    if (word_a->file_count != word_b->file_count) {
        return word_b->file_count - word_a->file_count; // Descending by file count
    }
    return strcmp(word_a->word, word_b->word); // Ascending alphabetically
}

int compare_file_ids(const void *a, const void *b) {
    int file_id_a = *(int *)a;
    int file_id_b = *(int *)b;
    return file_id_a - file_id_b; // Ascending order
}