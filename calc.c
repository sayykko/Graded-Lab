// Said Badalzade 251ADB059
// https://github.com/sayykko/Graded-Lab
// Compile with: gcc -O2 -Wall -Wextra -std=c17 -o calc calc.c -lm
// Resources used: RTU lab assignment; OpenAI Codex (AI assistance)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>

#define STUDENT_NAME "said"
#define STUDENT_LASTNAME "badalzade"
#define STUDENT_ID "251ADB059"

typedef struct {
    int is_float;
    long long integer;
    double real;
} Value;

enum TokenKind {
    TOK_END, TOK_NUMBER, TOK_PLUS, TOK_MINUS, TOK_MUL,
    TOK_DIV, TOK_POW, TOK_LPAREN, TOK_RPAREN, TOK_INVALID
};

typedef struct {
    enum TokenKind kind;
    size_t position;
    Value value;
} Token;

typedef struct {
    char *line;
    size_t length;
    size_t cursor;
    size_t offset;
    size_t error;
    Token token;
} Parser;

typedef struct {
    Value value;
    size_t start;
    size_t operator_position;
} PowerPart;

static void *allocate(size_t bytes)
{
    void *memory = malloc(bytes);
    if (memory == NULL) {
        fprintf(stderr, "Out of memory\n");
        exit(EXIT_FAILURE);
    }
    return memory;
}

/* Doubling capacity makes total buffer-growth work linear in appended data. */
static void *reserve(void *data, size_t *capacity, size_t needed, size_t item_size)
{
    if (needed <= *capacity) {
        return data;
    }
    size_t next = *capacity == 0 ? 16 : *capacity;
    while (next < needed) {
        if (next > (size_t)-1 / 2) {
            next = needed;
            break;
        }
        next *= 2;
    }
    if (next > (size_t)-1 / item_size) {
        fprintf(stderr, "Input is too large\n");
        exit(EXIT_FAILURE);
    }
    void *memory = realloc(data, next * item_size);
    if (memory == NULL) {
        fprintf(stderr, "Out of memory\n");
        exit(EXIT_FAILURE);
    }
    *capacity = next;
    return memory;
}

static Value integer_value(long long number)
{
    Value result = {0, number, 0.0};
    return result;
}

static Value real_value(double number)
{
    Value result = {1, 0, number};
    return result;
}

static double as_double(Value number)
{
    return number.is_float ? number.real : (double)number.integer;
}

static int is_zero(Value number)
{
    return number.is_float ? number.real == 0.0 : number.integer == 0;
}

/* offset counts all earlier file bytes, including
   comments and newlines; cursor counts bytes in this line. Their sum + 1 is
   the assignment's 1-based position. A newline is never skipped here because
   each line is a separate expression. Invalid tokens are reported only when
   the parser reaches them, so a later bad character cannot hide an earlier
   syntax error or an already-determined division by zero. */
static void next_token(Parser *parser)
{
    while (parser->cursor < parser->length &&
           isspace((unsigned char)parser->line[parser->cursor])) {
        ++parser->cursor;
    }

    size_t start = parser->cursor;
    parser->token.position = parser->offset + start + 1;
    parser->token.value = integer_value(0);
    if (start == parser->length) {
        parser->token.kind = TOK_END;
        return;
    }

    unsigned char current = (unsigned char)parser->line[start];
    if (isdigit(current)) {
        int floating = 0;
        while (parser->cursor < parser->length &&
               isdigit((unsigned char)parser->line[parser->cursor])) {
            ++parser->cursor;
        }
        /* Both sides of a decimal point must contain a digit. Leaving an
           invalid dot unconsumed makes 5. fail at the dot, not at the 5. */
        if (parser->cursor + 1 < parser->length &&
            parser->line[parser->cursor] == '.' &&
            isdigit((unsigned char)parser->line[parser->cursor + 1])) {
            floating = 1;
            ++parser->cursor;
            while (parser->cursor < parser->length &&
                   isdigit((unsigned char)parser->line[parser->cursor])) {
                ++parser->cursor;
            }
        }

        /* Temporarily terminate exactly this token. In particular, strtod
           must not consume an e-suffix, which this language does not allow. */
        char saved = parser->line[parser->cursor];
        parser->line[parser->cursor] = '\0';
        errno = 0;
        parser->token.kind = TOK_NUMBER;
        if (floating) {
            double number = strtod(parser->line + start, NULL);
            parser->token.value = real_value(number);
            if (!isfinite(number)) {
                parser->token.kind = TOK_INVALID;
            }
        } else {
            long long number = strtoll(parser->line + start, NULL, 10);
            parser->token.value = integer_value(number);
            if (errno == ERANGE) {
                parser->token.kind = TOK_INVALID;
            }
        }
        parser->line[parser->cursor] = saved;
        return;
    }

    ++parser->cursor;
    switch (current) {
        case '+': parser->token.kind = TOK_PLUS; break;
        case '-': parser->token.kind = TOK_MINUS; break;
        case '/': parser->token.kind = TOK_DIV; break;
        case '(': parser->token.kind = TOK_LPAREN; break;
        case ')': parser->token.kind = TOK_RPAREN; break;
        case '*':
            if (parser->cursor < parser->length &&
                parser->line[parser->cursor] == '*') {
                ++parser->cursor;
                parser->token.kind = TOK_POW;
            } else {
                parser->token.kind = TOK_MUL;
            }
            break;
        default: parser->token.kind = TOK_INVALID; break;
    }
}

