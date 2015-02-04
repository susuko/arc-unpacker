#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fs.h"
#include "logger.h"
#include "output_files.h"
#include "string_ex.h"

struct OutputFiles
{
    const char *output_dir;
    bool (*save)(OutputFiles*, VirtualFile*(*)(void *), void *);
    bool memory;
    Array *files;
};

static char *get_full_path(OutputFiles *output_files, const char *file_name)
{
    assert(output_files != NULL);
    assert(file_name != NULL);
    assert(strcmp(file_name, "") != 0);
    if (output_files->output_dir == NULL)
        return strdup(file_name);

    char *full_path = (char*)malloc(
        strlen(output_files->output_dir)
        + 1
        + strlen(file_name) + 1);
    assert(full_path != NULL);
    strcpy(full_path, output_files->output_dir);
    strcat(full_path, "/");
    strcat(full_path, file_name);
    return full_path;
}

static bool save_to_hdd(
    OutputFiles *output_files,
    VirtualFile *(*save_proc)(void *),
    void *context)
{
    assert(output_files != NULL);
    assert(save_proc != NULL);
    assert(!output_files->memory);

    log_info("Reading file...");

    bool result;
    VirtualFile *file = save_proc(context);
    if (file != NULL)
    {
        char *full_path = get_full_path(
            output_files,
            virtual_file_get_name(file));
        assert(full_path != NULL);

        log_info("Saving to %s... ", full_path);

        char *dir = dirname(full_path);
        assert(dir != NULL);
        if (!mkpath(dirname(full_path)))
            assert(0);
        IO *output_io = io_create_from_file(full_path, "wb");
        if (!output_io)
        {
            log_warning("Failed to open file %s", full_path);
        }
        else
        {
            io_seek(file->io, 0);
            io_write_string_from_io(output_io, file->io, io_size(file->io));
            io_destroy(output_io);
            result = true;
            log_info("Saved successfully");
        }
        free(full_path);
        free(dir);
    }
    else
    {
        log_error("Error while reading file");
    }

    if (file != NULL)
        virtual_file_destroy(file);
    log_info("");
    return result;
}

static bool save_to_memory(
    OutputFiles *output_files,
    VirtualFile *(*save_proc)(void *),
    void *context)
{
    VirtualFile *file;
    assert(output_files != NULL);
    assert(save_proc != NULL);
    assert(output_files->memory);
    file = save_proc(context);
    if (file == NULL)
        return false;
    if (!array_add(output_files->files, file))
        assert(0);
    return true;
}

OutputFiles *output_files_create_hdd(const char *output_dir)
{
    OutputFiles *output_files = (OutputFiles*)malloc(sizeof(OutputFiles));
    assert(output_files != NULL);
    output_files->output_dir = output_dir;
    output_files->memory = false;
    output_files->files = NULL;
    output_files->save = &save_to_hdd;
    return output_files;
}

OutputFiles *output_files_create_memory()
{
    OutputFiles *output_files = (OutputFiles*)malloc(sizeof(OutputFiles));
    assert(output_files != NULL);
    output_files->output_dir = NULL;
    output_files->memory = true;
    output_files->files = array_create();
    assert(output_files->files != NULL);
    output_files->save = &save_to_memory;
    return output_files;
}

void output_files_destroy(OutputFiles *output_files)
{
    assert(output_files != NULL);
    if (output_files->memory)
    {
        size_t i;
        for (i = 0; i < array_size(output_files->files); i ++)
        {
            VirtualFile *file = (VirtualFile*)array_get(output_files->files, i);
            virtual_file_destroy(file);
        }
        array_destroy(output_files->files);
    }
    free(output_files);
}

bool output_files_save(
    OutputFiles *output_files,
    VirtualFile *(*save_proc)(void *),
    void *context)
{
    assert(output_files != NULL);
    assert(output_files->save != NULL);
    return output_files->save(output_files, save_proc, context);
}

Array *output_files_get_saved(const OutputFiles *output_files)
{
    assert(output_files != NULL);
    assert(output_files->memory);
    return output_files->files;
}
