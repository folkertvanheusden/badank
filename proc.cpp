#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <string>
#include <tuple>
#include <unistd.h>
#include <vector>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "error.h"
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

		chdir(dir.c_str());  // TODO log if fail

		close(0);

		dup(pipe_to_proc[0]);
		close(pipe_to_proc[1]);
		close(1);
		close(2);
		dup(pipe_from_proc[1]);
		int stderr = open("/dev/null", O_WRONLY);
		close(pipe_from_proc[0]);

		// TODO: a smarter way?
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
}

TextProgram::~TextProgram()
{
	close(r);
	close(w);

	for(int i=0; i<3; i++) {
		// TODO handle status (now nullptr)
		int rc = waitpid(pid, nullptr, WNOHANG);
		printf("%d: %d\n", pid, rc);
		if (rc == -1)
			error_exit(true, "waitpid failed");

		if (rc == pid)
			break;

		if (i == 0) {
			// TODO LOG
			kill(pid, SIGTERM);
			sleep(1);
		}
		else if (i == 1) {
			// TODO LOG
			kill(pid, SIGKILL);
			sleep(1);
		}
		else {
			// TODO log
		}
	}
}

std::optional<std::string> TextProgram::read(std::optional<int> timeout_ms)
{
	struct pollfd fds[] = { { r, POLLIN, 0 } };

	int use_to_ms = -1;

	if (timeout_ms.has_value())
		use_to_ms = timeout_ms.value();

	printf("Use time: %d\n", use_to_ms);

	uint64_t start_ms = get_ts_ms();

	std::string buffer;

	for(;;) {
		int64_t time_left = use_to_ms == -1 ? 86400000 : (start_ms + use_to_ms - get_ts_ms());
		printf("Time left: %ld\n", time_left);
		if (time_left < 0)
			break;

		int rc = poll(fds, 1, time_left);
		if (rc == 1) {
			char temp[2] { 0 };

			int rc = ::read(r, temp, 1);
			if (rc == 0)
				break;
			if (rc == -1) {
				// TODO log error
				break;
			}

			if (temp[0] == '\n')
				return buffer;

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
