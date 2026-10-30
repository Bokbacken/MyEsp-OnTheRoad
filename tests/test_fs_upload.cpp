#include "check.h"
#include "fs_upload.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

static bool writeFile(const std::string &path, const char *contents)
{
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) return false;
    const size_t n = strlen(contents);
    const bool wrote = fwrite(contents, 1, n, f) == n;
    return fclose(f) == 0 && wrote;
}

static std::string readFile(const std::string &path)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return "";
    char buf[64] = {};
    const size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    return std::string(buf, n);
}

int main()
{
    char directory[] = "/tmp/fs-upload-test-XXXXXX";
    CHECK(mkdtemp(directory) != nullptr);
    const std::string dir = directory;
    const std::string destination = dir + "/" + std::string(87, 'x');
    char temporary[300], backup[300], boundaryPath[300];
    CHECK(FsUpload::siblingPath(destination.c_str(), 0x1234, false, temporary, sizeof(temporary)));
    CHECK(FsUpload::siblingPath(destination.c_str(), 0x1234, true, backup, sizeof(backup)));
    CHECK(strlen(temporary) < strlen(destination.c_str()) + 5);
    CHECK(strstr(temporary, ".part") != nullptr);
    CHECK(FsUpload::requestUriFits(14, 20, 36));
    CHECK(!FsUpload::requestUriFits(14, 20, 35));
    CHECK(FsUpload::requestUriFits(14, 1008, 1024));
    CHECK(!FsUpload::requestUriFits(14, 1009, 1024));

    std::string boundaryDir = "/" + std::string(282, 'd');
    const std::string boundaryDestination = boundaryDir + "/file";
    CHECK(FsUpload::siblingPath(boundaryDestination.c_str(), 1, false, boundaryPath, 300));
    boundaryDir += "d";
    const std::string tooLongDestination = boundaryDir + "/file";
    CHECK(!FsUpload::siblingPath(tooLongDestination.c_str(), 1, false, boundaryPath, 300));

    CHECK(writeFile(destination, "old"));
    CHECK(writeFile(temporary, "partial"));
    CHECK(!FsUpload::install(temporary, destination.c_str(), backup, false));
    CHECK(errno == EEXIST);
    CHECK(readFile(destination) == "old");
    FsUpload::discard(temporary);   // Aborted upload: discard staging data only.
    CHECK(readFile(destination) == "old");

    CHECK(writeFile(temporary, "new"));
    CHECK(FsUpload::install(temporary, destination.c_str(), backup, true));
    CHECK(readFile(destination) == "new");
    CHECK(access(backup, F_OK) != 0);

    CHECK(writeFile(destination, "preserve"));
    CHECK(!FsUpload::install((dir + "/missing").c_str(), destination.c_str(), backup, true));
    CHECK(readFile(destination) == "preserve");
    CHECK(access(backup, F_OK) != 0);

    unlink(destination.c_str());
    rmdir(directory);
    CHECK_DONE();
}
