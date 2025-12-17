#include "../include/storageserver.h"

// ============================================================================
// GLOBAL SENTENCE LOCKING
// ============================================================================

static void get_lock_key(const char *filename, int sentence_num, char *key, size_t key_size) {
    snprintf(key, key_size, "%s:%d", filename, sentence_num);
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL,
                "Generated lock key: %s", key);
}

int global_try_lock_sentence(StorageServerConfig *ctx, const char *filename,
                            int sentence_num, const char *username) {
    char key[MAX_PATH_LENGTH];
    get_lock_key(filename, sentence_num, key, sizeof(key));

    pthread_mutex_lock(&ctx->lock_table_mutex);

    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL,
                "Lock attempt: key=%s, user='%s', thread_id=%lu",
                key, username, pthread_self());
    printf("  [LOCK CHECK] %s by '%s'\n", key, username);

    SentenceLockEntry *current = ctx->global_locks;
    while (current) {
        if (strcmp(current->filename, filename) == 0 && 
            current->sentence_num == sentence_num) {
            if (current->is_locked) {
                if (strcmp(current->locked_by, username) == 0) {
                    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL,
                                "Lock reacquired: %s by same user '%s'", key, username);
                    printf("  [LOCK OK] Already locked by same user\n");
                    pthread_mutex_unlock(&ctx->lock_table_mutex);
                    return 1;
                }

                log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL,
                            "Lock denied: %s locked by '%s', denied for '%s'",
                            key, current->locked_by, username);
                printf("  [LOCK DENIED] Locked by '%s', denied for '%s'\n",
                       current->locked_by, username);
                pthread_mutex_unlock(&ctx->lock_table_mutex);
                return 0;
            }

            current->is_locked = 1;
            strncpy(current->locked_by, username, MAX_USERNAME_LENGTH - 1);
            current->locked_by[MAX_USERNAME_LENGTH - 1] = '\0';
            current->lock_time = time(NULL);

            log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL,
                        "Lock granted: %s by '%s' (existing entry)", key, username);
            printf("  [LOCK GRANTED] %s by '%s'\n", key, username);
            pthread_mutex_unlock(&ctx->lock_table_mutex);
            return 1;
        }
        current = current->next;
    }

    SentenceLockEntry *entry = malloc(sizeof(SentenceLockEntry));
    if (!entry) {
        log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL,
                    "Lock allocation failed: %s (out of memory)", key);
        pthread_mutex_unlock(&ctx->lock_table_mutex);
        return 0;
    }

    strncpy(entry->filename, filename, MAX_FILENAME_LENGTH - 1);
    entry->filename[MAX_FILENAME_LENGTH - 1] = '\0';
    entry->sentence_num = sentence_num;
    entry->is_locked = 1;
    strncpy(entry->locked_by, username, MAX_USERNAME_LENGTH - 1);
    entry->locked_by[MAX_USERNAME_LENGTH - 1] = '\0';
    entry->lock_time = time(NULL);
    pthread_mutex_init(&entry->mutex, NULL);

    entry->next = ctx->global_locks;
    ctx->global_locks = entry;

    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL,
                "Lock granted: %s by '%s' (new entry created)", key, username);
    printf("  [LOCK GRANTED] %s by '%s' (new entry)\n", key, username);
    pthread_mutex_unlock(&ctx->lock_table_mutex);
    return 1;
}

