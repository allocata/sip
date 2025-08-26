// sip - https://github.com/allocata/sip

#include <getopt.h>
#ifdef _WIN32
    #include <windows.h>
    #include <process.h>
#else
    #include <sys/wait.h>
    #include <unistd.h>
#endif

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <random>
#include <string>
#include <vector>

const char* PROGRAM_NAME = "sip";
const char* PROGRAM_VERSION = "1.0.0";
const int DEFAULT_TIMEOUT = 10;

// globals
static bool opt_verbose = false;
static bool opt_quiet = false;
static int opt_timeout = DEFAULT_TIMEOUT;
static std::string opt_output_dir = "./";
static std::string opt_branch = "";

static std::string rtrim(const std::string& str) {
    auto end = str.find_last_not_of(" \n\r\t");
    return end == std::string::npos ? "" : str.substr(0, end + 1);
}

static std::string dev_null() {
#ifdef _WIN32
    return " >nul 2>&1";
#else
    return " >/dev/null 2>&1";
#endif
}

static int exit_status_of(int rc) {
    if (rc == -1)
        return -1;
#ifdef _WIN32
    return rc;
#else
    if (WIFEXITED(rc))
        return WEXITSTATUS(rc);
    if (WIFSIGNALED(rc))
        return 128 + WTERMSIG(rc);
    return rc;
#endif
}

static bool looks_like_commit_sha(const std::string& ref) {
    return ref.length() >= 6 && ref.length() <= 64 && 
           ref.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos;
}

static bool path_available_for_write(const std::string& p) {
    std::filesystem::path x(p);
    if (std::filesystem::exists(x)) {
        std::fprintf(stderr, "%s: destination exists: %s\n", PROGRAM_NAME, p.c_str());
        return false;
    }
    auto dir = x.parent_path();
    if (!dir.empty() && !std::filesystem::exists(dir)) {
        std::error_code ec;
        if (!std::filesystem::create_directories(dir, ec)) {
            std::fprintf(stderr, "%s: mkdir failed: %s\n", PROGRAM_NAME, ec.message().c_str());
            return false;
        }
    }
    return true;
}

static bool parse_github_url(const std::string& url_or_repo, std::string& owner, std::string& repo, std::string& path, std::string& branch) {
    // Handle GitHub URLs like:
    // https://github.com/owner/repo
    // https://github.com/owner/repo/tree/branch/path
    // https://github.com/owner/repo/blob/branch/path
    // https://github.com/owner/repo.git
    // owner/repo
    // owner/repo/path
    
    std::string input = url_or_repo;
    
    const std::string github_prefix = "https://github.com/";
    if (input.rfind(github_prefix, 0) == 0) {
        input = input.substr(github_prefix.length());
    }
    
    if (input.length() > 4 && input.substr(input.length() - 4) == ".git") {
        input = input.substr(0, input.length() - 4);
        // Handle trailing slash after .git
        if (!input.empty() && input.back() == '/') {
            input.pop_back();
        }
    }
    
    std::size_t first_slash = input.find('/');
    if (first_slash == std::string::npos) {
        return false;
    }
    
    owner = input.substr(0, first_slash);
    
    std::size_t second_slash = input.find('/', first_slash + 1);
    if (second_slash == std::string::npos) {
        repo = input.substr(first_slash + 1);
        path = "";
        branch = "";
        return true;
    }
    
    repo = input.substr(first_slash + 1, second_slash - first_slash - 1);
    std::string remainder = input.substr(second_slash + 1);
    
    if (remainder.rfind("tree/", 0) == 0) {
        remainder = remainder.substr(5); // Remove "tree/"
        std::size_t next_slash = remainder.find('/');
        if (next_slash != std::string::npos) {
            branch = remainder.substr(0, next_slash);
            path = remainder.substr(next_slash + 1);
        } else {
            branch = remainder;
            path = "";
        }
    } else if (remainder.rfind("blob/", 0) == 0) {
        remainder = remainder.substr(5); // Remove "blob/"
        std::size_t next_slash = remainder.find('/');
        if (next_slash != std::string::npos) {
            branch = remainder.substr(0, next_slash);
            path = remainder.substr(next_slash + 1);
        } else {
            branch = remainder;
            path = "";
        }
    } else {
        // treat as plain path
        path = remainder;
        branch = "";
    }
    
    return true;
}