static void report_error(Parser *parser, size_t position)
{
    if (parser->error == 0) {
        parser->error = position;
    }
}

/* Keeping a type flag preserves integer division; promotion to double
   happens only if an operand is already float.
   The assignment guarantees that tested integer calculations do not overflow.
   Semantic errors use the right operand's start, except non-finite floating
   results, which the specification places on the operator itself. */
static Value calculate(Parser *parser, enum TokenKind operation, Value left,
                       Value right, size_t operator_position, size_t right_start)
{
    if (operation == TOK_DIV && is_zero(right)) {
        report_error(parser, right_start);
        return integer_value(0);
    }

    if (!left.is_float && !right.is_float) {
        long long a = left.integer;
        long long b = right.integer;
        switch (operation) {
            case TOK_PLUS: return integer_value(a + b);
            case TOK_MINUS: return integer_value(a - b);
            case TOK_MUL: return integer_value(a * b);
            case TOK_DIV: return integer_value(a / b);
            case TOK_POW: {
                if (b < 0) {
                    report_error(parser, right_start);
                    return integer_value(0);
                }
                long long result = 1;
                /* Repeated squaring is logarithmic even for a large exponent.
                   Do not square after its last bit: x**1 must not compute x*x.
                   Starting at 1 also gives the required value for 0**0. */
                while (b != 0) {
                    if (b % 2 != 0) {
                        result *= a;
                    }
                    b /= 2;
                    if (b != 0) {
                        a *= a;
                    }
                }
                return integer_value(result);
            }
            default: return integer_value(0);
        }
    }

    double a = as_double(left);
    double b = as_double(right);
    double result;
    switch (operation) {
        case TOK_PLUS: result = a + b; break;
        case TOK_MINUS: result = a - b; break;
        case TOK_MUL: result = a * b; break;
        case TOK_DIV: result = a / b; break;
        case TOK_POW: result = pow(a, b); break;
        default: return integer_value(0);
    }
    /* Underflow to a finite zero is allowed; errno alone is not an error. */
    if (!isfinite(result)) {
        report_error(parser, operator_position);
        return integer_value(0);
    }
    return real_value(result);
}

static Value parse_expression(Parser *parser);

/* Operator levels parse operands through the next tighter level.
   Parentheses restart the full expression grammar. Unary +/- are absent.
   Missing operands or ')' are reported at the current token; at line end
   this is the newline position, or file length + 1 without a final newline. */
static Value parse_primary(Parser *parser)
{
    if (parser->token.kind == TOK_NUMBER) {
        Value number = parser->token.value;
        next_token(parser);
        return number;
    }
    if (parser->token.kind == TOK_LPAREN) {
        next_token(parser);
        Value number = parse_expression(parser);
        if (parser->error != 0) {
            return integer_value(0);
        }
        if (parser->token.kind != TOK_RPAREN) {
            report_error(parser, parser->token.position);
            return integer_value(0);
        }
        next_token(parser);
        return number;
    }
    report_error(parser, parser->token.position);
    return integer_value(0);
}

