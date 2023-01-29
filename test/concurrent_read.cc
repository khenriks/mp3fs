#include <fcntl.h>
#include <unistd.h>

#include <cstdlib>
#include <thread>
#include <vector>

int fd;

void read_from_offset(int off) {
    const int buffer_size = 10000;
    char buffer[buffer_size];
    if (pread(fd, buffer, sizeof(buffer), off) == -1) {
        exit(1);
    }
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
    const int count = 4;
    const int interval = 30000;
    for (int off = 0; off < count * interval; off += interval) {
        threads.emplace_back(read_from_offset, off);
    }
    for (auto& t : threads) {
        t.join();
    }
    close(fd);
    return 0;
}
