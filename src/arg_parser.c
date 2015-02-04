#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "arg_parser.h"
#include "collections/pair.h"
#include "string_ex.h"

static bool is_alphanumeric(const char *string);

static bool get_switch(const char *argument, char **key, char **value);

static bool get_flag(const char *argument, char **value);

static Array *create_words(const char *sentence);

static bool is_alphanumeric(const char *string)
{
    unsigned int i;
    for (i = 0; i < strlen(string); i ++)
    {
        char c = string[i];
        if (c < '0' && c > '9' && c < 'a' && c > 'a' && c < 'A' && c > 'A')
            return false;
    }
    return true;
}

static bool get_switch(const char *argument, char **key, char **value)
{
    assert(argument != NULL);
    assert(value != NULL);

    char *value_ptr = NULL;

    *key = NULL;
    *value = NULL;

    if (argument[0] != '-')
        return false;
    while (argument[0] == '-')
        argument ++;

    value_ptr = ((char*)memchr(argument, '=', strlen(argument)));
    if (value_ptr == NULL)
        return false;

    *key = strndup(argument, value_ptr - argument);
    if (!is_alphanumeric(*key))
    {
        free(key);
        return false;
    }
    *value = strdup(value_ptr + 1);
    return true;
}

static bool get_flag(const char *argument, char **value)
{
    assert(argument != NULL);
    assert(value != NULL);

    *value = NULL;

    if (argument[0] != '-')
        return false;
    while (argument[0] == '-')
        argument ++;

    if (!is_alphanumeric(argument))
        return false;

    *value = strdup(argument);
    return true;
}

static Array *create_words(const char *sentence)
{
    assert(sentence != NULL);

    Array *words;
    size_t i = 0;
    const char *word = sentence;
    const char *next_word;

    words = array_create();
    next_word = strpbrk(word, " ");
    while (next_word != NULL)
    {
        char *new_word = strndup(word, next_word - word);
        array_add(words, new_word);
        word = next_word + 1;
        next_word = strpbrk(word, " ");
        i ++;
    }
    array_add(words, strdup(word));
    return words;
}



struct ArgParser
{
    LinkedList *switches;
    LinkedList *flags;
    Array *stray;
    LinkedList *help_items;
};

ArgParser *arg_parser_create()
{
    ArgParser *arg_parser = (ArgParser*)malloc(sizeof(ArgParser));
    assert(arg_parser != NULL);
    arg_parser->flags = linked_list_create();
    arg_parser->switches = linked_list_create();
    arg_parser->stray = array_create();
    arg_parser->help_items = linked_list_create();
    return arg_parser;
}

void arg_parser_destroy(ArgParser *arg_parser)
{
    void *item;
    assert(arg_parser != NULL);

    if (arg_parser->switches != NULL)
    {
        linked_list_reset(arg_parser->switches);
        while ((item = linked_list_get(arg_parser->switches)) != NULL)
        {
            linked_list_advance(arg_parser->switches);
            Pair *pair = (Pair*)item;
            free(pair->e1);
            free(pair->e2);
            pair_destroy(pair);
        }
        linked_list_destroy(arg_parser->switches);
    }

    if (arg_parser->flags != NULL)
    {
        linked_list_reset(arg_parser->flags);
        while ((item = linked_list_get(arg_parser->flags)) != NULL)
        {
            linked_list_advance(arg_parser->flags);
            free(item);
        }
        linked_list_destroy(arg_parser->flags);
    }

    if (arg_parser->stray != NULL)
    {
        size_t i;
        for (i = 0; i < array_size(arg_parser->stray); i ++)
            free(array_get(arg_parser->stray, i));
        array_destroy(arg_parser->stray);
    }

    if (arg_parser->help_items != NULL)
        linked_list_destroy(arg_parser->help_items);

    free(arg_parser);
}

void arg_parser_parse(ArgParser *arg_parser, int argc, const char **argv)
{
    const char *arg;
    char *key, *value;
    int i;

    if (argc == 0)
        return;

    assert(argv != NULL);

    for (i = 0; i < argc; i ++)
    {
        arg = argv[i];
        assert(arg != NULL);

        key = NULL;
        value = NULL;
        if (get_switch(arg, &key, &value))
        {
            linked_list_add(arg_parser->switches, pair_create(key, value));
        }
        else if (get_flag(arg, &value))
        {
            linked_list_add(arg_parser->flags, value);
        }
        else
        {
            array_add(arg_parser->stray, strdup(arg));
        }
    }
}