void usage(int status) {
    if (status != EXIT_SUCCESS) {
        std::fprintf(stderr, "Try '%s --help' for more information.\n", PROGRAM_NAME);
    } else {
        std::printf("Usage: %s [OPTION]... OWNER/REPO [PATH]\n", PROGRAM_NAME);
        std::printf("Download files and directories from GitHub repositories.\n\n");
        std::printf("Options:\n");
        std::printf("  -o, --output-dir=DIR     write output to DIR\n");
        std::printf(
            "  -b, --branch=REF         branch, tag, or commit (auto-detected if not specified)\n");
        std::printf("  -t, --timeout=SECONDS    download timeout (default: 10)\n");
        std::printf("  -q, --quiet              suppress output\n");
        std::printf("  -v, --verbose            verbose output (conflicts with --quiet)\n");
        std::printf("      --help               show this help\n");
        std::printf("      --version            show version\n\n");
        std::printf("Environment:\n");
        std::printf("  GITHUB_TOKEN             authenticate with private repositories\n\n");
        std::printf("Examples:\n");
        std::printf("  sip https://github.com/torvalds/linux/tree/master/LICENSES\n");
    }
    std::exit(status);
}

void print_version(void) {
    printf("%s %s\n", PROGRAM_NAME, PROGRAM_VERSION);
    printf("git clone alternative - MIT License\n");
    exit(EXIT_SUCCESS);
}

std::string quote_arg(const std::string& str) {
#ifdef _WIN32
    std::string quoted = "\"";
    for (char c : str) {
        if (c == '"')
            quoted += "\\\"";
        else if (c == '\\')
            quoted += "\\\\";
        else
            quoted += c;
    }
    quoted += "\"";
    return quoted;
#else
    std::string quoted = "'";
    for (char c : str) {
        if (c == '\'')
            quoted += "'\"'\"'";
        else
            quoted += c;
    }
    quoted += "'";
    return quoted;
#endif
}

std::string create_temp_dir() {
    std::filesystem::path temp_base = std::filesystem::temp_directory_path();
    
#ifdef _WIN32
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(100000, 999999);
    
    std::string temp_name = "sip_" + std::to_string(_getpid()) + "_" + std::to_string(dis(gen));
    std::filesystem::path temp_path = temp_base / temp_name;
    
    std::error_code ec;
    if (!std::filesystem::create_directory(temp_path, ec)) {
        return "";
    }
    return temp_path.string();
#else
    std::string template_path = (temp_base / "sip_XXXXXX").string();
    
    // mkdtemp requires a mutable C string
    std::vector<char> template_cstr(template_path.begin(), template_path.end());
    template_cstr.push_back('\0');
    
    char* result = mkdtemp(template_cstr.data());
    return result ? std::string(result) : "";
#endif
}

std::string discover_default_branch(const std::string& owner, const std::string& repo) {
    const char* token = std::getenv("GITHUB_TOKEN");
    std::string url = "https://github.com/" + owner + "/" + repo;
    std::string auth = token ? (" -c http.extraHeader=" + 
                              quote_arg(std::string("Authorization: Bearer ") + token)) : "";
    std::string cmd = "git" + auth + " ls-remote --symref " + quote_arg(url) + " HEAD";
    
    if (!opt_verbose) {
        cmd += dev_null();
    }
    
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe)
        return "main";

    char buf[512];
    std::string first;
    if (fgets(buf, sizeof(buf), pipe)) {
        first = buf;
    }
    pclose(pipe);

    // look for "ref: refs/heads/BRANCH" at the beginning
    if (first.rfind("ref:", 0) == 0) {
        auto pos = first.find("refs/heads/");
        if (pos != std::string::npos) {
            pos += 11;  // length of "refs/heads/"
            auto end = first.find_first_of(" \t\n", pos);
            std::string branch = first.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
            branch = rtrim(branch);
            return branch.empty() ? "main" : branch;
        }
    }

    return "main";
}

bool check_dependencies(void) {
#ifdef _WIN32
    bool curl_ok = (system("curl --version >nul 2>&1") == 0);
    bool git_ok = (system("git --version >nul 2>&1") == 0);
#else
    bool curl_ok = (system("curl --version >/dev/null 2>&1") == 0);
    bool git_ok = (system("git --version >/dev/null 2>&1") == 0);
#endif

    if (!curl_ok) {
        fprintf(stderr, "%s: curl not found\n", PROGRAM_NAME);
        return false;
    }
    if (!git_ok) {
        fprintf(stderr, "%s: git not found\n", PROGRAM_NAME);
        return false;
    }
    return true;
}

