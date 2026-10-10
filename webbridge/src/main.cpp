// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "config.hpp"
#include "log.hpp"
#include "server.hpp"

void relay_init_logging(bool verbose);

#include <chiaki/common.h>

#include <csignal>
#include <cstdio>
#include <cstring>

#include <condition_variable>
#include <mutex>

static std::mutex quit_mutex;
static std::condition_variable quit_cv;
static bool quit = false;

static void sig_handler(int)
{
	{
		std::lock_guard<std::mutex> lock(quit_mutex);
		quit = true;
	}
	quit_cv.notify_all();
}

// Minimal dotenv: KEY=VALUE lines, '#' comments; does not override existing
// environment variables.
static void load_dotenv(const std::string &path)
{
	FILE *f = fopen(path.c_str(), "r");
	if(!f)
		return;
	char line[1024];
	while(fgets(line, sizeof(line), f))
	{
		char *s = line;
		while(*s == ' ' || *s == '\t')
			s++;
		if(*s == '#' || *s == '\n' || !*s)
			continue;
		char *eq = strchr(s, '=');
		if(!eq)
			continue;
		*eq = '\0';
		char *v = eq + 1;
		if(char *nl = strchr(v, '\n'))
			*nl = '\0';
		// strip optional quotes
		size_t vl = strlen(v);
		if(vl >= 2 && ((v[0] == '"' && v[vl - 1] == '"') || (v[0] == '\'' && v[vl - 1] == '\'')))
		{
			v[vl - 1] = '\0';
			v++;
		}
		setenv(s, v, 0);
	}
	fclose(f);
	printf("loaded environment from %s\n", path.c_str());
}

static void usage(const char *argv0)
{
	printf("usage: %s [options]\n"
			"  --http-port <port>   HTTP port for frontend + REST (default 9080)\n"
			"  --ws-port <port>     signaling WebSocket port (default 9081)\n"
			"  --bind <addr>        bind address (default 0.0.0.0)\n"
			"  --frontend <dir>     static frontend directory (default: ./frontend next to binary)\n"
			"  --config <file>      config path (default ~/.config/chiaki-web/config.json)\n"
			"  --env <file>         dotenv file (default: ./.env, then <binary dir>/.env)\n"
			"  --verbose            debug logging\n",
			argv0);
}

int main(int argc, char **argv)
{
	ServerOptions options;
	std::string config_path;
	std::string env_path;
	bool verbose = false;
	for(int i = 1; i < argc; i++)
	{
		auto arg = [&](const char *name) -> const char * {
			if(strcmp(argv[i], name) == 0 && i + 1 < argc)
				return argv[++i];
			return nullptr;
		};
		if(const char *v = arg("--http-port"))
			options.http_port = (uint16_t)atoi(v);
		else if(const char *v = arg("--ws-port"))
			options.ws_port = (uint16_t)atoi(v);
		else if(const char *v = arg("--bind"))
			options.bind_address = v;
		else if(const char *v = arg("--frontend"))
			options.frontend_dir = v;
		else if(const char *v = arg("--config"))
			config_path = v;
		else if(const char *v = arg("--env"))
			env_path = v;
		else if(strcmp(argv[i], "--verbose") == 0)
		{
			bridge_log_set_verbose(true);
			verbose = true;
		}
		else
		{
			usage(argv[0]);
			return strcmp(argv[i], "--help") == 0 ? 0 : 1;
		}
	}

	ChiakiErrorCode err = chiaki_lib_init();
	if(err != CHIAKI_ERR_SUCCESS)
	{
		fprintf(stderr, "chiaki lib init failed: %s\n", chiaki_error_string(err));
		return 1;
	}

	relay_init_logging(verbose);

	// directory containing the binary (for default frontend dir and .env)
	std::string bin_dir;
	{
		char self[4096];
		ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
		if(n > 0)
		{
			self[n] = '\0';
			if(char *slash = strrchr(self, '/'))
			{
				*slash = '\0';
				bin_dir = self;
			}
		}
	}
	if(options.frontend_dir == "frontend" && !bin_dir.empty())
		options.frontend_dir = bin_dir + "/frontend";

	if(!env_path.empty())
		load_dotenv(env_path);
	else
	{
		load_dotenv(".env");
		if(!bin_dir.empty())
			load_dotenv(bin_dir + "/.env");
	}

	Config config(config_path);
	config.load();

	BridgeServer server(options, config);
	if(!server.start())
		return 1;

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);
	signal(SIGPIPE, SIG_IGN);

	CHIAKI_LOGI(bridge_log(), "webbridge: ready — open http://localhost:%u/", options.http_port);
	{
		std::unique_lock<std::mutex> lock(quit_mutex);
		quit_cv.wait(lock, [] { return quit; });
	}
	CHIAKI_LOGI(bridge_log(), "webbridge: shutting down");
	server.stop();
	return 0;
}
