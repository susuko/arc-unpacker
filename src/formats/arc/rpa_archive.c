// RPA archive
//
// Company:   -
// Engine:    Ren'Py
// Extension: .rpa
//
// Known games:
// - Everlasting Summer
// - Katawa Shoujo
// - Long Live The Queen

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "formats/arc/rpa_archive.h"
#include "io.h"
#include "logger.h"
#include "string_ex.h"

typedef struct
{
    char *name;
    uint32_t offset;
    uint32_t size;
    char *prefix;
    size_t prefix_size;
} RpaTableEntry;

typedef struct
{
    IO *arc_io;
    RpaTableEntry *table_entry;
} RpaUnpackContext;



// Stupid unpickle "implementation" ahead:
// instead of twiddling with stack, arrays, dictionaries and all that crap,
// just remember all pushed strings and integers for later interpretation.
// We also take advantage of RenPy using Pickle's HIGHEST_PROTOCOL, which means
// there's no need to parse 90% of the opcodes (such as "S" with escape stuff).
#define PICKLE_MARK            '('
#define PICKLE_STOP            '.'
#define PICKLE_POP             '0'
#define PICKLE_POP_MARK        '1'
#define PICKLE_DUP             '2'
#define PICKLE_FLOAT           'F'
#define PICKLE_INT             'I'
#define PICKLE_BININT1         'K'
#define PICKLE_BININT2         'M'
#define PICKLE_BININT4         'J'
#define PICKLE_LONG            'L'
#define PICKLE_NONE            'N'
#define PICKLE_PERSID          'P'
#define PICKLE_BINPERSID       'Q'
#define PICKLE_REDUCE          'R'
#define PICKLE_STRING          'S'
#define PICKLE_BINSTRING       'T'
#define PICKLE_SHORT_BINSTRING 'U'
#define PICKLE_UNICODE         'V'
#define PICKLE_BINUNICODE      'X'
#define PICKLE_APPEND          'a'
#define PICKLE_BUILD           'b'
#define PICKLE_GLOBAL          'c'
#define PICKLE_DICT            'd'
#define PICKLE_EMPTY_DICT      '}'
#define PICKLE_APPENDS         'e'
#define PICKLE_GET             'g'
#define PICKLE_BINGET          'h'
#define PICKLE_LONG_BINGET     'j'
#define PICKLE_INST            'i'
#define PICKLE_LIST            'l'
#define PICKLE_EMPTY_LIST      ']'
#define PICKLE_OBJ             'o'
#define PICKLE_PUT             'p'
#define PICKLE_BINPUT          'q'
#define PICKLE_LONG_BINPUT     'r'
#define PICKLE_SETITEM         's'
#define PICKLE_TUPLE           't'
#define PICKLE_EMPTY_TUPLE     ')'
#define PICKLE_SETITEMS        'u'
#define PICKLE_BINFLOAT        'G'
// Pickle protocol 2
#define PICKLE_PROTO    (unsigned char)'\x80'
#define PICKLE_NEWOBJ   (unsigned char)'\x81'
#define PICKLE_EXT1     (unsigned char)'\x82'
#define PICKLE_EXT2     (unsigned char)'\x83'
#define PICKLE_EXT4     (unsigned char)'\x84'
#define PICKLE_TUPLE1   (unsigned char)'\x85'
#define PICKLE_TUPLE2   (unsigned char)'\x86'
#define PICKLE_TUPLE3   (unsigned char)'\x87'
#define PICKLE_NEWTRUE  (unsigned char)'\x88'
#define PICKLE_NEWFALSE (unsigned char)'\x89'
#define PICKLE_LONG1    (unsigned char)'\x8a'
#define PICKLE_LONG4    (unsigned char)'\x8b'

typedef struct
{
    Array *string_lengths;
    Array *strings;
    Array *numbers;
} RpaUnpickleContext;

static void rpa_unpickle_handle_string(
    char *str,
    size_t str_size,
    RpaUnpickleContext *context)
{
    array_add(context->strings, str);
    array_add(context->string_lengths, (void*)str_size);
}

static void rpa_unpickle_handle_number(
    size_t number,
    RpaUnpickleContext *context)
{
    array_add(context->numbers, (void*)number);
}

