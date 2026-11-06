"""Exercise production recursive deletion, including cooperative cancellation."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "main/app_web_files.cpp").read_text()
body = source.split("struct DeleteProgress {", 1)[1].split("\nconst char *errName", 1)[0]
code = r'''
#include <atomic>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <filesystem>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
constexpr int TREE_DEPTH=16;
constexpr size_t PATH_CAP=300;
static void (*onLock)() = nullptr;
struct Lock { explicit Lock(bool) { if(onLock) onLock(); } };
void *psAlloc(size_t n) { return malloc(n); }
void vTaskDelay(int) {}
namespace BoardSd { bool healthy=false; bool transferHealthy() { return healthy; } }
struct DeleteProgress {
''' + body + r'''
int main(int argc, char **argv) {
    assert(argc==2);
    const std::string root=argv[1];
    const std::string tree=root+"/tree";
    std::filesystem::create_directories(tree+"/child");
    for(int i=0;i<80;i++) {
        FILE *f=fopen((tree+"/child/"+std::to_string(i)).c_str(),"wb");
        assert(f); fputs("data",f); fclose(f);
    }
    DeleteProgress progress;
    progress.cancel=true;
    assert(removeTree(false,tree.c_str(),0,&progress)==ECANCELED);
    assert(progress.removed==0);
    assert(std::filesystem::exists(tree+"/child/0"));
    progress.cancel=false;
    assert(removeTree(true,tree.c_str(),0,&progress)==EIO);
    assert(progress.removed==0);
    static DeleteProgress *current=&progress;
    onLock=[] { if(current->removed>=5) current->cancel=true; };
    assert(removeTree(false,tree.c_str(),0,&progress)==ECANCELED);
    assert(progress.removed==5);
    assert(std::filesystem::exists(tree));
    assert(std::distance(std::filesystem::directory_iterator(tree+"/child"),
                         std::filesystem::directory_iterator{})==75);
    onLock=nullptr;
    DeleteProgress resumed;
    assert(removeTree(false,tree.c_str(),0,&resumed)==0);
    assert(resumed.removed==77); // 75 files plus both directories.
    assert(!std::filesystem::exists(tree));
    assert(removeTree(false,tree.c_str(),0,&resumed)==ENOENT);
    assert(removeTree(false,root.c_str(),TREE_DEPTH+1,&resumed)==ENOTEMPTY);
    puts("Recursive delete: pre-cancel, partial cancellation, counters and restart passed");
}
'''
with tempfile.TemporaryDirectory(prefix="otr-delete-") as directory:
    work = Path(directory)
    cpp = work / "test.cpp"
    cpp.write_text(code)
    binary = work / "test"
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary), directory], check=True)

assert 'MyWeb::on("/api/files/delete/cancel", HTTP_POST, handleDeleteStatus)' in source
status = source.split("static esp_err_t handleDeleteStatus", 1)[1].split("static esp_err_t", 1)[0]
assert status.index("adminAllowed()") < status.index("progress.cancel.store(true)")
assert "strcmp(id, s_deleteJob.id)" in status
assert 's_deleteJob.finished.load()' in status
assert 'BoardSd::Transfer transfer(t->sd)' in source
