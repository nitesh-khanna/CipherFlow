# CipherFlow

A C++17 command-line utility that encrypts or decrypts regular files in a directory. It uses OpenSSL and authenticated AES-256-GCM, processes large files in chunks, and stages output before replacing each original file.

## Features

- Encrypt or decrypt every regular file found under a directory, including files in nested directories.
- Process data in 1 MiB chunks so large files do not need to be loaded into memory all at once.
- Derive a 256-bit encryption key from a password with PBKDF2-HMAC-SHA-256 (100,000 iterations).
- Use a random salt and nonce for each encrypted chunk and authenticate each chunk with AES-GCM.
- Reject incorrect passwords and modified or malformed encrypted data through authentication checks.
- Write to a temporary sibling file (`<filename>.sdfv.tmp`) and replace the source after processing completes.

## Requirements

- A C++17 compiler (the Makefile uses `g++`).
- GNU Make.
- OpenSSL 3 development headers and the `libcrypto` library.

The Makefile defaults to the UCRT64 location `/ucrt64`. If OpenSSL is installed elsewhere, pass its prefix when building. If your compiler already finds OpenSSL headers and libraries through its standard paths, you can override the flags directly.

## Build

From the project root:

```sh
make
```

This creates two executables:

- `encrypt_decrypt` — the directory-oriented command-line application.
- `cryption` — a legacy single-file task entry point.

For a non-default OpenSSL installation, for example:

```sh
make OPENSSL_PREFIX=/usr
```

Or use explicit compiler and linker flags:

```sh
make OPENSSL_CXXFLAGS="-I/path/to/openssl/include" OPENSSL_LDFLAGS="-L/path/to/openssl/lib"
```

## Configure the password

The application reads `.env` from the current working directory (the directory from which you launch the executable). Create that file with a password entry:

```dotenv
VAULT_PASSWORD=replace-this-with-a-strong-password
```

`PASSWORD` and `KEY` are also accepted as key names. Keep `.env` private and do not commit it. The password is required for both encryption and decryption; decryption must use the same password that was used to encrypt the files.

## Usage

Pass a directory and an action:

```sh
./encrypt_decrypt /path/to/folder encrypt
./encrypt_decrypt /path/to/folder decrypt
```

Actions are case-insensitive. The program also supports interactive prompts when run without arguments:

```sh
./encrypt_decrypt
```

Show the command usage:

```sh
./encrypt_decrypt --help
```

The operation replaces the contents of files in the target directory tree; it does not create a separate output directory or retain a plaintext backup. Back up important data before encrypting. Run decryption on the same directory to restore the original file contents.

Only regular files are processed. An empty directory (or one containing no regular files) is reported as having nothing to do. The tool validates that the files can be updated before beginning, and writes each file to a temporary sibling before replacing that file. If an operation fails, inspect the reported error and any `.sdfv.tmp` file before retrying.

## Encrypted data format

Each encrypted chunk is stored as a self-describing binary packet with the `SDFV` magic value. Its header records the format version, algorithm and KDF identifiers, PBKDF2 iteration count, salt and nonce lengths, plaintext and ciphertext sizes, and the random salt and nonce. The header is authenticated as AES-GCM additional data; the ciphertext and authentication tag follow it. The current algorithm identifier is `aes-256-gcm-pbkdf2-sha256-v1`.

## Tests

Run the project checks with:

```sh
make test
```

This builds and runs the architecture tests and the CLI round-trip script. The round-trip check verifies encryption and decryption with a temporary test directory and `.env` file.

## Project layout

```text
.
├── main.cpp                         # Main CLI entry point
├── Makefile                         # Build and test targets
├── src/app/chunker/                 # File and chunk manifests
├── src/app/cli/                     # CLI arguments and prompts
├── src/app/crypto/                  # AES-GCM engine and packet format
├── src/app/encryptDecrypt/          # Legacy single-file entry point
├── src/app/fileHandling/            # Password loading from .env
├── src/app/jobs/                    # Chunk processing queue
├── src/app/manifest/                # Processing manifests
├── src/app/metadata/                # In-memory metadata store
├── src/app/storage/                 # Local staged file writes
└── test/                            # Architecture and round-trip checks
```

## Clean build outputs

```sh
make clean
```