int global_unlock_sentence(StorageServerConfig *ctx, const char *filename,
                          int sentence_num, const char *username) {
    pthread_mutex_lock(&ctx->lock_table_mutex);

    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL,
                "Unlock attempt: file='%s', sentence=%d, user='%s'",
                filename, sentence_num, username);

    SentenceLockEntry *current = ctx->global_locks;
    while (current) {
        if (strcmp(current->filename, filename) == 0 && 
            current->sentence_num == sentence_num) {
            if (!current->is_locked || strcmp(current->locked_by, username) != 0) {
                log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL,
                            "Unlock failed: file='%s', sentence=%d, not locked by '%s' (locked_by='%s', is_locked=%d)",
                            filename, sentence_num, username, current->locked_by, current->is_locked);
                pthread_mutex_unlock(&ctx->lock_table_mutex);
                return 0;
            }

            time_t lock_duration = time(NULL) - current->lock_time;
            current->is_locked = 0;
            current->locked_by[0] = '\0';

            log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL,
                        "Unlock successful: file='%s', sentence=%d, user='%s', duration=%ld seconds",
                        filename, sentence_num, username, lock_duration);
            printf("  [UNLOCK] %s:%d by '%s'\n", filename, sentence_num, username);
            pthread_mutex_unlock(&ctx->lock_table_mutex);
            return 1;
        }
        current = current->next;
    }

    log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL,
                "Unlock failed: lock entry not found for file='%s', sentence=%d",
                filename, sentence_num);
    pthread_mutex_unlock(&ctx->lock_table_mutex);
    return 0;
}

// ============================================================================
// WORD/SENTENCE OPERATIONS
// ============================================================================

WordNode* create_word_node(const char *word_content) {
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL,
                "Creating word node: content='%s'", word_content);

    WordNode *node = malloc(sizeof(WordNode));
    if (!node) {
        log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL,
                    "Word node allocation failed for: '%s'", word_content);
        return NULL;
    }

    node->content = strdup(word_content);
    if (!node->content) {
        log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL,
                    "Word content strdup failed for: '%s'", word_content);
        free(node);
        return NULL;
    }

    node->prev = NULL;
    node->next = NULL;
    return node;
}

void free_word_list(WordNode *head) {
    int word_count = 0;
    WordNode *current = head;
    while (current) {
        WordNode *next = current->next;
        free(current->content);
        free(current);
        word_count++;
        current = next;
    }

    if (word_count > 0) {
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL,
                    "Freed word list: %d words", word_count);
    }
}

WordNode* parse_words_to_list(const char *sentence_str, int *word_count) {
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL,
                "Parsing words from sentence: '%s'", sentence_str);

    *word_count = 0;
    char temp[MAX_SENTENCE_LENGTH];
    strncpy(temp, sentence_str, MAX_SENTENCE_LENGTH - 1);
    temp[MAX_SENTENCE_LENGTH - 1] = '\0';

    size_t len = strlen(temp);
    if (len > 0 && is_sentence_delimiter(temp[len - 1])) {
        temp[len - 1] = '\0';
    }

    trim_whitespace(temp);

    if (strlen(temp) == 0) {
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL,
                    "Empty sentence after trimming, returning NULL");
        return NULL;
    }

    WordNode *head = NULL;
    WordNode *tail = NULL;

    char *token = strtok(temp, " \t\n\r");
    while (token) {
        WordNode *word_node = create_word_node(token);
        if (!word_node) {
            log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL,
                        "Failed to create word node, freeing list (parsed=%d words)", *word_count);
            free_word_list(head);
            return NULL;
        }

        if (tail) {
            tail->next = word_node;
            word_node->prev = tail;
            tail = word_node;
        } else {
            head = tail = word_node;
        }

        (*word_count)++;
        token = strtok(NULL, " \t\n\r");
    }

    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL,
                "Parsed %d words from sentence", *word_count);
    return head;
}

char* word_list_to_string(WordNode *word_head, char delimiter) {
    if (!word_head) {
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL,
                    "Converting empty word list to string");
        return strdup("");
    }

    size_t total_len = 0;
    int word_count = 0;
    WordNode *current = word_head;
    while (current) {
        total_len += strlen(current->content) + 1;
        word_count++;
        current = current->next;
    }

    total_len += 2;
    char *result = malloc(total_len);
    if (!result) {
        log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL,
                    "Failed to allocate string buffer (size=%zu)", total_len);
        return NULL;
    }

    result[0] = '\0';
    current = word_head;
    while (current) {
        strcat(result, current->content);
        if (current->next) {
            strcat(result, " ");
        }
        current = current->next;
    }

    if (delimiter != '\0') {
        size_t len = strlen(result);
        result[len] = delimiter;
        result[len + 1] = '\0';
    }

    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL,
                "Converted word list to string: %d words, delimiter='%c', length=%zu",
                word_count, delimiter ? delimiter : '0', strlen(result));
    return result;
}

