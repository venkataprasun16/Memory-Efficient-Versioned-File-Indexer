### CS253 Assignment 1
# Memory-Efficient Versioned File Indexer
## Overview

The program reads a large text file **incrementally** using a fixed-size buffer (256 KB – 1024 KB). As it reads each chunk, it tokenizes the text into lowercase alphanumeric words and builds a frequency map (word → count). This map is called a **word-level index**. Each indexed file is associated with a user-provided **version name**, allowing multiple versions to be indexed and compared within the same execution run.

Once indexing is complete, the user can run one of three queries:
- **word** — how many times a word appears in a version
- **diff** — frequency difference of a word between two versions
- **top** — top-K most frequent words in a version

## Classes 

### `Buffered_File_Reader`
Handles all files. Opens a binary file and reads it in fixed-size chunks using an internal `vector<char>` buffer. Tracks a `leftover` string to handle tokens that are split across two consecutive buffer boundaries — ensuring no word is missed or double-counted.

- Constructor enforces buffer size is between **256 KB and 1024 KB** (throws `out_of_range` otherwise)
- `nextChunk(string& chunk)` — fills the chunk and returns `false` when the file is exhausted
- Destructor closes the file safely

### `Tokenizer`
Converts a raw text chunk into a list of lowercase words. A "word" is any contiguous sequence of alphanumeric characters (`isalnum`). All characters are lowercased for case-insensitive matching.

- `tokenize(const string& text)` → `vector<string>`
- `isWordChar(char c)` — private helper to classify characters

### `Indexer`
Maintains a **versioned index** — an `unordered_map` keyed by version name, where each version maps to its word-frequency map. Coordinates `Buffered_File_Reader` and `Tokenizer` to build each version's index.

- `build(version, filePath, bufferKB)` — indexes a file under a version name
- `getFrequency(version, word)` — returns count of a word (0 if absent)
- `getMap(version)` — returns the full frequency map for a version
- `hasVersion(version)` — checks if a version has been indexed

### `Query` *(Abstract Base Class)*
Defines the interface for all query types. Contains two pure virtual methods:
- `execute(Indexer& idx)` — performs the query logic
- `printResult()` — prints the result to stdout

This class is **never instantiated directly** — it exists solely to define a contract for derived query classes.

### `WordQuery` *(derived from `Query`)*
Looks up the frequency of a single word in a given version.

- Overrides `execute()` and `printResult()`
- Stores: `version`, `word`, `result` (frequency count)

### `DiffQuery` *(derived from `Query`)*
Computes the frequency difference of a word between two versions (`freq1 - freq2`).

- Overrides `execute()` and `printResult()`
- Stores: `version1`, `version2`, `word`, `result` (signed difference)

### `TopKQuery` *(derived from `Query`)*
Retrieves the top-K most frequent words in a version, optionally filtered by a minimum frequency threshold.
- Overrides `execute()` (sorts entries descending, slices top-K) and `printResult()`

### `QueryProcessor`
Owns a reference to the `Indexer` and acts as the execution engine. Calls `execute()` then `printResult()` on any `Query` object passed to it.

- `run(Query& query)` — accepts any `Query` subclass via the base reference (runtime polymorphism)

### `sortDescending<T>` *(Function Template)*
A standalone template function that sorts a `vector<pair<string, T>>` by value in descending order using a lambda comparator. Used by `TopKQuery` to rank words.

## C++ Features Used

| **Inheritance** | `WordQuery`, `DiffQuery`, `TopKQuery` all inherit from abstract base class `Query` |
| **Runtime Polymorphism** | `QueryProcessor::run(Query&)` calls `execute()` and `printResult()` via virtual dispatch — the correct derived class method is called at runtime |
| **Abstract Base Class** | `Query` declares `execute()` and `printResult()` as pure virtual (`= 0`); cannot be instantiated |
| **Function Overloading** | `TopKQuery` has two constructors — one with `(version, k)` and one with `(version, k, minFrequency)` |
| **Function Template** | `sortDescending<T>` works for any comparable value type |
| **Exception Handling** | `try/catch/throw` used in `main()`; exceptions thrown for bad buffer size, missing file, invalid args, unknown version |
| **RAII** | `Buffered_File_Reader` opens the file in its constructor and closes it in its destructor |

## How to Compile
```bash
g++ -std=c++17 -O2 -o analyzer main.cpp
```

## How to Run

**Word query** — frequency of a single word in a file:
```bash
./analyzer --file dataset_v1.txt --version v1 \
           --buffer 512 --query word --word error
```

**Top-K query** — top 10 most frequent words:
```bash
./analyzer --file dataset_v1.txt --version v1 \
           --buffer 512 --query top --top 10
```

**Diff query** — frequency difference between two versions:
```bash
./analyzer --file1 dataset_v1.txt --version1 v1 \
           --file2 dataset_v2.txt --version2 v2 \
           --buffer 512 --query diff --word error
```

## Argument Parsing

Arguments are parsed in `main()` using a simple sequential loop over `argv`. Each `--flag value` pair is consumed together by advancing the index `i` by 1 after reading a value. Recognized flags:

- `--file` / `--file1` / `--file2` → file paths
- `--version` / `--version1` / `--version2` → version names
- `--buffer` → parsed with `stoul()`
- `--query` → query type string
- `--word` → target word
- `--top` → parsed with `stoi()`

After parsing, validation checks ensure all required arguments are present for the selected query type. Missing or incompatible arguments throw `invalid_argument`.

## Error Handling

All errors are caught in `main()`'s `try/catch(const exception&)` block and printed to `stderr`. 

Buffer size outside 256–1024 KB - `out_of_range` 
File cannot be opened - `runtime_error` 
Unknown or missing version - `invalid_argument` 
Missing required CLI argument - `invalid_argument` 
`--top k` is zero or negative - `invalid_argument` 
Unknown query type - `invalid_argument` |