// sparse checkout is way faster for big repos
bool download_directory_selective(const std::string& owner,
                                  const std::string& repo,
                                  const std::string& path,
                                  const std::string& output) {
    if (!opt_quiet)
        std::printf("Downloading directory '%s'...\n", path.c_str());

    if (!path_available_for_write(output)) return false;

    std::string temp_dir = create_temp_dir();
    if (temp_dir.empty()) {
        std::fprintf(stderr, "%s: failed to create temp directory\n", PROGRAM_NAME);
        return false;
    }

    std::string git_url = "https://github.com/" + owner + "/" + repo + ".git";
    
    const char* token = std::getenv("GITHUB_TOKEN");
    std::string auth_config = token ? ("-c http.extraHeader=" + 
                                      quote_arg(std::string("Authorization: Bearer ") + token) + " ") : "";
    
    std::string clone_cmd = "GIT_TERMINAL_PROMPT=0 git " + auth_config + 
                           "-c http.lowSpeedLimit=1000 -c http.lowSpeedTime=10 " +
                           "clone --filter=blob:none --no-checkout --depth 1 ";
    if (!opt_quiet)
        clone_cmd += "--progress ";
    clone_cmd += quote_arg(git_url) + " " + quote_arg(temp_dir);
    if (!opt_verbose) clone_cmd += dev_null();

    if (opt_verbose)
        std::fprintf(stderr, "%s: cloning repository...\n", PROGRAM_NAME);
    
    int result = std::system(clone_cmd.c_str());
    if (result != 0) {
        int exit_code = exit_status_of(result);
        std::fprintf(stderr, "%s: clone failed (exit %d)\n", PROGRAM_NAME, exit_code);
        std::filesystem::remove_all(temp_dir);
        return false;
    }

    std::string sparse_init_cmd = "GIT_TERMINAL_PROMPT=0 git -C " + quote_arg(temp_dir) + 
                                 " sparse-checkout init --cone";
    if (!opt_verbose) sparse_init_cmd += dev_null();

    if (opt_verbose)
        std::fprintf(stderr, "%s: initializing sparse checkout...\n", PROGRAM_NAME);
    
    result = std::system(sparse_init_cmd.c_str());
    if (result != 0) {
        int exit_code = exit_status_of(result);
        std::fprintf(stderr, "%s: sparse-checkout init failed (exit %d)\n", PROGRAM_NAME, exit_code);
        std::filesystem::remove_all(temp_dir);
        return false;
    }

    std::string sparse_set_cmd = "GIT_TERMINAL_PROMPT=0 git -C " + quote_arg(temp_dir) + 
                                " sparse-checkout set -- " + quote_arg(path);
    if (!opt_verbose) sparse_set_cmd += dev_null();

    if (opt_verbose)
        std::fprintf(stderr, "%s: setting sparse checkout pattern...\n", PROGRAM_NAME);
    
    result = std::system(sparse_set_cmd.c_str());
    if (result != 0) {
        int exit_code = exit_status_of(result);
        std::fprintf(stderr, "%s: sparse-checkout set failed (exit %d)\n", PROGRAM_NAME, exit_code);
        std::filesystem::remove_all(temp_dir);
        return false;
    }

    if (!opt_branch.empty()) {
        if (opt_verbose)
            std::fprintf(stderr, "%s: fetching reference '%s'...\n", PROGRAM_NAME, opt_branch.c_str());
        
        // try tag first
        std::string fetch_tag_cmd = "GIT_TERMINAL_PROMPT=0 git -C " + quote_arg(temp_dir) + " " + auth_config + 
                                   "fetch --depth 1 origin tag " + quote_arg(opt_branch);
        if (!opt_verbose) fetch_tag_cmd += dev_null();
        
        result = std::system(fetch_tag_cmd.c_str());
        if (result != 0) {
            // try branch
            std::string fetch_branch_cmd = "GIT_TERMINAL_PROMPT=0 git -C " + quote_arg(temp_dir) + " " + auth_config + 
                                          "fetch --depth 1 origin " + quote_arg(opt_branch) + ":" + quote_arg(opt_branch);
            if (!opt_verbose) fetch_branch_cmd += dev_null();
            
            result = std::system(fetch_branch_cmd.c_str());
            if (result != 0 && looks_like_commit_sha(opt_branch)) {
                // try direct SHA fetch
                std::string fetch_sha_cmd = "GIT_TERMINAL_PROMPT=0 git -C " + quote_arg(temp_dir) + " " + auth_config + 
                                           "fetch --depth 1 origin " + quote_arg(opt_branch);
                if (!opt_verbose) fetch_sha_cmd += dev_null();
                
                result = std::system(fetch_sha_cmd.c_str());
            }
        }
        
        if (result != 0) {
            int exit_code = exit_status_of(result);
            std::fprintf(stderr, "%s: fetch failed for '%s' (exit %d)\n", PROGRAM_NAME, opt_branch.c_str(), exit_code);
            std::filesystem::remove_all(temp_dir);
            return false;
        }

        std::string checkout_cmd = "GIT_TERMINAL_PROMPT=0 git -C " + quote_arg(temp_dir) + " checkout " + quote_arg(opt_branch);
        if (!opt_verbose) checkout_cmd += dev_null();
        
        if (opt_verbose)
            std::fprintf(stderr, "%s: checking out '%s'...\n", PROGRAM_NAME, opt_branch.c_str());
        
        result = std::system(checkout_cmd.c_str());
        if (result != 0) {
            int exit_code = exit_status_of(result);
            std::fprintf(stderr, "%s: checkout failed for '%s' (exit %d)\n", PROGRAM_NAME, opt_branch.c_str(), exit_code);
            std::filesystem::remove_all(temp_dir);
            return false;
        }
    } else {
        // No specific branch - just checkout default with sparse rules
        std::string checkout_cmd = "GIT_TERMINAL_PROMPT=0 git -C " + quote_arg(temp_dir) + " checkout";
        if (!opt_verbose) checkout_cmd += dev_null();
        
        if (opt_verbose)
            std::fprintf(stderr, "%s: checking out default branch...\n", PROGRAM_NAME);
        
        result = std::system(checkout_cmd.c_str());
        if (result != 0) {
            int exit_code = exit_status_of(result);
            std::fprintf(stderr, "%s: checkout failed (exit %d)\n", PROGRAM_NAME, exit_code);
            std::filesystem::remove_all(temp_dir);
            return false;
        }
    }

    std::filesystem::path src_path = std::filesystem::path(temp_dir) / path;
    std::filesystem::path dest_path = std::filesystem::current_path() / output;
    
    if (opt_verbose)
        std::fprintf(stderr, "%s: copying files...\n", PROGRAM_NAME);
    
    std::error_code copy_ec;
    std::filesystem::copy(src_path, dest_path, 
                         std::filesystem::copy_options::recursive | 
                         std::filesystem::copy_options::overwrite_existing, 
                         copy_ec);

    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
    if (ec && opt_verbose) {
        std::fprintf(stderr, "%s: warning: failed to remove temp dir: %s\n", PROGRAM_NAME,
                     ec.message().c_str());
    }

    if (copy_ec) {
        std::fprintf(stderr, "%s: copy failed: %s\n", PROGRAM_NAME, copy_ec.message().c_str());
        return false;
    }

    if (!opt_quiet)
        std::puts("done.");
    return true;
}