// ============================================================================
// FILE CONTENT LOADING AND SAVING
// ============================================================================

FileContent* load_file_content(const char *storage_dir, const char *filename) {
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL,
                "Loading file content: storage_dir='%s', filename='%s'",
                storage_dir, filename);

    char content[LARGE_BUFFER_SIZE];
    int read_result = ss_read_file(storage_dir, filename, content, sizeof(content));
    if (read_result != ERR_SUCCESS) {
        log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL,
                    "Failed to read file: filename='%s', error=%d", filename, read_result);
        return NULL;
    }

    FileContent *file = malloc(sizeof(FileContent));
    if (!file) {
        log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL,
                    "Failed to allocate FileContent for: %s", filename);
        return NULL;
    }

    strncpy(file->filename, filename, MAX_FILENAME_LENGTH - 1);
    file->head = NULL;
    file->sentence_count = 0;
    pthread_mutex_init(&file->file_lock, NULL);

    size_t content_length = strlen(content);
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL,
                "File content loaded: %zu bytes", content_length);

    if (content_length == 0) {
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL,
                    "Empty file loaded: %s", filename);
        return file;
    }

    char *start = content;
    char *p = content;
    SentenceNode *tail = NULL;

    while (*p) {
        if (is_sentence_delimiter(*p)) {
            size_t len = p - start + 1;
            char sentence_buffer[MAX_SENTENCE_LENGTH];
            strncpy(sentence_buffer, start, len);
            sentence_buffer[len] = '\0';

            SentenceNode *node = malloc(sizeof(SentenceNode));
            if (!node) {
                log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL,
                            "Failed to allocate SentenceNode, freeing file content");
                free_file_content(file);
                return NULL;
            }

            node->delimiter = *p;
            node->word_count = 0;
            node->word_head = parse_words_to_list(sentence_buffer, &node->word_count);
            node->word_tail = node->word_head;
            if (node->word_tail) {
                while (node->word_tail->next) {
                    node->word_tail = node->word_tail->next;
                }
            }

            node->next = NULL;
            node->is_locked = 0;
            node->locked_by[0] = '\0';
            pthread_mutex_init(&node->sentence_lock, NULL);

            if (tail) {
                tail->next = node;
            } else {
                file->head = node;
            }
            tail = node;
            file->sentence_count++;

            log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL,
                        "Parsed sentence #%d: %d words, delimiter='%c'",
                        file->sentence_count - 1, node->word_count, node->delimiter);

            start = p + 1;
            while (*start == ' ' || *start == '\n' || *start == '\r' || *start == '\t') {
                start++;
            }
            p = start;
        } else {
            p++;
        }
    }

    if (*start && start < p) {
        char sentence_buffer[MAX_SENTENCE_LENGTH];
        size_t len = p - start;
        strncpy(sentence_buffer, start, len);
        sentence_buffer[len] = '\0';
        trim_whitespace(sentence_buffer);

        if (strlen(sentence_buffer) > 0) {
            SentenceNode *node = malloc(sizeof(SentenceNode));
            if (node) {
                node->delimiter = '\0';
                node->word_count = 0;
                node->word_head = parse_words_to_list(sentence_buffer, &node->word_count);
                node->word_tail = node->word_head;
                if (node->word_tail) {
                    while (node->word_tail->next) {
                        node->word_tail = node->word_tail->next;
                    }
                }

                node->next = NULL;
                node->is_locked = 0;
                node->locked_by[0] = '\0';
                pthread_mutex_init(&node->sentence_lock, NULL);

                if (tail) {
                    tail->next = node;
                } else {
                    file->head = node;
                }
                file->sentence_count++;

                log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL,
                            "Parsed trailing sentence #%d: %d words, no delimiter",
                            file->sentence_count - 1, node->word_count);
            }
        }
    }

    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL,
                "File content loaded successfully: filename='%s', sentences=%d",
                filename, file->sentence_count);
    return file;
}

