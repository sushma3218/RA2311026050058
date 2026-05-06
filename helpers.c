#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Allocate an array of strings
char** new_string_array(long size) {
    return calloc(size, sizeof(char*));
}

// Set string at index
void set_string(char** arr, long idx, char* val) {
    arr[idx] = val;
}

// Get string at index
char* get_string(char** arr, long idx) {
    return arr[idx];
}

// Compare strings (wrapper for strcmp)
long string_compare(char* a, char* b) {
    return strcmp(a, b);
}

// Print prompt for string input
void print_prompt(long i) {
    printf("Enter string %ld: ", i);
    fflush(stdout); // ensure it prints before waiting for input
}

// Read string from stdin
char* read_string(void) {
    char buf[512];
    if (!fgets(buf, sizeof(buf), stdin)) return "";
    buf[strcspn(buf, "\n")] = 0; // Remove newline
    return strdup(buf);
}