bool download_file(const std::string& owner,
                   const std::string& repo,
                   const std::string& path,
                   const std::string& output) {
    if (!opt_quiet)
        std::printf("Downloading '%s'...\n", path.c_str());

    if (!path_available_for_write(output)) return false;

    std::string ref = opt_branch;
    if (ref.empty()) {
        if (opt_verbose)
            std::fprintf(stderr, "%s: discovering default branch...\n", PROGRAM_NAME);
        ref = discover_default_branch(owner, repo);
        if (opt_verbose)
            std::fprintf(stderr, "%s: using default branch: %s\n", PROGRAM_NAME, ref.c_str());
    }

    std::string url =
        "https://raw.githubusercontent.com/" + owner + "/" + repo + "/" + ref + "/" + path;

    std::string cmd = "curl ";
    if (!opt_quiet)
        cmd += "--progress-bar ";
    else
        cmd += "-s ";
    
    const char* token = std::getenv("GITHUB_TOKEN");
    if (token) {
        cmd += "-H " + quote_arg(std::string("Authorization: Bearer ") + token) + " ";
    }
    
    cmd += "-f -L --retry 3 --retry-all-errors --retry-delay 1 --max-time " +
           std::to_string(opt_timeout) + " -o " + quote_arg(output) + " " + quote_arg(url);

    if (!opt_verbose) {
        cmd += dev_null();
    }

    if (opt_verbose)
        std::fprintf(stderr, "%s: %s\n", PROGRAM_NAME, cmd.c_str());

    int result = std::system(cmd.c_str());
    if (result == 0) {
        if (!opt_quiet)
            std::puts("done.");
        return true;
    }

    int exit_code = exit_status_of(result);
    if (exit_code == 22) {
        std::fprintf(stderr, "%s: file not found (check path/branch)\n", PROGRAM_NAME);
    } else {
        std::fprintf(stderr, "%s: download failed (exit %d)\n", PROGRAM_NAME, exit_code);
    }
    return false;
}