static char *rpa_unpickle_read_string(IO *table_io, size_t str_size)
{
    char *str = (char*)malloc(str_size + 1);
    assert(str != NULL);
    io_read_string(table_io, str, str_size);
    str[str_size] = '\0';
    return str;
}

static bool rpa_unpickle(IO *table_io, RpaUnpickleContext *context)
{
    size_t table_size = io_size(table_io);
    while (io_tell(table_io) < table_size)
    {
        unsigned char c = io_read_u8(table_io);
        switch (c)
        {
            case PICKLE_SHORT_BINSTRING:
            {
                char str_size = io_read_u8(table_io);
                char *string = rpa_unpickle_read_string(table_io, str_size);
                rpa_unpickle_handle_string(string, str_size, context);
                break;
            }

            case PICKLE_BINUNICODE:
            {
                uint32_t str_size = io_read_u32_le(table_io);
                char *string = rpa_unpickle_read_string(table_io, str_size);
                rpa_unpickle_handle_string(string, str_size, context);
                break;
            }

            case PICKLE_BININT1:
            {
                rpa_unpickle_handle_number(io_read_u8(table_io), context);
                break;
            }

            case PICKLE_BININT2:
            {
                rpa_unpickle_handle_number(io_read_u16_le(table_io), context);
                break;
            }

            case PICKLE_BININT4:
            {
                rpa_unpickle_handle_number(io_read_u32_le(table_io), context);
                break;
            }

            case PICKLE_LONG1:
            {
                size_t length = io_read_u8(table_io);
                uint32_t number = 0;
                size_t i;
                size_t pos = io_tell(table_io);
                for (i = 0; i < length; i ++)
                {
                    io_seek(table_io, pos + length - 1 - i);
                    number *= 256;
                    number += io_read_u8(table_io);
                }
                rpa_unpickle_handle_number(number, context);
                io_seek(table_io, pos + length);
                break;
            }

            case PICKLE_PROTO:
                io_skip(table_io, 1);
                break;

            case PICKLE_BINPUT:
                io_skip(table_io, 1);
                break;

            case PICKLE_LONG_BINPUT:
                io_skip(table_io, 4);
                break;

            case PICKLE_APPEND:
            case PICKLE_SETITEMS:
            case PICKLE_MARK:
            case PICKLE_EMPTY_LIST:
            case PICKLE_EMPTY_DICT:
            case PICKLE_TUPLE1:
            case PICKLE_TUPLE2:
            case PICKLE_TUPLE3:
                break;

            case PICKLE_STOP:
                return true;

            default:
                log_error("Unsupported pickle operator '%c' (%02X)", c, c);
                return false;
        }
    }
    return false;
}



static RpaTableEntry **rpa_decode_table(
    const char *table,
    size_t table_size,
    uint32_t key,
    size_t *file_count)
{
    RpaUnpickleContext context;
    context.strings = array_create();
    context.string_lengths = array_create();
    context.numbers = array_create();

    IO *table_io = io_create_from_buffer(table, table_size);
    rpa_unpickle(table_io, &context);

    // Suspicion: reading renpy sources leaves me under impression that older
    // games might not embed prefixes at all. This means that there are twice
    // as many numbers as strings, and all prefixes should be set to empty.
    // Since I haven't seen such games, I leave this remark only as a comment.
    assert(array_size(context.strings) % 2 == 0);
    *file_count = array_size(context.strings) / 2;
    assert(array_size(context.numbers) == array_size(context.strings));

    RpaTableEntry **entries
        = (RpaTableEntry**)malloc(sizeof(RpaTableEntry*) * (*file_count));
    assert(entries != NULL);

    size_t i;
    for (i = 0; i < *file_count; i ++)
    {
        RpaTableEntry *entry = (RpaTableEntry*)malloc(sizeof(RpaTableEntry));
        assert(entry != NULL);
        entry->name = (char*)array_get(context.strings, i*2);
        entry->prefix = (char*)array_get(context.strings, i*2+1);
        entry->prefix_size = (size_t)array_get(context.string_lengths, i*2+1);
        entry->offset = (uint32_t)array_get(context.numbers, i*2) ^ key;
        entry->size = (uint32_t)array_get(context.numbers, i*2+1) ^ key;
        entries[i] = entry;
    }

    array_destroy(context.strings);
    array_destroy(context.string_lengths);
    array_destroy(context.numbers);

    io_destroy(table_io);
    return entries;
}

