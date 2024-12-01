#include "header.h"

/* Thread function is used by all threads.
    Divides each thread's task depending if it is a Mapper or a Reducer. 
    */
void *thread_function(void *arg) {
    ThreadArgs *thread_args = (ThreadArgs *) arg;
    // If thread is a Mapper, perform the mapper function
    if (thread_args->thread_id < thread_args->number_of_mapper_threads) {
        mapper_function(thread_args->mapper_args);
    }

    // Wait for all Mappers to finish their task before proceeding with the Reducers
    pthread_barrier_wait(thread_args->barrier);

    // If thread is a Reducer, perform the reducer function
    if (thread_args->thread_id >= thread_args->number_of_mapper_threads) {
        reducer_function(thread_args->reducer_args, thread_args->thread_id, thread_args->number_of_mapper_threads, thread_args->total_number_of_threads);
    }

    free(thread_args);
    pthread_exit(NULL);
}

/* Mapper function performs the following tasks:
    - opens each file and reads it word by word;
    - for each unique word there is an entry in the local list with the word and the ID of the file it is occured in;
    - after processing the file, it is closed;
    - in the end, all local lists are merged in a global list, which contains all entries. 
    */
void *mapper_function(MapperArgs *mapper_args) {
    WordInfo *local_word_list = NULL;

    /* Using the while loop to give a thread a new file to process if it finished the previous one. 
        By assigning the workload this way, the work is spread equally between the threads, taking
        into consideration the dimension of each file. 
        */
    while(1) {
        // Using an atomic incrementer for the index of the next file to be processed
        // This way, the threads will not process the same file
        int current_file_index = atomic_fetch_add(mapper_args->next_file_index, 1);

        // If all files have been processed, exit the loop
        if (current_file_index >= mapper_args->file_count) {
            break;
        }

        char *file_name = mapper_args->checked_files[current_file_index].file_name;
        FILE *file = fopen(file_name, "r");
        if (!file) {
            printf("Failed to open file %s\n", file_name);
            continue;
        }

        // For each word in the file, parse it and add it to the local list
        char buffer[MAX_WORD_SIZE];
        while (fscanf(file, "%s", buffer) == 1) {
            parse_word(buffer);
            add_word_to_list(&local_word_list, buffer, mapper_args->checked_files[current_file_index].file_id);
        }

        fclose(file);
    }

    /* Merge the local list into the global list. 
        Using a mutex to ensure the list is modified by a single thread at once and there is no race condition. 
        */
    pthread_mutex_lock(mapper_args->word_list_mutex);
    merge_local_list_into_global(mapper_args->unique_words, local_word_list);
    pthread_mutex_unlock(mapper_args->word_list_mutex);
}

/* Reducer function performs the following tasks:
    - groups the words in local buckets by their starting letter;
    - sorts the words in each bucket alphabetically and then after the number of files they appear in;
    - writes the sorted words to the output file for the corresponding letter. 
    */
void *reducer_function(ReducerArgs *reducer_args, int thread_id, int number_of_mapper_threads, int total_number_of_threads) {
    // Spread the workload (letters) equally between the threads
    int reducer_id = thread_id - number_of_mapper_threads;
    int start_letter = reducer_id * reducer_args->letters_per_thread;
    int end_letter = start_letter + reducer_args->letters_per_thread - 1;

    // Add extra letters to the last thread if applicable
    if (reducer_id == total_number_of_threads - number_of_mapper_threads - 1) {
        end_letter += reducer_args->extra_letters;
    }

    WordInfo *local_buckets[26] = {NULL};

    // Use a mutex while accesing the shared list of unique words
    pthread_mutex_lock(reducer_args->word_list_mutex);
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

/* Parse the word so that it is a lowercase alphabetic string.
    Numbers, punctuation signs and other characters will be ignored.
    */
void parse_word(char *word) {
    char *write_ptr = word;
    for (char *read_ptr = word; *read_ptr; read_ptr++) {
        if (isalpha(*read_ptr)) {
            *write_ptr = tolower(*read_ptr);
            write_ptr++;
        }
    }
    *write_ptr = '\0';
}

/* Function to add a word to a partial list. 
    Each entry should have the following form:
    { word, [ file_id1, file_id2, ... ] }
    */
void add_word_to_list(WordInfo **word_list, const char *word, int file_id) {
    WordInfo *current = *word_list;
    // Check if the word is already in the list
    while (current) {
        if (strcmp(current->word, word) == 0) {
            // Check if the file ID is already in the list
            for (int i = 0; i < current->file_count; i++) {
                // If the file ID is already in the list, return
                if (current->file_ids[i] == file_id) {
                    return;
                }
            }

            // If the word is in the list, but with a different set of IDs, add the new ID to the list
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

/* Function to merge a local list of words into a global list. */
void merge_local_list_into_global(WordInfo **global_list, WordInfo *local_list) {
    WordInfo *current_local = local_list;

    while (current_local) {
        // Search for the word in the global list
        WordInfo *current_global = *global_list;
        WordInfo *prev_global = NULL;

        while (current_global && strcmp(current_global->word, current_local->word) < 0) {
            prev_global = current_global;
            current_global = current_global->next;
        }

        if (current_global && strcmp(current_global->word, current_local->word) == 0) {
            // Word exists in the global list; merge file IDs
            for (int i = 0; i < current_local->file_count; i++) {
                int file_id = current_local->file_ids[i];
                int found = 0;

                for (int j = 0; j < current_global->file_count; j++) {
                    if (current_global->file_ids[j] == file_id) {
                        found = 1;
                        break;
                    }
                }

                if (!found) {
                    // Add the file ID to the global word entry
                    current_global->file_ids = realloc(current_global->file_ids, (current_global->file_count + 1) * sizeof(int));
                    current_global->file_ids[current_global->file_count] = file_id;
                    current_global->file_count++;
                }
            }
        } else {
            // Word does not exist in the global list; insert it
            WordInfo *new_global_word = malloc(sizeof(WordInfo));
            new_global_word->word = strdup(current_local->word);
            new_global_word->file_count = current_local->file_count;
            new_global_word->file_ids = malloc(current_local->file_count * sizeof(int));
            memcpy(new_global_word->file_ids, current_local->file_ids, current_local->file_count * sizeof(int));

            // Insert the new word into the global list
            new_global_word->next = current_global;
            if (prev_global) {
                prev_global->next = new_global_word;
            } else {
                *global_list = new_global_word;
            }
        }

        // Move to the next word in the local list
        WordInfo *temp = current_local;
        current_local = current_local->next;

        // Free the local word entry (no longer needed after merging)
        free(temp->word);
        free(temp->file_ids);
        free(temp);
    }
}

/* Comparison function for qsort to sort words by file count and alphabetically. */
int compare_words(const void *a, const void *b) {
    WordInfo *word_a = *(WordInfo **)a;
    WordInfo *word_b = *(WordInfo **)b;

    if (word_a->file_count != word_b->file_count) {
        return word_b->file_count - word_a->file_count; // Descending by file count
    }
    return strcmp(word_a->word, word_b->word); // Ascending alphabetically
}

/* Comparison function for qsort to sort file IDs. */
int compare_file_ids(const void *a, const void *b) {
    int file_id_a = *(int *)a;
    int file_id_b = *(int *)b;
    return file_id_a - file_id_b; // Ascending order
}