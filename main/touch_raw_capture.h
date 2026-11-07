#pragma once

#include <stdio.h>

namespace TouchRawCapture {

// S3 3.5B only. Capture is paused in results while the buffer is exported.
bool start();
void stop();
void clear();
bool writeCsvComments(FILE *file);

}
