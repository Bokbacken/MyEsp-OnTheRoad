// app_web_files.h - web file browser API (/api/fs/*, /api/sd/remount).
#pragma once

namespace AppWebFiles {

bool registerRoutes();   // Called from AppWeb::begin()
bool busy();            // File transfers or SD formatting in progress

}  // namespace AppWebFiles