static int rpa_check_version(IO *arc_io)
{
    const char *rpa_magic_3 = "RPA-3.0 ";
    const char *rpa_magic_2 = "RPA-2.0 ";
    char magic[8];
    io_read_string(arc_io, magic, 8);
    if (memcmp(rpa_magic_2, magic, 8) == 0)
        return 2;
    if (memcmp(rpa_magic_3, magic, 8) == 0)
        return 3;
    return -1;
}

static uint32_t rpa_read_hex_number(IO *arc_io, size_t length)
{
    size_t i;
    uint32_t result = 0;
    for (i = 0; i < length; i ++)
    {
        char c = io_read_u8(arc_io);
        result *= 16;
        if (c >= 'A' && c <= 'F')
            result += c + 10 - 'A';

        else if (c >= 'a' && c <= 'f')
            result += c + 10 - 'a';

        else if (c >= '0' && c <= '9')
            result += c - '0';
    }
    return result;
}

static bool rpa_read_raw_table(IO *arc_io, char **table, size_t *table_size)
{
    size_t compressed_size = io_size(arc_io) - io_tell(arc_io);
    char *compressed = (char*)malloc(compressed_size);
    assert(compressed != NULL);
    if (!io_read_string(arc_io, compressed, compressed_size))
        assert(0);

    *table = NULL;
    bool result = zlib_inflate(compressed, compressed_size, table, table_size);
    if (!result)
    {
        free(*table);
        *table = NULL;
        *table_size = 0;
    }
    free(compressed);
    return result;
}

static VirtualFile *rpa_read_file(void *_context)
{
    RpaUnpackContext *context = (RpaUnpackContext*)_context;
    assert(context != NULL);
    VirtualFile *file = virtual_file_create();
    assert(file != NULL);

    if (!io_seek(context->arc_io, context->table_entry->offset))
        assert(0);
    assert(context->table_entry->offset < io_size(context->arc_io));

    if (!io_write_string(
        file->io,
        context->table_entry->prefix,
        context->table_entry->prefix_size))
    {
        assert(0);
    }

    if (!io_write_string_from_io(
        file->io,
        context->arc_io,
        context->table_entry->size))
    {
        assert(0);
    }

    virtual_file_set_name(file, context->table_entry->name);
    return file;
}

static bool rpa_unpack(
    __attribute__((unused)) Archive *archive,
    IO *arc_io,
    OutputFiles *output_files)
{
    int version = rpa_check_version(arc_io);

    size_t table_offset;
    uint32_t key;
    if (version == 3)
    {
        table_offset = rpa_read_hex_number(arc_io, 16);
        io_skip(arc_io, 1);
        key = rpa_read_hex_number(arc_io, 8);
    }
    else if (version == 2)
    {
        table_offset = rpa_read_hex_number(arc_io, 16);
        key = 0;
    }
    else
    {
        log_error("Not a RPA archive");
        return false;
    }

    log_info("Version: %d", version);

    if (!io_seek(arc_io, table_offset))
    {
        log_error("Bad table offset");
        return false;
    }

    size_t table_size;
    char *table;
    if (!rpa_read_raw_table(arc_io, &table, &table_size))
    {
        log_error("Failed to read table");
        return false;
    }

    size_t file_count;
    RpaTableEntry **entries = rpa_decode_table(
        table, table_size, key, &file_count);
    assert(entries != NULL);
    free(table);

    size_t i;
    RpaUnpackContext context;
    context.arc_io = arc_io;
    for (i = 0; i < file_count; i ++)
    {
        context.table_entry = entries[i];
        output_files_save(output_files, &rpa_read_file, &context);
        free(entries[i]->name);
        free(entries[i]->prefix);
        free(entries[i]);
    }
    free(entries);
    return true;
}

Archive *rpa_archive_create()
{
    Archive *archive = archive_create();
    archive->unpack = &rpa_unpack;
    return archive;
}
