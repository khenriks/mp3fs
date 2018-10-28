#include <thread>
#include <vector>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

int fd;

void read_from_offset(int off) {
    char buffer[10000];
    pread(fd, buffer, sizeof(buffer), off);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        return 2;
    }
    fd = open(argv[1], O_RDONLY);
    if (fd == -1) {
        return 1;
    }
    std::vector<std::thread> threads;
    int count = 4;
    int interval = 30000;
    for (int off = 0; off < count * interval; off += interval) {
        threads.emplace_back(read_from_offset, off);
    }
    for (auto& t : threads) {
        t.join();
    }
    close(fd);
    return 0;
}
