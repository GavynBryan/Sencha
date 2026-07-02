#pragma once

#include <string>
#include <vector>

// Spawns a detached child process. args are the arguments after argv[0];
// workingDir, when non-empty, becomes the child's working directory. On
// success outPid holds the child's pid (as long, keeping platform process
// headers out of editor headers). The caller owns reaping (waitpid) if it
// wants exit status; an unreaped child is adopted by init at parent exit.
// POSIX-only; other hosts fail with an error.
bool SpawnProcess(const std::string& executablePath,
                  const std::vector<std::string>& args,
                  const std::string& workingDir,
                  long& outPid,
                  std::string* error);

// Non-blocking exit check for a pid this process spawned; reaps the child
// when it has exited. An unknown/already-reaped pid reports exited.
bool HasProcessExited(long pid);
