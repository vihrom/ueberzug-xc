#ifndef JSON_PARSER_H
#define JSON_PARSER_H

#include <stddef.h>
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* Command action types */
typedef enum {
    ACTION_ADD,
    ACTION_REMOVE,
    ACTION_UNKNOWN
} CommandAction;

/* Structured representation of an input command */
typedef struct {
    CommandAction action;
    char path[PATH_MAX];
    int x;
    int y;
    int width;
    int height;
} Command;

/* Parse a single JSON line into a Command structure */
int parse_json_line(const char *line, Command *cmd);

/* Free allocated resources associated with a Command */
void free_command(Command *cmd);

#endif /* JSON_PARSER_H */
