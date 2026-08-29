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

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: edi_runner <path/to/game.edi> or <relative/path/to/game.edi>\n");
        return 1;
    }

    FILE *edi = fopen(argv[1], "rb");
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

    const char *sandbox_dir = "/tmp/edi_sandbox/";
    mkdir(sandbox_dir, 0777);
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
        create_parent_directories(sandbox_dir, file_names[i]);

        fseek(edi, file_offsets[i], SEEK_SET);
        unsigned char *buffer = malloc(file_sizes[i]);
        fread(buffer, 1, file_sizes[i], edi);

        char target_file_path[1024];
        snprintf(target_file_path, sizeof(target_file_path), "%s%s", sandbox_dir, file_names[i]);

        FILE *out = fopen(target_file_path, "wb");
        if (out) {
            fwrite(buffer, 1, file_sizes[i], out);
            fclose(out);
        }
        free(buffer);
    }
    fclose(edi);
//Is this part even ok?

    char chmod_cmd[1024];
    snprintf(chmod_cmd, sizeof(chmod_cmd), "chmod +x \"%s%s\"", sandbox_dir, entry_point);
    system(chmod_cmd);

    char run_cmd[1024];
    snprintf(run_cmd, sizeof(run_cmd), "\"%s%s\"", sandbox_dir, entry_point);
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
    snprintf(clean_cmd, sizeof(clean_cmd), "rm -rf %s", sandbox_dir);
    system(clean_cmd);

    return result;
}

