# CipherFlow

CipherFlow is a C++17 command-line utility for encrypting and decrypting files in a directory tree. It uses OpenSSL's authenticated AES-256-GCM encryption, processes large files in chunks, and supports configurable multithreaded processing through a worker pool.

## Features

- Encrypts or decrypts regular files under a target directory, including nested directories.
- Processes data in 1 MiB chunks to avoid loading entire large files into memory.
- Derives a 256-bit key using PBKDF2-HMAC-SHA-256 with 100,000 iterations.
- Uses a random salt and nonce for each encrypted chunk and authenticates each chunk with AES-GCM.
- Rejects incorrect passwords and modified or malformed encrypted data through authentication checks.
- Supports a configurable worker pool using `--workers N` (1–64 workers).
- Uses `std::thread`, `std::mutex`, and `std::condition_variable` to coordinate concurrent jobs through a shared job queue.
- Preserves deterministic output ordering and stages processed output before committing changes to source files.
- Includes architecture tests, round-trip tests, tamper detection, wrong-password handling, and concurrent round-trip tests.
- Includes a benchmark script that measures runtime, throughput, and speedup and verifies decrypted files byte-for-byte.

## Requirements

- C++17 compiler (`g++` in the supplied Makefile).
- GNU Make.
- OpenSSL 3 development headers and the `libcrypto` library.
- For the Windows setup used during development: MSYS2 UCRT64 and `mingw32-make`.

The Makefile defaults to the OpenSSL prefix `/ucrt64`. If OpenSSL is installed elsewhere, pass its prefix when building.

## Build

From the project root:

```sh
make
```

On Windows with MSYS2 UCRT64:

```sh
mingw32-make OPENSSL_PREFIX=/ucrt64
```

For a non-default OpenSSL installation:

```sh
make OPENSSL_PREFIX=/usr
```

Or specify compiler and linker flags directly:

```sh
make OPENSSL_CXXFLAGS="-I/path/to/openssl/include" OPENSSL_LDFLAGS="-L/path/to/openssl/lib"
```

The main directory-oriented executable is `encrypt_decrypt` (`encrypt_decrypt.exe` on Windows). The project may also include the legacy single-file `cryption` entry point.

## Configure the password

The application reads `.env` from the current working directory. Create this file in the directory from which you launch the executable:

```dotenv
VAULT_PASSWORD=replace-this-with-a-strong-password
```

`PASSWORD` and `KEY` are also accepted as key names.

**Keep `.env` private and never commit it.** Encryption and decryption must use the same password.

## Usage

Specify the target directory, action, and optionally the worker count:

```sh
./encrypt_decrypt /path/to/folder encrypt --workers 4
./encrypt_decrypt /path/to/folder decrypt --workers 4
```

On Windows/MSYS2:

```sh
./encrypt_decrypt.exe "/d/path/to/folder" encrypt --workers 4
./encrypt_decrypt.exe "/d/path/to/folder" decrypt --workers 4
```

The worker count can be configured from 1 to 64. Actions are case-insensitive.

Interactive mode:

```sh
./encrypt_decrypt
```

Display command usage:

```sh
./encrypt_decrypt --help
```

**Important:** operations modify files in the target directory tree rather than creating a separate output directory. Back up important data before encrypting. Do not use the tool on your only copy of important files.

The application stages output in temporary sibling files before committing changes. If an operation fails, inspect the reported error and any `.sdfv.tmp` or `.sdfv.bak` files before retrying.

## Benchmark Results

The benchmark used a **225 MiB synthetic dataset** consisting of three binary files of 50 MiB, 75 MiB, and 100 MiB. Each worker configuration was run three times, and the table reports median runtime and median throughput. Speedup is calculated relative to the one-worker median for the same operation.

### Encryption

| Workers | Median time (s) | Median throughput (MB/s) | Speedup |
|---:|---:|---:|---:|
| 1 | 4.175 | 56.512 | 1.00× |
| 2 | 2.663 | 88.594 | 1.57× |
| 4 | 1.516 | 155.597 | 2.75× |
| 8 | 1.155 | 204.185 | 3.61× |

### Decryption

| Workers | Median time (s) | Median throughput (MB/s) | Speedup | Verification |
|---:|---:|---:|---:|---|
| 1 | 4.159 | 56.725 | 1.00× | PASS |
| 2 | 2.612 | 90.310 | 1.59× | PASS |
| 4 | 1.793 | 131.576 | 2.32× | PASS |
| 8 | 1.289 | 182.990 | 3.23× | PASS |

With eight workers, the benchmark achieved a **3.61× encryption speedup** and a **3.23× decryption speedup** compared with one worker. Decrypted files were compared byte-for-byte against the originals for all tested worker configurations.

These results are from a local synthetic-data benchmark, not a universal performance guarantee. Actual performance varies with CPU, storage, compiler settings, OpenSSL version, and workload.

### Reproduce the benchmark

1. Build the project and configure `.env`.
2. Prepare the benchmark dataset. The script used during testing expects it at `/d/projects/cipherflow-benchmark-data` in the MSYS2 environment.
3. Place `benchmark.sh` in the project root and run:

```sh
chmod +x benchmark.sh
./benchmark.sh
```

The script tests 1, 2, 4, and 8 workers with three repetitions per configuration. It creates timestamped results containing logs, a CSV, and an encrypted reference dataset. Temporary working copies are removed after each run.

**Before running:** verify the project and dataset paths in `benchmark.sh`. The benchmark encrypts and decrypts files in its working copies, so do not point it at your only copy of important data.


## Encrypted Data Format

Each encrypted chunk is stored as a self-describing binary packet with the `SDFV` magic value. Its header records the format version, algorithm and KDF identifiers, PBKDF2 iteration count, salt and nonce lengths, plaintext and ciphertext sizes, and the random salt and nonce. The header is authenticated as AES-GCM additional data; the ciphertext and authentication tag follow it.

The current algorithm identifier is `aes-256-gcm-pbkdf2-sha256-v1`.

## Tests

Run the project checks with:

```sh
make test
```

On Windows/MSYS2 UCRT64:

```sh
mingw32-make OPENSSL_PREFIX=/ucrt64 test
```

The test suite covers action parsing, AES-GCM encryption/decryption and tamper detection, chunked-file processing, queue-based processing, concurrent round trips using 1, 2, 4, and 8 workers, wrong-password handling, and preservation of encrypted inputs when decryption fails.

These tests validate implemented behavior; they are not a formal security audit.

## Project Layout

```text
.
├── main.cpp                         # Main CLI entry point
├── Makefile                         # Build and test targets
├── benchmark.sh                     # Repeatable performance benchmark
├── src/app/chunker/                 # File and chunk manifests
├── src/app/cli/                     # CLI arguments and prompts
├── src/app/crypto/                  # AES-GCM engine and packet format
├── src/app/encryptDecrypt/          # Legacy single-file entry point
├── src/app/fileHandling/            # Password loading from .env
├── src/app/jobs/                    # Concurrent job queue and worker processing
├── src/app/manifest/                # Processing manifests
├── src/app/metadata/                # In-memory metadata store
├── src/app/storage/                 # Local staged file writes
└── test/                            # Architecture and round-trip checks
```

## Clean Build Outputs

```sh
make clean
```

On Windows/MSYS2:

```sh
mingw32-make OPENSSL_PREFIX=/ucrt64 clean
```

## Repository Hygiene

Do not commit `.env`, passwords, private test data, generated executables, temporary `.sdfv.tmp` or `.sdfv.bak` files, or local benchmark logs.