static Value parse_power(Parser *parser)
{
    size_t first_start = parser->token.position;
    Value first = parse_primary(parser);
    if (parser->error != 0 || parser->token.kind != TOK_POW) {
        return first;
    }

    /* Collect a ** b ** c, then fold from the right, giving a ** (b ** c).
       A heap-backed list avoids one C stack frame per '**' on very long lines.
       Parentheses still use recursion; specified inputs nest at most 1000 deep. */
    PowerPart *parts = NULL;
    size_t count = 0;
    size_t capacity = 0;
    parts = reserve(parts, &capacity, 1, sizeof(*parts));
    parts[count++] = (PowerPart){first, first_start, 0};
    while (parser->error == 0 && parser->token.kind == TOK_POW) {
        size_t operator_position = parser->token.position;
        next_token(parser);
        size_t operand_start = parser->token.position;
        Value operand = parse_primary(parser);
        if (parser->error != 0) {
            break;
        }
        parts = reserve(parts, &capacity, count + 1, sizeof(*parts));
        parts[count++] = (PowerPart){operand, operand_start, operator_position};
    }

    Value result = integer_value(0);
    if (parser->error == 0) {
        result = parts[count - 1].value;
        for (size_t i = count - 1; i > 0 && parser->error == 0; --i) {
            result = calculate(parser, TOK_POW, parts[i - 1].value, result,
                               parts[i].operator_position, parts[i].start);
        }
    }
    free(parts);
    return result;
}

static Value parse_term(Parser *parser)
{
    Value result = parse_power(parser);
    while (parser->error == 0 &&
           (parser->token.kind == TOK_MUL || parser->token.kind == TOK_DIV)) {
        enum TokenKind operation = parser->token.kind;
        size_t operator_position = parser->token.position;
        next_token(parser);
        size_t right_start = parser->token.position;
        Value right = parse_power(parser);
        if (parser->error == 0) {
            result = calculate(parser, operation, result, right,
                               operator_position, right_start);
        }
    }
    return result;
}

static Value parse_expression(Parser *parser)
{
    Value result = parse_term(parser);
    /* Updating result in a loop gives left associativity to + and -. The
       equivalent loop in parse_term gives it to * and /. */
    while (parser->error == 0 &&
           (parser->token.kind == TOK_PLUS || parser->token.kind == TOK_MINUS)) {
        enum TokenKind operation = parser->token.kind;
        size_t operator_position = parser->token.position;
        next_token(parser);
        size_t right_start = parser->token.position;
        Value right = parse_term(parser);
        if (parser->error == 0) {
            result = calculate(parser, operation, result, right,
                               operator_position, right_start);
        }
    }
    return result;
}

/* Reuse the line buffer and read in binary mode so every byte in CRLF
   line endings, if present, is still counted accurately.
   Returns 1 for a line, 0 for EOF, and -1 for an input read failure. */
static int read_line(FILE *input, char **line, size_t *capacity,
                     size_t *length, int *newline)
{
    *length = 0;
    *newline = 0;
    int character;
    while ((character = fgetc(input)) != EOF) {
        if (character == '\n') {
            *newline = 1;
            break;
        }
        *line = reserve(*line, capacity, *length + 2, sizeof(**line));
        (*line)[(*length)++] = (char)character;
    }
    if (ferror(input)) {
        return -1;
    }
    if (*length == 0 && !*newline) {
        return 0;
    }
    *line = reserve(*line, capacity, *length + 1, sizeof(**line));
    (*line)[*length] = '\0';
    return 1;
}

static char *join_path(const char *directory, const char *name)
{
    size_t directory_length = strlen(directory);
    size_t name_length = strlen(name);
    int separator = directory_length > 0 && directory[directory_length - 1] != '/';
    char *path = allocate(directory_length + (size_t)separator + name_length + 1);
    memcpy(path, directory, directory_length);
    if (separator) {
        path[directory_length++] = '/';
    }
    memcpy(path + directory_length, name, name_length + 1);
    return path;
}

static int has_txt_suffix(const char *name)
{
    size_t length = strlen(name);
    return length >= 4 && strcmp(name + length - 4, ".txt") == 0;
}

static char *base_name(const char *path, int strip_txt)
{
    size_t end = strlen(path);
    while (end > 1 && path[end - 1] == '/') {
        --end;
    }
    size_t start = end;
    while (start > 0 && path[start - 1] != '/') {
        --start;
    }
    size_t length = end - start;
    if (strip_txt && length >= 4 && memcmp(path + end - 4, ".txt", 4) == 0) {
        length -= 4;
    }
    if (length == 0 && !strip_txt) {
        char *root = allocate(5);
        memcpy(root, "root", 5);
        return root;
    }
    char *base = allocate(length + 1);
    memcpy(base, path + start, length);
    base[length] = '\0';
    return base;
}

