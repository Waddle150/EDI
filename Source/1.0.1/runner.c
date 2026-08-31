#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void create_parent_directories(const char *base_sandbox, const char *relative_file_path) {
    char full_path[1024];
    snprintf(full_path, sizeof(full_path), "%s%s", base_sandbox, relative_file_path);

    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", full_path);
    size_t len = strlen(tmp);

    for (size_t i = strlen(base_sandbox); i < len; i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            mkdir(tmp, 0777);
            tmp[i] = '/';
        }
    }
}

int parse_bool_flag(const char *value) {
    if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) {
        return 1;
    } else if (strcmp(value, "false") == 0 || strcmp(value, "0") == 0) {
        return 0;
    } else {
        fprintf(stderr, "[EDI] Invalid boolean value: %s (use 'true' or 'false')\n", value);
        return -1;
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: edi_runner [--ram true|false] <path/to/game.edi>\n");
        printf("  --ram true   : Load entire image into RAM (uses /dev/shm or /tmp)\n");
        printf("  --ram false  : Extract to /tmp/edi_sandbox/ (default)\n");
        return 1;
    }

    int use_ram = 0;
    const char *edi_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--ram") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[EDI] --ram flag requires a value (true or false)\n");
                return 1;
            }
            int result = parse_bool_flag(argv[i + 1]);
            if (result == -1) {
                return 1;
            }
            use_ram = result;
            i++;
        } else if (edi_path == NULL) {
            edi_path = argv[i];
        }
    }

    if (edi_path == NULL) {
        fprintf(stderr, "[EDI] No .edi file specified\n");
        return 1;
    }

    FILE *edi = fopen(edi_path, "rb");
    if (!edi) {
        printf("[EDI] Could not read disc image.\n");
        return 1;
    }

    char magic[4];
    fread(magic, 1, 4, edi);
    if (strncmp(magic, "EDI!", 4) != 0) {
        printf("[EDI] Not a valid .edi image.\n");
        fclose(edi);
        return 1;
    }

    unsigned int entry_len = 0;
    fread(&entry_len, sizeof(unsigned int), 1, edi);
    char *entry_point = malloc(entry_len + 1);
    fread(entry_point, 1, entry_len, edi);
    entry_point[entry_len] = '\0';
    unsigned int file_count = 0;
    fread(&file_count, sizeof(unsigned int), 1, edi);

    printf("[EDI] Starting... %s\n", entry_point);
    printf("[EDI] RAM mode: %s\n", use_ram ? "enabled" : "disabled");

    const char *sandbox_dir = use_ram ? "/dev/shm/edi_sandbox_" : "/tmp/edi_sandbox/";
    char final_sandbox[1024];

    if (use_ram) {
        snprintf(final_sandbox, sizeof(final_sandbox), "/dev/shm/edi_sandbox_%d/", getpid());
        if (access("/dev/shm", F_OK) == -1) {
            snprintf(final_sandbox, sizeof(final_sandbox), "/tmp/edi_sandbox_ram_%d/", getpid());
        }
    } else {
        snprintf(final_sandbox, sizeof(final_sandbox), "/tmp/edi_sandbox/");
    }

    mkdir(final_sandbox, 0777);
    unsigned int *file_sizes = malloc(sizeof(unsigned int) * file_count);
    unsigned int *file_offsets = malloc(sizeof(unsigned int) * file_count);
    char **file_names = malloc(sizeof(char*) * file_count);
    for (unsigned int i = 0; i < file_count; i++) {
        unsigned int name_len = 0;
        fread(&name_len, sizeof(unsigned int), 1, edi);

        file_names[i] = malloc(name_len + 1);
        fread(file_names[i], 1, name_len, edi);
        file_names[i][name_len] = '\0';

        fread(&file_sizes[i], sizeof(unsigned int), 1, edi);
        fread(&file_offsets[i], sizeof(unsigned int), 1, edi);
    }
    for (unsigned int i = 0; i < file_count; i++) {
        create_parent_directories(final_sandbox, file_names[i]);

        fseek(edi, file_offsets[i], SEEK_SET);
        unsigned char *buffer = malloc(file_sizes[i]);
        fread(buffer, 1, file_sizes[i], edi);

        char target_file_path[1024];
        snprintf(target_file_path, sizeof(target_file_path), "%s%s", final_sandbox, file_names[i]);

        FILE *out = fopen(target_file_path, "wb");
        if (out) {
            fwrite(buffer, 1, file_sizes[i], out);
            fclose(out);
        }
        free(buffer);
    }
    fclose(edi);

    char chmod_cmd[1024];
    snprintf(chmod_cmd, sizeof(chmod_cmd), "chmod +x \"%s%s\"", final_sandbox, entry_point);
    system(chmod_cmd);

    char run_cmd[1024];
    snprintf(run_cmd, sizeof(run_cmd), "\"%s%s\"", final_sandbox, entry_point);
    printf("[EDI] Running... %s\n", run_cmd);
    int result = system(run_cmd);

    for (unsigned int i = 0; i < file_count; i++) {
        free(file_names[i]);
    }
    free(file_names);
    free(file_sizes);
    free(file_offsets);
    free(entry_point);

    printf("[EDI] Closing...\n");
    char clean_cmd[1024];
    snprintf(clean_cmd, sizeof(clean_cmd), "rm -rf %s", final_sandbox);
    system(clean_cmd);

    return result;
}
