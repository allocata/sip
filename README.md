# sip

A small command-line utility to download files, directories, or entire
repositories from GitHub. Works with public and private repositories.
Uses sparse checkout for efficient directory retrieval and discovers the
default branch when none is specified. Accepts both `OWNER/REPO` style
arguments and full GitHub URLs.

## Requirements

* `git` and `curl` in PATH
* C++17 compiler (for building)
* On Windows: MinGW-w64 with GCC (recommended) or MSVC with getopt compatibility

## Build

### Linux/macOS
```
g++ -std=c++17 -O3 -Wall -Wextra -o sip sip.cpp
./sip --version
```

### Windows
```
# Using MinGW-w64 (recommended)
g++ -std=c++17 -O3 -Wall -Wextra -static-libgcc -static-libstdc++ -o sip.exe sip.cpp

# Or using the Makefile
make
```

Windows builds use static linking to avoid DLL dependency issues. The resulting executable is self-contained and doesn't require external runtime libraries. Note: Windows executables will be larger (~2MB) due to included standard libraries.

## Install

Copy the `sip` (or `sip.exe` on Windows) binary somewhere on your PATH, e.g.:

### Linux/macOS
```
install -m755 sip /usr/local/bin/
```

### Windows
Copy `sip.exe` to a directory in your PATH, or use it directly from the build directory.

## Synopsis

```
sip [OPTION]... OWNER/REPO [PATH]
sip [OPTION]... https://github.com/OWNER/REPO
sip [OPTION]... https://github.com/OWNER/REPO/tree/BRANCH/PATH
sip [OPTION]... https://github.com/OWNER/REPO/blob/BRANCH/PATH
```

If PATH is omitted, the repository is cloned.
If PATH ends with “/”, the named directory is downloaded via sparse checkout.
Otherwise PATH is treated as a single file to download.

## Options

```
-o, --output-dir=DIR     write output to DIR
-b, --branch=REF         branch, tag, or commit SHA (auto-detected if omitted)
-t, --timeout=SECONDS    curl timeout (default: 10)
-q, --quiet              suppress output
-v, --verbose            verbose output
    --help              show help
    --version           show version
```

## Environment

```
GITHUB_TOKEN             Personal access token for private repositories
```

## Examples

Download a file from the default branch:

```
sip torvalds/linux CREDITS
```

Download a directory into ./arch:

```
sip torvalds/linux arch/ -o ./arch
```

Download from a tag:

```
sip torvalds/linux -b v6.0 CREDITS
```

Download using a specific commit:

```
sip owner/repo -b 0123abcd path/to/file.txt
```

Use full GitHub URLs:

```
sip https://github.com/torvalds/linux/
sip https://github.com/torvalds/linux/tree/master/arch/
```

## Behavior

* Files are fetched from raw\.githubusercontent.com with redirects followed.
* Directories are fetched by shallow, filtered clone + sparse checkout.
* The default branch is discovered automatically when `-b` is not given.
* Output paths must not already exist; choose a different destination.
* For URL inputs, branch in the URL is honored unless it is a common
  default (master/main), in which case auto-detection is used.

## Exit status

Returns 0 on success. Non-zero indicates failure; details are printed to stderr.
Typical causes: `22` (curl HTTP error), `128` (git error).

## License

MIT License.