bool clone_repository(const std::string& owner,
                      const std::string& repo,
                      const std::string& /* path */,
                      const std::string& output) {
    if (!opt_quiet)
        std::printf("Cloning into '%s'...\n", output.c_str());

    if (!path_available_for_write(output)) return false;

    std::string url = "https://github.com/" + owner + "/" + repo + ".git";
    
    const char* token = std::getenv("GITHUB_TOKEN");
    std::string auth_config = token ? ("-c http.extraHeader=" + 
                                      quote_arg(std::string("Authorization: Bearer ") + token) + " ") : "";
    
    bool is_sha = !opt_branch.empty() && looks_like_commit_sha(opt_branch);
    
    std::string cmd = "GIT_TERMINAL_PROMPT=0 git " + auth_config + 
                     "-c http.lowSpeedLimit=1000 -c http.lowSpeedTime=10 clone --depth 1 ";

    if (!opt_quiet)
        cmd += "--progress ";
    
    // don't use --branch with commit SHAs
    if (!opt_branch.empty() && !is_sha)
        cmd += "--branch " + quote_arg(opt_branch) + " ";
    
    cmd += quote_arg(url) + " " + quote_arg(output);

    if (!opt_verbose)
        cmd += dev_null();

    if (opt_verbose)
        std::fprintf(stderr, "%s: %s\n", PROGRAM_NAME, cmd.c_str());

    int result = std::system(cmd.c_str());
    if (result != 0) {
        int exit_code = exit_status_of(result);
        if (exit_code == 128) {
            std::fprintf(stderr, "%s: repo not found or private\n", PROGRAM_NAME);
        } else {
            std::fprintf(stderr, "%s: clone failed (exit %d)\n", PROGRAM_NAME, exit_code);
        }
        return false;
    }

    if (is_sha) {
        std::string sha_cmd = "GIT_TERMINAL_PROMPT=0 git -C " + quote_arg(output) + " " + auth_config +
                             "fetch --depth 1 origin " + quote_arg(opt_branch);
        if (!opt_verbose) sha_cmd += dev_null();
        
        if (opt_verbose)
            std::fprintf(stderr, "%s: %s\n", PROGRAM_NAME, sha_cmd.c_str());
        
        result = std::system(sha_cmd.c_str());
        if (result != 0) {
            int exit_code = exit_status_of(result);
            std::fprintf(stderr, "%s: failed to fetch commit %s (exit %d)\n", 
                        PROGRAM_NAME, opt_branch.c_str(), exit_code);
            return false;
        }

        std::string checkout_cmd = "GIT_TERMINAL_PROMPT=0 git -C " + quote_arg(output) + " checkout " + quote_arg(opt_branch);
        if (!opt_verbose) checkout_cmd += dev_null();
        
        if (opt_verbose)
            std::fprintf(stderr, "%s: %s\n", PROGRAM_NAME, checkout_cmd.c_str());
        
        result = std::system(checkout_cmd.c_str());
        if (result != 0) {
            int exit_code = exit_status_of(result);
            std::fprintf(stderr, "%s: failed to checkout commit %s (exit %d)\n", 
                        PROGRAM_NAME, opt_branch.c_str(), exit_code);
            return false;
        }
    }

    if (!opt_quiet)
        std::puts("done.");
    return true;
}

