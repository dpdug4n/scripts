#define _GNU_SOURCE
#include <liburing.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <linux/limits.h>
#include <time.h>
#include <errno.h>

#define QUEUE_DEPTH 32
#define BUFFER_SIZE 4096
#define PASSES 3

void random_string(char *buf, size_t len) {
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (size_t i = 0; i < len - 1; i++) {
        buf[i] = charset[rand() % (sizeof(charset) - 1)];
    }
    buf[len - 1] = '\0';
}

int unlink_with_uring(struct io_uring *ring, const char *path) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    if (!sqe) return -1;

    io_uring_prep_unlink(sqe, path, 0);  // <-- add the third arg here!
    io_uring_submit(ring);

    struct io_uring_cqe *cqe;
    io_uring_wait_cqe(ring, &cqe);
    int res = cqe->res;
    io_uring_cqe_seen(ring, cqe);
    return res;
}


int overwrite_file(struct io_uring *ring, const char *filepath, int passes) {
    int fd = open(filepath, O_WRONLY | O_NOATIME);
    if (fd < 0) return -1;

    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        return -1;
    }

    off_t size = st.st_size;
    char *data = malloc(BUFFER_SIZE);
    if (!data) return -1;

    for (int p = 0; p < passes; p++) {
        off_t offset = 0;
        while (offset < size) {
            size_t chunk = (size - offset > BUFFER_SIZE) ? BUFFER_SIZE : size - offset;
            for (size_t i = 0; i < chunk; i++) data[i] = rand() % 256;

            struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
            io_uring_prep_write(sqe, fd, data, chunk, offset);
            io_uring_submit(ring);

            struct io_uring_cqe *cqe;
            io_uring_wait_cqe(ring, &cqe);
            if (cqe->res < 0) {
                fprintf(stderr, "Write failed on %s: %s\n", filepath, strerror(-cqe->res));
                io_uring_cqe_seen(ring, cqe);
                break;
            }
            io_uring_cqe_seen(ring, cqe);
            offset += chunk;
        }
    }

    fsync(fd);
    close(fd);
    free(data);
    return 0;
}

void rename_scramble(const char *path, int times) {
    char newname[PATH_MAX];
    char dirname[PATH_MAX];
    strcpy(dirname, path);
    char *slash = strrchr(dirname, '/');
    if (slash) *slash = '\0';
    else strcpy(dirname, ".");

    char current[PATH_MAX];
    strcpy(current, path);

    for (int i = 0; i < times; i++) {
        char randname[32];
        random_string(randname, sizeof(randname));
        snprintf(newname, sizeof(newname), "%s/%s", dirname, randname);
        rename(current, newname);
        strcpy(current, newname);
    }
    rename(current, path); // rename back to original
}

void wipe_recursive(struct io_uring *ring, const char *path) {
    struct stat st;
    if (lstat(path, &st) < 0) return;

    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path);
        if (!dir) return;

        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
                continue;
            char child[PATH_MAX];
            snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
            wipe_recursive(ring, child);
        }
        closedir(dir);
        rmdir(path);
    } else if (S_ISREG(st.st_mode)) {
        printf("[*] Wiping: %s\n", path);
        overwrite_file(ring, path, PASSES);
        rename_scramble(path, 3);
        unlink_with_uring(ring, path);
    } else {
        unlink(path); // fallback for symlinks/devices
    }
}

void wipe_free_space(const char *dir, struct io_uring *ring) {
    char filler[PATH_MAX];
    snprintf(filler, sizeof(filler), "%s/.tmp_fill", dir);
    int fd = open(filler, O_CREAT | O_WRONLY, 0600);
    if (fd < 0) return;

    char *data = malloc(BUFFER_SIZE);
    if (!data) return;
    for (int i = 0; i < BUFFER_SIZE; i++) data[i] = rand() % 256;

    while (1) {
        ssize_t written = write(fd, data, BUFFER_SIZE);
        if (written <= 0) break;
    }

    fsync(fd);
    close(fd);
    unlink_with_uring(ring, filler);
    free(data);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s /path/to/wipe\n", argv[0]);
        return 1;
    }

    srand(time(NULL));

    struct io_uring ring;
    if (io_uring_queue_init(QUEUE_DEPTH, &ring, 0) < 0) {
        perror("io_uring init failed");
        return 1;
    }

    printf("[*] Starting wipe: %s\n", argv[1]);
    wipe_recursive(&ring, argv[1]);
    wipe_free_space(argv[1], &ring);

    io_uring_queue_exit(&ring);
    printf("[✔] Wipe complete.\n");
    return 0;
}