static char *result_name(const char *input_path)
{
    char *base = base_name(input_path, 1);
    size_t length = strlen(base) + strlen(STUDENT_NAME) +
                    strlen(STUDENT_LASTNAME) + strlen(STUDENT_ID) + 8;
    char *name = allocate(length);
    snprintf(name, length, "%s_%s_%s_%s.txt", base, STUDENT_NAME,
             STUDENT_LASTNAME, STUDENT_ID);
    free(base);
    return name;
}

/* Keep results until the whole input file succeeds; otherwise write only
   the first ERROR. An error in a later expression therefore cannot leave
   partial answers in the output file. */
static int process_file(const char *input_path, const char *output_directory)
{
    FILE *input = fopen(input_path, "rb");
    if (input == NULL) {
        fprintf(stderr, "Cannot open input '%s': %s\n", input_path, strerror(errno));
        return 1;
    }

    char *line = NULL;
    size_t line_capacity = 0;
    Value *results = NULL;
    size_t result_capacity = 0;
    size_t result_count = 0;
    size_t offset = 0;
    size_t expression_error = 0;
    int failed = 0;

    for (;;) {
        size_t length;
        int newline;
        int status = read_line(input, &line, &line_capacity, &length, &newline);
        if (status < 0) {
            fprintf(stderr, "Cannot read input '%s'\n", input_path);
            failed = 1;
            break;
        }
        if (status == 0) {
            break;
        }

        size_t first = 0;
        while (first < length && isspace((unsigned char)line[first])) {
            ++first;
        }
        if (first < length && line[first] != '#') {
            Parser parser = {0};
            parser.line = line;
            parser.length = length;
            parser.offset = offset;
            next_token(&parser);
            Value result = parse_expression(&parser);
            if (parser.error == 0 && parser.token.kind != TOK_END) {
                report_error(&parser, parser.token.position);
            }
            if (parser.error != 0) {
                expression_error = parser.error;
                break;
            }
            results = reserve(results, &result_capacity,
                              result_count + 1, sizeof(*results));
            results[result_count++] = result;
        }
        offset += length + (size_t)newline;
    }
    if (fclose(input) != 0) {
        fprintf(stderr, "Cannot close input '%s'\n", input_path);
        failed = 1;
    }
    free(line);
    if (failed) {
        free(results);
        return 1;
    }

    char *name = result_name(input_path);
    char *output_path = join_path(output_directory, name);
    free(name);
    FILE *output = fopen(output_path, "wb");
    if (output == NULL) {
        fprintf(stderr, "Cannot write output '%s': %s\n", output_path, strerror(errno));
        free(output_path);
        free(results);
        return 1;
    }
    if (expression_error != 0) {
        failed = fprintf(output, "ERROR:%zu\n", expression_error) < 0;
    } else {
        for (size_t i = 0; i < result_count; ++i) {
            int written = results[i].is_float
                ? fprintf(output, "%.15g\n", results[i].real)
                : fprintf(output, "%lld\n", results[i].integer);
            if (written < 0) {
                failed = 1;
                break;
            }
        }
    }
    if (fclose(output) != 0) {
        failed = 1;
    }
    if (failed) {
        fprintf(stderr, "Cannot finish writing output '%s'\n", output_path);
    }
    free(output_path);
    free(results);
    /* An expression ERROR is an ordinary file result; only I/O failure
       makes this return non-zero. The directory loop therefore continues. */
    return failed ? 1 : 0;
}

static int ensure_output_directory(const char *path)
{
    struct stat information;
    if (stat(path, &information) == 0) {
        if (S_ISDIR(information.st_mode)) {
            return 0;
        }
        fprintf(stderr, "Output path '%s' is not a directory\n", path);
        return 1;
    }
    if (mkdir(path, 0777) == 0) {
        return 0;
    }
    fprintf(stderr, "Cannot create output directory '%s': %s\n", path, strerror(errno));
    return 1;
}

static int longest_path_first(const void *left, const void *right)
{
    const char *a = *(const char *const *)left;
    const char *b = *(const char *const *)right;
    size_t a_length = strlen(a);
    size_t b_length = strlen(b);
    if (a_length != b_length) {
        return a_length > b_length ? -1 : 1;
    }
    return strcmp(a, b);
}

