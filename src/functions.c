#include "header.h"

void *mapper_function(void *arg) {
    MapperArgs *mapper_args = (MapperArgs *) arg;

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

    pthread_exit(NULL);
}

void *reducer_function(void *arg) {
    printf("Reducer thread\n");
    pthread_exit(NULL);
}

void parse_word(char *word) {
    char *write_ptr = word; // Pointer to overwrite the input string in place
    for (char *read_ptr = word; *read_ptr; read_ptr++) {
        if (isalnum(*read_ptr)) {
            // If it's a letter or digit, convert to lowercase and copy
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
