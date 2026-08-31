#define _XOPEN_SOURCE 500
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ftw.h>
#include <sys/wait.h>

#define DEFAULT_BUFFER_SIZE (64 * 1024)
#define MAX_CACHED_DIRS 1024
#define MAX_RAM_MB 4096
#define MAX_PATH_LEN 1024
#define CLEANUP_FLAGS (FTW_DEPTH | FTW_PHYS)

typedef struct {
    char paths[MAX_CACHED_DIRS][MAX_PATH_LEN];
    int count;
} DirCache;

typedef struct {
    int use_ram;
    unsigned int buffer_size;
    const char *edi_path;
    const char *sandbox_path;
} Options;

DirCache dir_cache = {{}, 0};

int dir_cached(const char *dir) {
    for (int i = 0; i < dir_cache.count; i++) {
        if (strcmp(dir_cache.paths[i], dir) == 0) {
            return 1;
        }
    }
    return 0;
}

void cache_dir(const char *dir) {
    if (dir_cache.count < MAX_CACHED_DIRS) {
        strncpy(dir_cache.paths[dir_cache.count], dir, MAX_PATH_LEN - 1);
        dir_cache.count++;
    }
}

int is_path_safe(const char *sandbox, const char *file_path) {
    char full_path[MAX_PATH_LEN];
    char real_sandbox[MAX_PATH_LEN];
    char real_full_path[MAX_PATH_LEN];

    if (strlen(sandbox) + strlen(file_path) >= MAX_PATH_LEN - 1) {
        return 0;
    }

    snprintf(full_path, sizeof(full_path), "%s%s", sandbox, file_path);

    if (realpath(sandbox, real_sandbox) == NULL) {
        return 0;
    }

    if (realpath(full_path, real_full_path) == NULL) {
        return 1;
    }

    return strncmp(real_full_path, real_sandbox, strlen(real_sandbox)) == 0;
}

void create_parent_directories(const char *base_sandbox, const char *relative_file_path) {
    char full_path[MAX_PATH_LEN];
    snprintf(full_path, sizeof(full_path), "%s%s", base_sandbox, relative_file_path);

    char tmp[MAX_PATH_LEN];
    snprintf(tmp, sizeof(tmp), "%s", full_path);
    size_t len = strlen(tmp);

    for (size_t i = strlen(base_sandbox); i < len; i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            if (!dir_cached(tmp)) {
                mkdir(tmp, 0777);
                cache_dir(tmp);
            }
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

int cleanup_callback(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
    if (remove(fpath) == -1) {
        perror("remove");
    }
    return 0;
}

int get_exit_status(int status) {
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

void print_usage(const char *program) {
    printf("Usage: %s [options] <path/to/game.edi>\n", program);
    printf("Options:\n");
    printf("  --ram true|false       Load entire image into RAM (default: false)\n");
    printf("  --buffer SIZE          Buffer size in KB (default: 64)\n");
    printf("  --sandbox PATH         Custom sandbox directory (default: /tmp/edi_sandbox/)\n");
}

int check_ram_safety(unsigned long total_size, int use_ram) {
    if (!use_ram) {
        return 1;
    }

    unsigned long available_mb = (unsigned long)sysconf(_SC_AVPHYS_PAGES) * sysconf(_SC_PAGE_SIZE) / (1024 * 1024);
    unsigned long needed_mb = (total_size / (1024 * 1024)) + 512;

    printf("[EDI] Available RAM: %lu MB\n", available_mb);
    printf("[EDI] Estimated needed: %lu MB\n", needed_mb);

    if (needed_mb > MAX_RAM_MB) {
        fprintf(stderr, "[EDI] ERROR: Image too large for RAM mode (%lu MB > limit %u MB)\n", needed_mb, MAX_RAM_MB);
        return 0;
    }

    if (needed_mb > available_mb) {
        fprintf(stderr, "[EDI] WARNING: Insufficient RAM available\n");
        fprintf(stderr, "[EDI] Available: %lu MB, Needed: %lu MB\n", available_mb, needed_mb);
        printf("[EDI] Continue anyway? (y/n): ");
        fflush(stdout);

        int response = getchar();
        while (getchar() != '\n');
        if (response != 'y' && response != 'Y') {
            printf("[EDI] Cancelled.\n");
            return 0;
        }
    }

    return 1;
}

Options parse_arguments(int argc, char *argv[]) {
    Options opts = {0, DEFAULT_BUFFER_SIZE, NULL, NULL};

    if (argc < 2) {
        print_usage(argv[0]);
        exit(1);
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--ram") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[EDI] --ram flag requires a value (true or false)\n");
                exit(1);
            }
            int result = parse_bool_flag(argv[i + 1]);
            if (result == -1) {
                exit(1);
            }
            opts.use_ram = result;
            i++;
        } else if (strcmp(argv[i], "--buffer") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[EDI] --buffer flag requires a value in KB\n");
                exit(1);
            }
            unsigned int kb = atoi(argv[i + 1]);
            if (kb <= 0 || kb > 1024 * 1024) {
                fprintf(stderr, "[EDI] Invalid buffer size: %u (must be 1-1048576 KB)\n", kb);
                exit(1);
            }
            opts.buffer_size = kb * 1024;
            i++;
        } else if (strcmp(argv[i], "--sandbox") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[EDI] --sandbox flag requires a path\n");
                exit(1);
            }
            opts.sandbox_path = argv[i + 1];
            i++;
        } else if (argv[i][0] != '-' && opts.edi_path == NULL) {
            opts.edi_path = argv[i];
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "[EDI] Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            exit(1);
        }
    }

    if (opts.edi_path == NULL) {
        fprintf(stderr, "[EDI] No .edi file specified\n");
        print_usage(argv[0]);
        exit(1);
    }

    return opts;
}

