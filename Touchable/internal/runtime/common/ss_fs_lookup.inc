/* A read-only mount cannot change during the process. Cache exact guest names
 * once per directory. This keeps APFS case handling but avoids thousands of
 * repeated directory scans while Unity reads its asset catalog. */
typedef struct { char *guest, *physical; } ss_name_pair;
typedef struct ss_ro_directory {
    char *path;
    ss_name_pair *entries;
    size_t count;
    struct ss_ro_directory *next;
} ss_ro_directory;
static ss_ro_directory *ss_ro_directories;
static int ss_name_compare(const void *a, const void *b) {
    return strcmp(((const ss_name_pair *)a)->guest, ((const ss_name_pair *)b)->guest);
}
/* The caller holds g_path_lock, as it does for mutable path resolution. */
static int ss_path_component_ro(char *parent, size_t capacity, const char *name) {
    ss_ro_directory *cached = ss_ro_directories;
    while (cached && strcmp(cached->path, parent)) cached = cached->next;
    if (!cached) {
        DIR *directory = opendir(parent);
        if (!directory) return 0;
        cached = calloc(1, sizeof(*cached));
        if (!cached) die("Cannot allocate read-only path cache");
        cached->path = strdup(parent);
        size_t capacity = 0;
        struct dirent *entry;
        while ((entry = readdir(directory))) {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
            char guest[0x301];
            if (!guest_name(parent, entry->d_name, guest, sizeof(guest))) continue;
            if (cached->count == capacity) {
                capacity = capacity ? capacity * 2 : 32;
                cached->entries = realloc(cached->entries, capacity * sizeof(*cached->entries));
                if (!cached->entries) die("Cannot grow read-only path cache");
            }
            cached->entries[cached->count++] = (ss_name_pair){strdup(guest), strdup(entry->d_name)};
        }
        closedir(directory);
        qsort(cached->entries, cached->count, sizeof(*cached->entries), ss_name_compare);
        cached->next = ss_ro_directories; ss_ro_directories = cached;
    }
    ss_name_pair key = {(char *)name, NULL};
    ss_name_pair *entry = bsearch(&key, cached->entries, cached->count, sizeof(*cached->entries), ss_name_compare);
    if (!entry) { errno = ENOENT; return 0; }
    size_t length = strlen(parent), added = strlen(entry->physical);
    if (length + added + 2 > capacity) { errno = ENAMETOOLONG; return 0; }
    parent[length] = '/'; memcpy(parent + length + 1, entry->physical, added + 1);
    return 1;
}
