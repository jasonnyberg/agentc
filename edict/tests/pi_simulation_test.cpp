#include <sys/wait.h>
#include <gtest/gtest.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <iostream>
#include <fstream>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <chrono>
#include <thread>

TEST(PiSimulationTest, MiniKanrenLogicExample) {
    const char* inPipe = "/tmp/agentc_in.pipe";
    const char* outPipe = "/tmp/agentc_out.pipe";
    unlink(inPipe);
    unlink(outPipe);
    mkfifo(inPipe, 0666);
    mkfifo(outPipe, 0666);

    pid_t pid = fork();
    if (pid == 0) {
        char path[PATH_MAX];
        realpath("./edict/edict", path);
        execl(path, "edict", "--ipc", inPipe, outPipe, (char*)NULL);
        exit(1);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    int fdIn = open(inPipe, O_WRONLY);
    int fdOut = open(outPipe, O_RDONLY);
    
    // MiniKanren query: membero(q, [tea, cake])
    // The Edict VM interprets this via the `logicffi.agentc_logic_eval_ltv` thunk.
    std::string query = "{\"fresh\": [\"q\"], \"where\": [[\"membero\", \"q\", [\"tea\", \"cake\"]]], \"results\": [\"q\"]} logicffi.agentc_logic_eval_ltv !\n";
    write(fdIn, query.c_str(), query.size());
    write(fdIn, "exit\n", 5);

    char buffer[4096];
    bool success = false;
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(5)) {
        ssize_t bytes = read(fdOut, buffer, sizeof(buffer) - 1);
        if (bytes > 0) {
            buffer[bytes] = '\0';
            if (std::string(buffer).find("tea") != std::string::npos && std::string(buffer).find("cake") != std::string::npos) {
                success = true;
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    close(fdIn);
    close(fdOut);
    waitpid(pid, nullptr, 0);
    unlink(inPipe);
    unlink(outPipe);

    EXPECT_TRUE(success) << "Failed to get expected MiniKanren result";
}