int save_file_content(const char *storage_dir, FileContent *file_content) {
    log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL,
                "Saving file content: filename='%s', sentences=%d",
                file_content->filename, file_content->sentence_count);

    char buffer[LARGE_BUFFER_SIZE] = {0};
    int sentences_saved = 0;
    int total_words = 0;

    SentenceNode *current = file_content->head;
    while (current) {
        if (current->word_count > 0) {
            char *sentence_str = word_list_to_string(current->word_head, current->delimiter);
            if (sentence_str) {
                strncat(buffer, sentence_str, LARGE_BUFFER_SIZE - strlen(buffer) - 1);
                total_words += current->word_count;
                sentences_saved++;
                free(sentence_str);

                if (current->next && current->next->word_count > 0) {
                    strncat(buffer, "\n", LARGE_BUFFER_SIZE - strlen(buffer) - 1);
                }
            }
        }
        current = current->next;
    }

    size_t buffer_length = strlen(buffer);
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL,
                "File buffer prepared: %zu bytes, %d sentences, %d words",
                buffer_length, sentences_saved, total_words);

    int result = ss_write_file(storage_dir, file_content->filename, buffer);
    if (result == ERR_SUCCESS) {
        log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL,
                    "File saved successfully: filename='%s', size=%zu bytes",
                    file_content->filename, buffer_length);
    } else {
        log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL,
                    "File save failed: filename='%s', error=%d",
                    file_content->filename, result);
    }

    return result;
}

void free_file_content(FileContent *file_content) {
    if (!file_content) {
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL,
                    "Attempted to free NULL FileContent");
        return;
    }

    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL,
                "Freeing file content: filename='%s', sentences=%d",
                file_content->filename, file_content->sentence_count);

    int sentence_count = 0;
    SentenceNode *current = file_content->head;
    while (current) {
        SentenceNode *next = current->next;
        free_word_list(current->word_head);
        pthread_mutex_destroy(&current->sentence_lock);
        free(current);
        sentence_count++;
        current = next;
    }

    pthread_mutex_destroy(&file_content->file_lock);
    free(file_content);

    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL,
                "File content freed: %d sentences", sentence_count);
}

int lock_sentence(FileContent *file, int sentence_num, const char *username) {
    (void)username;
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL,
                "Lock sentence: file='%s', sentence=%d, current_count=%d",
                file->filename, sentence_num, file->sentence_count);

    if (sentence_num < 0) {
        log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL,
                    "Invalid sentence number: %d (must be >= 0)", sentence_num);
        return ERR_SENTENCE_INDEX_OUT_OF_RANGE;
    }

    if (sentence_num >= file->sentence_count) {
        return ERR_SENTENCE_INDEX_OUT_OF_RANGE;
    }

    SentenceNode *current = file->head;
    for (int i = 0; i < sentence_num && current; i++) {
        current = current->next;
    }

    if (!current) return ERR_SENTENCE_INDEX_OUT_OF_RANGE;

    pthread_mutex_lock(&current->sentence_lock);

    if (sentence_num < 0 || sentence_num >= file->sentence_count) {
        pthread_mutex_unlock(&current->sentence_lock);
        return ERR_SENTENCE_INDEX_OUT_OF_RANGE;
    }

    current->is_locked = 1;
    strncpy(current->locked_by, username, MAX_USERNAME_LENGTH - 1);
    current->locked_by[MAX_USERNAME_LENGTH - 1] = '\0';

    pthread_mutex_unlock(&current->sentence_lock);
    return ERR_SUCCESS;
}