int main(int argc, char** argv) {
    if (!check_dependencies()) {
        std::exit(EXIT_FAILURE);
    }

    static const struct option long_options[] = {{"output-dir", required_argument, nullptr, 'o'},
                                                 {"branch", required_argument, nullptr, 'b'},
                                                 {"timeout", required_argument, nullptr, 't'},
                                                 {"quiet", no_argument, nullptr, 'q'},
                                                 {"verbose", no_argument, nullptr, 'v'},
                                                 {"help", no_argument, nullptr, 'h'},
                                                 {"version", no_argument, nullptr, 'V'},
                                                 {nullptr, 0, nullptr, 0}};

    int c;
    while ((c = getopt_long(argc, argv, "o:b:t:qvhV", long_options, nullptr)) != -1) {
        switch (c) {
            case 'o':
                opt_output_dir = optarg;
                break;
            case 'b':
                opt_branch = optarg;
                break;
            case 't': {
                char* endptr;
                long timeout = std::strtol(optarg, &endptr, 10);
                if (*endptr != '\0' || timeout <= 0 || timeout > INT_MAX) {
                    std::fprintf(stderr, "%s: bad timeout '%s'\n", PROGRAM_NAME, optarg);
                    std::exit(EXIT_FAILURE);
                }
                opt_timeout = static_cast<int>(timeout);
            } break;
            case 'q':
                opt_quiet = true;
                break;
            case 'v':
                opt_verbose = true;
                break;
            case 'h':
                usage(EXIT_SUCCESS);
                break;
            case 'V':
                print_version();
                break;
            default:
                usage(EXIT_FAILURE);
        }
    }

    if (opt_quiet && opt_verbose) {
        std::fprintf(stderr, "%s: --quiet and --verbose are mutually exclusive\n", PROGRAM_NAME);
        std::exit(EXIT_FAILURE);
    }

    if (optind >= argc) {
        std::fprintf(stderr, "%s: missing repository\n", PROGRAM_NAME);
        usage(EXIT_FAILURE);
    }

    std::string repo_arg = argv[optind++];

    std::string owner, repo, path, url_branch;

    if (!parse_github_url(repo_arg, owner, repo, path, url_branch)) {
        std::fprintf(stderr, "%s: invalid GitHub URL or repo format\n", PROGRAM_NAME);
        std::exit(EXIT_FAILURE);
    }
    
    // if branch was extracted from URL and no -b option given, use URL branch
    // but skip common default branch names that should be auto-detected
    if (!url_branch.empty() && opt_branch.empty() && 
        url_branch != "master" && url_branch != "main") {
        opt_branch = url_branch;
    }

    if (path.empty() && optind < argc) {
        path = argv[optind++];
    }

    if (optind < argc) {
        std::fprintf(stderr, "%s: too many arguments\n", PROGRAM_NAME);
        usage(EXIT_FAILURE);
    }

    bool success = false;

    if (path.empty()) {
        // clone whole repo - use repo name as default destination
        std::string dest = opt_output_dir;
        if (opt_output_dir == "./" || opt_output_dir == ".") {
            dest = repo;
        }
        success = clone_repository(owner, repo, "", dest);
    } else if (path.back() == '/') {
        // directory download
        std::string dir_path = path.substr(0, path.length() - 1);
        std::string output_path = (opt_output_dir == "./" || opt_output_dir == ".")
                                      ? std::filesystem::path(dir_path).filename().string()
                                      : (std::filesystem::path(opt_output_dir) /
                                         std::filesystem::path(dir_path).filename())
                                            .string();

        success = download_directory_selective(owner, repo, dir_path, output_path);
        if (!success && !opt_quiet) {
            std::fprintf(stderr, "%s: trying full repo clone...\n", PROGRAM_NAME);
            std::string dest = opt_output_dir;
            if (opt_output_dir == "./" || opt_output_dir == ".") dest = repo;
            success = clone_repository(owner, repo, "", dest);
        }
    } else {
        // single file
        std::string output_file =
            (opt_output_dir == "./" || opt_output_dir == ".")
                ? path
                : (std::filesystem::path(opt_output_dir) / std::filesystem::path(path).filename())
                      .string();

        success = download_file(owner, repo, path, output_file);

        // maybe it's actually a directory?
        if (!success) {
            if (!opt_quiet)
                std::fprintf(stderr, "%s: trying as directory...\n", PROGRAM_NAME);
            std::string output_path = (opt_output_dir == "./" || opt_output_dir == ".")
                                          ? std::filesystem::path(path).filename().string()
                                          : (std::filesystem::path(opt_output_dir) /
                                             std::filesystem::path(path).filename())
                                                .string();
            success = download_directory_selective(owner, repo, path, output_path);
        }
    }

    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}