int main(int argc, char *argv[]) {
    Options opts = parse_arguments(argc, argv);

    FILE *edi = fopen(opts.edi_path, "rb");
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
    printf("[EDI] RAM mode: %s\n", opts.use_ram ? "enabled" : "disabled");
    printf("[EDI] Buffer size: %u KB\n", opts.buffer_size / 1024);

    char final_sandbox[MAX_PATH_LEN];
    if (opts.sandbox_path) {
        snprintf(final_sandbox, sizeof(final_sandbox), "%s/", opts.sandbox_path);
    } else if (opts.use_ram) {
        snprintf(final_sandbox, sizeof(final_sandbox), "/dev/shm/edi_sandbox_%d/", getpid());
        if (access("/dev/shm", F_OK) == -1) {
            snprintf(final_sandbox, sizeof(final_sandbox), "/tmp/edi_sandbox_ram_%d/", getpid());
        }
    } else {
        snprintf(final_sandbox, sizeof(final_sandbox), "/tmp/edi_sandbox/");
    }

    mkdir(final_sandbox, 0777);
    cache_dir(final_sandbox);

    unsigned int *file_sizes = malloc(sizeof(unsigned int) * file_count);
    unsigned int *file_offsets = malloc(sizeof(unsigned int) * file_count);
    char **file_names = malloc(sizeof(char*) * file_count);
    
    unsigned long total_size = 0;
    for (unsigned int i = 0; i < file_count; i++) {
        unsigned int name_len = 0;
        fread(&name_len, sizeof(unsigned int), 1, edi);

        file_names[i] = malloc(name_len + 1);
        fread(file_names[i], 1, name_len, edi);
        file_names[i][name_len] = '\0';

        fread(&file_sizes[i], sizeof(unsigned int), 1, edi);
        fread(&file_offsets[i], sizeof(unsigned int), 1, edi);
        
        total_size += file_sizes[i];
    }

    if (!check_ram_safety(total_size, opts.use_ram)) {
        fclose(edi);
        for (unsigned int i = 0; i < file_count; i++) {
            free(file_names[i]);
        }
        free(file_names);
        free(file_sizes);
        free(file_offsets);
        free(entry_point);
        return 1;
    }

    unsigned char *buffer = malloc(opts.buffer_size);
    for (unsigned int i = 0; i < file_count; i++) {
        if (!is_path_safe(final_sandbox, file_names[i])) {
            fprintf(stderr, "[EDI] SECURITY: Path traversal attempt detected: %s\n", file_names[i]);
            continue;
        }

        create_parent_directories(final_sandbox, file_names[i]);

        fseek(edi, file_offsets[i], SEEK_SET);

        char target_file_path[MAX_PATH_LEN];
        snprintf(target_file_path, sizeof(target_file_path), "%s%s", final_sandbox, file_names[i]);

        FILE *out = fopen(target_file_path, "wb");
        if (out) {
            unsigned int remaining = file_sizes[i];
            while (remaining > 0) {
                unsigned int to_read = (remaining > opts.buffer_size) ? opts.buffer_size : remaining;
                fread(buffer, 1, to_read, edi);
                fwrite(buffer, 1, to_read, out);
                remaining -= to_read;
            }
            fclose(out);
        }
    }
    free(buffer);
    fclose(edi);

    char entry_path[MAX_PATH_LEN];
    snprintf(entry_path, sizeof(entry_path), "%s%s", final_sandbox, entry_point);
    chmod(entry_path, 0755);

    printf("[EDI] Running... %s\n", entry_path);
    pid_t pid = fork();
    if (pid == 0) {
        execvp(entry_path, (char *const[]) { entry_path, NULL });
        perror("execvp");
        exit(1);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        int result = get_exit_status(status);
    } else {
        perror("fork");
        return 1;
    }

    printf("[EDI] Cleaning up...\n");
    nftw(final_sandbox, cleanup_callback, 64, CLEANUP_FLAGS);

    for (unsigned int i = 0; i < file_count; i++) {
        free(file_names[i]);
    }
    free(file_names);
    free(file_sizes);
    free(file_offsets);
    free(entry_point);

    printf("[EDI] Done.\n");
    return 0;
}