/* Snapshot names so new outputs cannot become new inputs. S_ISREG excludes
   subdirectories, including those whose names end with '.txt'. */
static int process_directory(const char *path, const char *output_directory)
{
    DIR *directory = opendir(path);
    if (directory == NULL) {
        fprintf(stderr, "Cannot open input directory '%s': %s\n", path, strerror(errno));
        return 1;
    }
    char **files = NULL;
    size_t capacity = 0;
    size_t count = 0;
    int failed = 0;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(directory);
        if (entry == NULL) {
            if (errno != 0) {
                fprintf(stderr, "Cannot read input directory '%s': %s\n", path, strerror(errno));
                failed = 1;
            }
            break;
        }
        if (!has_txt_suffix(entry->d_name)) {
            continue;
        }
        char *file_path = join_path(path, entry->d_name);
        struct stat information;
        if (stat(file_path, &information) != 0) {
            fprintf(stderr, "Cannot inspect '%s': %s\n", file_path, strerror(errno));
            free(file_path);
            failed = 1;
            continue;
        }
        if (S_ISREG(information.st_mode)) {
            files = reserve(files, &capacity, count + 1, sizeof(*files));
            files[count++] = file_path;
        } else {
            free(file_path);
        }
    }
    if (closedir(directory) != 0) {
        fprintf(stderr, "Cannot close input directory '%s'\n", path);
        failed = 1;
    }
    /* All paths have the same directory prefix. Each output adds a nonempty
       suffix to its input's base name, making its name longer. Reading longer
       names first prevents overwriting an unread input when both folders
       refer to the same directory. */
    if (count > 1) {
        qsort(files, count, sizeof(*files), longest_path_first);
    }
    for (size_t i = 0; i < count; ++i) {
        if (process_file(files[i], output_directory) != 0) {
            failed = 1;
        }
        free(files[i]);
    }
    free(files);
    return failed ? 1 : 0;
}

static int usage(const char *program)
{
    fprintf(stderr, "Usage: %s [-d DIR | --dir DIR] "
                    "[-o OUTDIR | --output-dir OUTDIR] [input.txt]\n", program);
    return 1;
}

/* A file and -d are mutually exclusive.
   Directory and result-file naming deliberately differ as specified: the
   default directory omits the surname; individual output files include it. */
int main(int argc, char *argv[])
{
    const char *input_file = NULL;
    const char *input_directory = NULL;
    const char *output_directory = NULL;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--dir") == 0) {
            if (input_directory != NULL || i + 1 == argc || argv[i + 1][0] == '-') {
                return usage(argv[0]);
            }
            input_directory = argv[++i];
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output-dir") == 0) {
            if (output_directory != NULL || i + 1 == argc || argv[i + 1][0] == '-') {
                return usage(argv[0]);
            }
            output_directory = argv[++i];
        } else if (argv[i][0] == '-' || input_file != NULL) {
            return usage(argv[0]);
        } else {
            input_file = argv[i];
        }
    }
    if ((input_file == NULL) == (input_directory == NULL)) {
        return usage(argv[0]);
    }

    const char *source = input_directory != NULL ? input_directory : input_file;
    struct stat information;
    if (stat(source, &information) != 0) {
        fprintf(stderr, "Cannot access input '%s': %s\n", source, strerror(errno));
        return 1;
    }
    if (input_directory != NULL ? !S_ISDIR(information.st_mode) : !S_ISREG(information.st_mode)) {
        fprintf(stderr, "Input '%s' has the wrong type\n", source);
        return 1;
    }

    char *default_directory = NULL;
    if (output_directory == NULL) {
        char *base = base_name(source, input_directory == NULL);
        size_t length = strlen(base) + strlen(STUDENT_NAME) + strlen(STUDENT_ID) + 3;
        default_directory = allocate(length);
        snprintf(default_directory, length, "%s_%s_%s", base, STUDENT_NAME, STUDENT_ID);
        free(base);
        output_directory = default_directory;
    }
    if (ensure_output_directory(output_directory) != 0) {
        free(default_directory);
        return 1;
    }
    int status = input_directory != NULL
        ? process_directory(input_directory, output_directory)
        : process_file(input_file, output_directory);
    free(default_directory);
    return status;
}