int unlock_sentence(FileContent *file, int sentence_num, const char *username) {
    (void)username;
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL,
                "Unlock sentence: file='%s', sentence=%d",
                file->filename, sentence_num);

    if (sentence_num < 0 || sentence_num >= file->sentence_count) {
        log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL,
                    "Unlock failed: sentence %d out of range (count=%d)",
                    sentence_num, file->sentence_count);
        return ERR_SENTENCE_INDEX_OUT_OF_RANGE;
    }

    return ERR_SUCCESS;
}

// ============================================================================
// MULTI-WORD SENTENCE MODIFICATION (FIXED VERSION)
// ============================================================================

int modify_sentence_multiword(FileContent *file, int sentence_num, int word_index,
    const char *new_content, const char *username, int *new_sentence_num) {
(void)username;

log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL,
"Multiword modify: file='%s', sentence=%d, word_index=%d, content='%s', user='%s'",
file->filename, sentence_num, word_index, new_content, username);

*new_sentence_num = sentence_num;

// Validate sentence exists
if (sentence_num < 0 || sentence_num >= file->sentence_count) {
log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL,
"Sentence index %d out of range (count=%d)",
sentence_num, file->sentence_count);
return ERR_SENTENCE_INDEX_OUT_OF_RANGE;
}

// Navigate to target sentence
SentenceNode *current = file->head;
for (int i = 0; i < sentence_num && current; i++) {
current = current->next;
}

if (!current) {
log_message(log_file, LOG_LEVEL_ERROR, NULL, 0, NULL,
"Sentence node is NULL after traversal (sentence=%d)", sentence_num);
return ERR_SENTENCE_INDEX_OUT_OF_RANGE;
}

pthread_mutex_lock(&current->sentence_lock);

// Allow word_index == word_count for appending
if (word_index < 0 || word_index > current->word_count) {
log_message(log_file, LOG_LEVEL_WARNING, NULL, 0, NULL,
"Word index %d out of range (word_count=%d, max allowed=%d)",
word_index, current->word_count, current->word_count);
pthread_mutex_unlock(&current->sentence_lock);
return ERR_WORD_INDEX_OUT_OF_RANGE;
}

// Save original delimiter before modifications
char original_delimiter = current->delimiter;

// Parse input content into words
char temp[MAX_SENTENCE_LENGTH];
strncpy(temp, new_content, MAX_SENTENCE_LENGTH - 1);
temp[MAX_SENTENCE_LENGTH - 1] = '\0';

char *words[100];
int word_count = 0;
char *token = strtok(temp, " \t\n\r");
while (token && word_count < 100) {
words[word_count++] = strdup(token);
token = strtok(NULL, " \t\n\r");
}

log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL,
"Parsed %d words from content: '%s'", word_count, new_content);

// Navigate to insertion point
WordNode *insert_after = NULL;
if (word_index > 0) {
insert_after = current->word_head;
for (int i = 1; i < word_index && insert_after; i++) {
insert_after = insert_after->next;
}
}

// Process each word
int current_sent = sentence_num;
SentenceNode *active_sent = current;
WordNode *last_inserted = insert_after;