void arg_parser_clear_help(ArgParser *arg_parser)
{
    assert(arg_parser != NULL);
    linked_list_destroy(arg_parser->help_items);
    arg_parser->help_items = linked_list_create();
}

void arg_parser_add_help(
    ArgParser *arg_parser,
    const char *invocation,
    const char *description)
{
    assert(arg_parser != NULL);
    assert(invocation != NULL);
    assert(description != NULL);

    Pair *pair = pair_create((void*)invocation, (void*)description);
    linked_list_add(arg_parser->help_items, pair);
}

bool arg_parser_has_switch(ArgParser *arg_parser, const char *key)
{
    Pair *pair;
    assert(arg_parser != NULL);
    assert(key != NULL);
    while (key[0] == '-')
        key ++;
    linked_list_reset(arg_parser->switches);
    while ((pair = (Pair*)linked_list_get(arg_parser->switches)) != NULL)
    {
        linked_list_advance(arg_parser->switches);
        if (strcmp((char*)pair->e1, key) == 0)
            return true;
    }
    return false;
}

char *arg_parser_get_switch(ArgParser *arg_parser, const char *key)
{
    Pair *pair;

    assert(arg_parser != NULL);
    assert(key != NULL);

    while (key[0] == '-')
        key ++;

    linked_list_reset(arg_parser->switches);
    while ((pair = (Pair*)linked_list_get(arg_parser->switches)) != NULL)
    {
        linked_list_advance(arg_parser->switches);
        if (strcmp((char*)pair->e1, key) == 0)
            return (char*)pair->e2;
    }
    return NULL;
}

bool arg_parser_has_flag(ArgParser *arg_parser, const char *flag)
{
    void *item;

    assert(arg_parser != NULL);
    assert(flag != NULL);

    while (flag[0] == '-')
        flag ++;

    linked_list_reset(arg_parser->flags);
    while ((item = linked_list_get(arg_parser->flags)) != NULL)
    {
        linked_list_advance(arg_parser->flags);
        if (strcmp((char*)item, flag) == 0)
            return true;
    }
    return false;
}

Array *arg_parser_get_stray(ArgParser *arg_parser)
{
    assert(arg_parser != NULL);
    return arg_parser->stray;
}

void arg_parser_print_help(ArgParser *arg_parser)
{
    const size_t max_invocation_length = 25;
    const size_t max_line_length = 78;
    const char *invocation, *description;
    char *word;
    void *item;
    size_t i, j;
    size_t tmp_length;
    Pair *pair;
    bool first_word;

    assert(arg_parser != NULL);

    if (linked_list_size(arg_parser->help_items) == 0)
    {
        puts("No additional switches are available.");
        return;
    }

    linked_list_reset(arg_parser->help_items);
    while ((item = linked_list_get(arg_parser->help_items)) != NULL)
    {
        linked_list_advance(arg_parser->help_items);

        pair = (Pair*)item;
        invocation = (const char*)pair->e1;
        description = (const char*)pair->e2;

        tmp_length = strlen(invocation);
        if (strlen(invocation) >= 2 && strncmp(invocation, "--", 2) == 0)
        {
            printf("    ");
            tmp_length += 4;
        }
        printf(invocation);

        for (; tmp_length < max_invocation_length; tmp_length ++)
            printf(" ");

        //word wrap
        Array *words = create_words(description);
        first_word = true;
        for (i = 0; i < array_size(words); i ++)
        {
            word = (char*)array_get(words, i);
            tmp_length += strlen(word);
            if (!first_word && tmp_length > max_line_length)
            {
                printf("\n");
                for (j = 0; j < max_invocation_length; j ++)
                    printf(" ");
                tmp_length = max_invocation_length;
            }
            printf("%s ", word);
            free(word);
            first_word = false;
        }
        printf("\n");
        array_destroy(words);
    }
}
