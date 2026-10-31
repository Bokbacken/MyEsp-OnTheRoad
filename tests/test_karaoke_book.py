"""Exercise the production plain-lyrics frame sender with synthetic clients."""
import json
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class PlainLyricsTest(unittest.TestCase):
    def test_frames(self):
        source = (ROOT / "main/kara_web.cpp").read_text()
        sender = source[source.index("void sendPlainLyrics"):
                        source.index("void senderTask")]
        harness = r'''
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include "kara_stream.h"
using SemaphoreHandle_t = void *;
constexpr int portMAX_DELAY=0, HTTPD_WS_TYPE_TEXT=1;
SemaphoreHandle_t s_bookMux = reinterpret_cast<void *>(1);
std::string s_plainLyrics;
char s_json[KaraStream::JSON_CAP];
#define ESP_LOGE(...) ((void)0)
void xSemaphoreTake(void *,int) {}
void xSemaphoreGive(void *) {}
bool sameSongSeq(uint32_t seq) { return seq==7; }
bool sendFrame(int fd,int type,const char *text,size_t n) {
    if(fd!=9 || type!=HTTPD_WS_TYPE_TEXT) return false;
    fwrite(text,1,n,stdout); putchar('\n'); return true;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            cpp = pathlib.Path(directory) / "book.cpp"
            exe = pathlib.Path(directory) / "book"
            cpp.write_text(harness + sender + r'''
int main() {
    sendPlainLyrics(9,6); // stale song sends nothing
    s_plainLyrics="First line\n\n";
    for(int i=0;i<220;i++) s_plainLyrics+="åäö \"quoted\"\\path\n";
    sendPlainLyrics(9,7);
}
''')
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-I" + str(ROOT / "main"),
                            "-I" + str(ROOT / "components/otr_midi/include"),
                            str(cpp), str(ROOT / "main/kara_stream.cpp"),
                            str(ROOT / "components/otr_midi/otr_midi.cpp"),
                            "-o", str(exe)], check=True)
            result = subprocess.run([str(exe)], check=True, text=True, capture_output=True)
            frames = [json.loads(line) for line in result.stdout.splitlines()]
            self.assertEqual(frames[0], {"type": "bookstart", "plain": True})
            self.assertGreater(len(frames), 2)
            self.assertEqual("".join(frame["text"] for frame in frames[1:]),
                             "First line\n\n" + 'åäö "quoted"\\path\n' * 220)


if __name__ == "__main__":
    unittest.main()