for (int i = 0; i < word_count; i++) {
char *word = words[i];
int len = strlen(word);
char delimiter = '\0';
int delimiter_pos = -1;

// Find delimiter in word
for (int j = 0; j < len; j++) {
if (is_sentence_delimiter(word[j])) {
delimiter = word[j];
delimiter_pos = j;
printf("  → Delimiter '%c' found at position %d in '%s'\n", delimiter, j, word);
break;
}
}

// Case 1: No delimiter in word - simple insertion
if (delimiter == '\0') {
WordNode *new_word = create_word_node(word);
if (!new_word) {
free(word);
pthread_mutex_unlock(&current->sentence_lock);
for (int j = i + 1; j < word_count; j++) free(words[j]);
return ERR_OUT_OF_MEMORY;
}

if (!last_inserted) {
new_word->next = active_sent->word_head;
if (active_sent->word_head) {
active_sent->word_head->prev = new_word;
}
active_sent->word_head = new_word;
if (!active_sent->word_tail) {
active_sent->word_tail = new_word;
}
} else {
new_word->next = last_inserted->next;
new_word->prev = last_inserted;
if (last_inserted->next) {
last_inserted->next->prev = new_word;
} else {
active_sent->word_tail = new_word;
}
last_inserted->next = new_word;
}

active_sent->word_count++;
last_inserted = new_word;
free(word);
}
// Case 2: Delimiter at end of word
else if (delimiter_pos == len - 1) {
word[delimiter_pos] = '\0';

// Insert word if not empty
if (strlen(word) > 0) {
WordNode *new_word = create_word_node(word);
if (!new_word) {
free(word);
pthread_mutex_unlock(&current->sentence_lock);
for (int j = i + 1; j < word_count; j++) free(words[j]);
return ERR_OUT_OF_MEMORY;
}

if (!last_inserted) {
new_word->next = active_sent->word_head;
if (active_sent->word_head) {
active_sent->word_head->prev = new_word;
}
active_sent->word_head = new_word;
if (!active_sent->word_tail) {
active_sent->word_tail = new_word;
}
} else {
new_word->next = last_inserted->next;
new_word->prev = last_inserted;
if (last_inserted->next) {
last_inserted->next->prev = new_word;
} else {
active_sent->word_tail = new_word;
}
last_inserted->next = new_word;
}

active_sent->word_count++;
last_inserted = new_word;
}

// Extract remaining words
WordNode *remaining_words = NULL;
char moved_delimiter = '\0';

if (last_inserted && last_inserted->next) {
remaining_words = last_inserted->next;
last_inserted->next = NULL;
if (remaining_words) {
remaining_words->prev = NULL;
}

int remaining_count = 0;
WordNode *tmp = remaining_words;
while (tmp) {
remaining_count++;
tmp = tmp->next;
}

if (original_delimiter != '\0') {
moved_delimiter = original_delimiter;
printf("  → Transferring original delimiter '%c' to moved words\n",
 moved_delimiter);
}

active_sent->word_tail = last_inserted;
active_sent->word_count -= remaining_count;

printf("  → Extracted %d remaining words from sentence %d\n",
remaining_count, current_sent);
}

// End current sentence with new delimiter
active_sent->delimiter = delimiter;
printf("  → Sentence %d ended with '%c'\n", current_sent, delimiter);
free(word);

// Create new sentence
pthread_mutex_lock(&file->file_lock);

SentenceNode *new_sent = malloc(sizeof(SentenceNode));
if (!new_sent) {
pthread_mutex_unlock(&file->file_lock);
pthread_mutex_unlock(&current->sentence_lock);
for (int j = i + 1; j < word_count; j++) free(words[j]);
return ERR_OUT_OF_MEMORY;
}

new_sent->word_head = NULL;
new_sent->word_tail = NULL;
new_sent->word_count = 0;
new_sent->delimiter = '\0';
new_sent->is_locked = 0;
new_sent->locked_by[0] = '\0';
pthread_mutex_init(&new_sent->sentence_lock, NULL);

new_sent->next = active_sent->next;
active_sent->next = new_sent;
file->sentence_count++;
current_sent++;

pthread_mutex_unlock(&file->file_lock);

if (remaining_words) {
new_sent->word_head = remaining_words;

WordNode *tail = remaining_words;
int count = 1;
while (tail->next) {
tail = tail->next;
count++;
}
new_sent->word_tail = tail;
new_sent->word_count = count;

new_sent->delimiter = moved_delimiter;

printf("  → Moved %d words to new sentence %d (delimiter='%c')\n",
count, current_sent, moved_delimiter ? moved_delimiter : '0');

if (moved_delimiter != '\0' && i < word_count - 1) {
printf("  → Moved words have delimiter, creating sentence %d for remaining insertions\n",
 current_sent + 1);

pthread_mutex_lock(&file->file_lock);

SentenceNode *continuation_sent = malloc(sizeof(SentenceNode));
if (!continuation_sent) {
pthread_mutex_unlock(&file->file_lock);
pthread_mutex_unlock(&current->sentence_lock);
for (int j = i + 1; j < word_count; j++) free(words[j]);
return ERR_OUT_OF_MEMORY;
}

continuation_sent->word_head = NULL;
continuation_sent->word_tail = NULL;
continuation_sent->word_count = 0;
continuation_sent->delimiter = '\0';
continuation_sent->is_locked = 0;
continuation_sent->locked_by[0] = '\0';
pthread_mutex_init(&continuation_sent->sentence_lock, NULL);

continuation_sent->next = new_sent->next;
new_sent->next = continuation_sent;
file->sentence_count++;
current_sent++;

pthread_mutex_unlock(&file->file_lock);

printf("  → Created sentence #%d for continuation\n", current_sent);

active_sent = continuation_sent;
last_inserted = NULL;
} else {
printf("  → Created sentence #%d\n", current_sent);
active_sent = new_sent;
if (new_sent->word_tail) {
last_inserted = new_sent->word_tail;
} else {
last_inserted = NULL;
}
}
} else {
printf("  → Created sentence #%d\n", current_sent);
active_sent = new_sent;
last_inserted = NULL;
}
}
// Case 3: Delimiter in middle of word
else {
char before_delim[MAX_SENTENCE_LENGTH];
char after_delim[MAX_SENTENCE_LENGTH];

strncpy(before_delim, word, delimiter_pos);
before_delim[delimiter_pos] = '\0';

strcpy(after_delim, word + delimiter_pos + 1);

if (strlen(before_delim) > 0) {
WordNode *new_word = create_word_node(before_delim);
if (!new_word) {
free(word);
pthread_mutex_unlock(&current->sentence_lock);
for (int j = i + 1; j < word_count; j++) free(words[j]);
return ERR_OUT_OF_MEMORY;
}

if (!last_inserted) {
new_word->next = active_sent->word_head;
if (active_sent->word_head) {
active_sent->word_head->prev = new_word;
}
active_sent->word_head = new_word;
if (!active_sent->word_tail) {
active_sent->word_tail = new_word;
}
} else {
new_word->next = last_inserted->next;
new_word->prev = last_inserted;
if (last_inserted->next) {
last_inserted->next->prev = new_word;
} else {
active_sent->word_tail = new_word;
}
last_inserted->next = new_word;
}

active_sent->word_count++;
last_inserted = new_word;
}

WordNode *remaining_words = NULL;
char moved_delimiter = '\0';

if (last_inserted && last_inserted->next) {
remaining_words = last_inserted->next;
last_inserted->next = NULL;
if (remaining_words) {
remaining_words->prev = NULL;
}

int remaining_count = 0;
WordNode *tmp = remaining_words;
while (tmp) {
remaining_count++;
tmp = tmp->next;
}

if (original_delimiter != '\0') {
moved_delimiter = original_delimiter;
printf("  → Transferring original delimiter '%c' to moved words (mid-split)\n",
 moved_delimiter);
}

active_sent->word_tail = last_inserted;
active_sent->word_count -= remaining_count;

printf("  → Extracted %d remaining words (mid-word split)\n", remaining_count);
}

active_sent->delimiter = delimiter;
printf("  → Sentence %d ended with '%c' (mid-word split)\n", current_sent, delimiter);

pthread_mutex_lock(&file->file_lock);

SentenceNode *new_sent = malloc(sizeof(SentenceNode));
if (!new_sent) {
pthread_mutex_unlock(&file->file_lock);
pthread_mutex_unlock(&current->sentence_lock);
free(word);
for (int j = i + 1; j < word_count; j++) free(words[j]);
return ERR_OUT_OF_MEMORY;
}

new_sent->word_head = NULL;
new_sent->word_tail = NULL;
new_sent->word_count = 0;
new_sent->delimiter = '\0';
new_sent->is_locked = 0;
new_sent->locked_by[0] = '\0';
pthread_mutex_init(&new_sent->sentence_lock, NULL);

new_sent->next = active_sent->next;
active_sent->next = new_sent;
file->sentence_count++;
current_sent++;

pthread_mutex_unlock(&file->file_lock);

if (remaining_words) {
new_sent->word_head = remaining_words;

WordNode *tail = remaining_words;
int count = 1;
while (tail->next) {
tail = tail->next;
count++;
}
new_sent->word_tail = tail;
new_sent->word_count = count;

new_sent->delimiter = moved_delimiter;

printf("  → Moved %d words to new sentence %d (delimiter='%c')\n",
count, current_sent, moved_delimiter ? moved_delimiter : '0');

if (moved_delimiter != '\0' && (i < word_count - 1 || strlen(after_delim) > 0)) {
printf("  → Moved words have delimiter, creating sentence %d for continuation\n",
 current_sent + 1);

pthread_mutex_lock(&file->file_lock);

SentenceNode *continuation_sent = malloc(sizeof(SentenceNode));
if (!continuation_sent) {
pthread_mutex_unlock(&file->file_lock);
pthread_mutex_unlock(&current->sentence_lock);
free(word);
for (int j = i + 1; j < word_count; j++) free(words[j]);
return ERR_OUT_OF_MEMORY;
}

continuation_sent->word_head = NULL;
continuation_sent->word_tail = NULL;
continuation_sent->word_count = 0;
continuation_sent->delimiter = '\0';
continuation_sent->is_locked = 0;
continuation_sent->locked_by[0] = '\0';
pthread_mutex_init(&continuation_sent->sentence_lock, NULL);

continuation_sent->next = new_sent->next;
new_sent->next = continuation_sent;
file->sentence_count++;
current_sent++;

pthread_mutex_unlock(&file->file_lock);

printf("  → Created sentence #%d for continuation\n", current_sent);

active_sent = continuation_sent;
last_inserted = NULL;
} else {
printf("  → Created sentence #%d for text after delimiter\n", current_sent);
active_sent = new_sent;
if (new_sent->word_tail) {
last_inserted = new_sent->word_tail;
} else {
last_inserted = NULL;
}
}
} else {
printf("  → Created sentence #%d for text after delimiter\n", current_sent);
active_sent = new_sent;
last_inserted = NULL;
}

if (strlen(after_delim) > 0) {
free(word);
words[i] = strdup(after_delim);
i--;
continue;
}

free(word);
}
}

pthread_mutex_unlock(&current->sentence_lock);
*new_sentence_num = current_sent;

log_message(log_file, LOG_LEVEL_INFO, NULL, 0, NULL,
"Multiword modify completed: inserted %d words, final_sentence=%d",
word_count, current_sent);

return ERR_SUCCESS;
}


int modify_sentence(FileContent *file, int sentence_num, int word_index,
                   const char *new_content, const char *username) {
    int new_sent;
    log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL,
                "Legacy modify_sentence called, delegating to multiword version");
    return modify_sentence_multiword(file, sentence_num, word_index, new_content, username, &new_sent);
}

char* get_sentence_string(SentenceNode *sentence) {
    if (!sentence) {
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL,
                    "get_sentence_string called with NULL sentence");
        return NULL;
    }

    char *result = word_list_to_string(sentence->word_head, sentence->delimiter);
    if (result) {
        log_message(log_file, LOG_LEVEL_DEBUG, NULL, 0, NULL,
                    "Generated sentence string: words=%d, delimiter='%c', length=%zu",
                    sentence->word_count, sentence->delimiter ? sentence->delimiter : '0',
                    strlen(result));
    }

    return result;
}