// (C) 2021-2023 by Folkert van Heusden <mail@vanheusden.com>
// Released under MIT license

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <string>
#include <string.h>
#include <tuple>
#include <unistd.h>
#include <vector>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "error.h"
#include "log.h"
#include "proc.h"
#include "str.h"
#include "time.h"

int WRITE(int fd, const char *whereto, size_t len)
{
        ssize_t cnt=0;

        while(len > 0)
        {
                ssize_t rc = write(fd, whereto, len);
                if (rc <= 0)
                        return rc;

		whereto += rc;
		len -= rc;
		cnt += rc;
	}

	return cnt;
}

std::tuple<pid_t, int, int> exec_with_pipe(const std::string & command, const std::string & dir)
{
	int pipe_to_proc[2], pipe_from_proc[2];

	pipe(pipe_to_proc);
	pipe(pipe_from_proc);

	pid_t pid = fork();
	if (pid == 0) {
		setsid();

		if (dir.empty() == false && chdir(dir.c_str()) == -1)
			dolog(warning, "chdir to %s for %s failed: %s", dir.c_str(), command.c_str(), strerror(errno));

		close(0);

		dup(pipe_to_proc[0]);
		close(pipe_to_proc[1]);
		close(1);
		close(2);
		dup(pipe_from_proc[1]);
		open("/dev/null", O_WRONLY);
		close(pipe_from_proc[0]);

		int fd_max = sysconf(_SC_OPEN_MAX);
		for(int fd=3; fd<fd_max; fd++)
			close(fd);

		std::vector<std::string> parts = split(command, " ");

		size_t n_args = parts.size();
		char **pars = new char *[n_args + 1];
		for(size_t i=0; i<n_args; i++)
			pars[i] = (char *)parts.at(i).c_str();
		pars[n_args] = nullptr;

		if (execv(pars[0], &pars[0]) == -1)
			error_exit(true, "Failed to invoke %s", command.c_str());
	}

	close(pipe_to_proc[0]);
	close(pipe_from_proc[1]);

	std::tuple<pid_t, int, int> out(pid, pipe_to_proc[1], pipe_from_proc[0]);

	return out;
}

TextProgram::TextProgram(const std::string & command, const std::string & dir)
{
	auto prc = exec_with_pipe(command, dir);

	pid = std::get<0>(prc);
	w = std::get<1>(prc);
	r = std::get<2>(prc);

	dolog(debug, "Started \"%s\" with pid %d", command.c_str(), pid);
}

TextProgram::~TextProgram()
{
	write("quit");

	mymsleep(100);

	close(r);
	close(w);

	for(int i=0; i<3; i++) {
		int rc = waitpid(pid, nullptr, WNOHANG);

		if (rc == -1)
			error_exit(true, "waitpid failed");

		if (rc == pid)
			break;

		if (i == 0) {
			dolog(debug, "Sending SIGTERM to process %d", pid);
			kill(pid, SIGTERM);
			mymsleep(500);
		}
		else if (i == 1) {
			dolog(debug, "Sending SIGKILL to process %d", pid);
			kill(pid, SIGKILL);
			mymsleep(100);
		}
		else {
			dolog(warning, "Failed to terminate process %d", pid);
		}
	}
}

std::optional<std::string> TextProgram::read(std::optional<int> timeout_ms)
{
	struct pollfd fds[] = { { r, POLLIN, 0 } };

	int use_to_ms = -1;

	if (timeout_ms.has_value()) {
		use_to_ms = timeout_ms.value();

		dolog(debug, "%d] use time: %d", pid, use_to_ms);
	}

	uint64_t start_ms = get_ts_ms();

	std::string buffer;

	for(;;) {
		int64_t time_left = use_to_ms == -1 ? 86400000 : (start_ms + use_to_ms - get_ts_ms());
		if (timeout_ms.has_value())
			dolog(debug, "time left: %ld", time_left);
		if (time_left < 0)
			break;

		int rc = poll(fds, 1, time_left);
		if (rc == 1) {
			char temp[2] { 0 };

			int rc = ::read(r, temp, 1);
			if (rc == 0)
				break;
			if (rc == -1) {
				dolog(debug, "read error: %s", strerror(errno));
				break;
			}

			if (temp[0] == '\r') {
				// ignore
			}
			else if (temp[0] == '\n') {
				return buffer;
			}

			buffer += temp;
		}
	}

	return { };
}

bool TextProgram::write(const std::string & text)
{
	std::string work = text + "\n";

	return WRITE(w, work.c_str(), work.size()) == int(work.size());
}
