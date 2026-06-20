#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <stdexcept>
#include <unordered_map>
#include <algorithm>
#include <chrono>

using namespace std;

template <typename T>
void sortDescending(vector<pair<string, T>>& entries) {
    sort(entries.begin(), entries.end(),
         [](const pair<string, T>& a, const pair<string, T>& b) {
             return a.second > b.second;
         });
}

class Buffered_File_Reader {
public:
    Buffered_File_Reader(const string& path, size_t bufferSizeKB);
    ~Buffered_File_Reader();
    bool nextChunk(string& chunk);
    size_t bufferSizeKB() const;

private:
    ifstream     file;
    size_t       bufferSize;
    vector<char> buffer;
    string       leftover;
};

Buffered_File_Reader::Buffered_File_Reader(const string& path, size_t bufferSizeKB)
    : bufferSize(bufferSizeKB * 1024), leftover("")
{
    if (bufferSizeKB < 256 || bufferSizeKB > 1024)
        throw out_of_range("Buffer size must be between 256 KB and 1024 KB. Got: " + to_string(bufferSizeKB) + " KB");

    file.open(path, ios::binary);
    if (!file.is_open())
        throw runtime_error("Cannot open file: " + path);

    buffer.resize(bufferSize);
}

Buffered_File_Reader::~Buffered_File_Reader() {
    if (file.is_open())
        file.close();
}

bool Buffered_File_Reader::nextChunk(string& chunk) {
    file.read(buffer.data(), (streamsize)bufferSize);
    size_t bytesRead = (size_t)file.gcount();

    if (bytesRead == 0 && leftover.empty())
        return false;

    chunk = leftover + string(buffer.data(), bytesRead);
    leftover.clear();

    if (!file.eof() && bytesRead > 0) {
        size_t i = chunk.size();
        while (i > 0 && isalnum((unsigned char)chunk[i - 1]))
            --i;
        leftover = chunk.substr(i);
        chunk    = chunk.substr(0, i);
    }

    return true;
}

size_t Buffered_File_Reader::bufferSizeKB() const {
    return bufferSize / 1024;
}

class Tokenizer {
public:
    vector<string> tokenize(const string& text);

private:
    bool isWordChar(char c);
};

bool Tokenizer::isWordChar(char c) {
    return isalnum((unsigned char)c) != 0;
}

vector<string> Tokenizer::tokenize(const string& text) {
    vector<string> tokens;
    string token;

    for (char c : text) {
        if (isWordChar(c)) {
            token += (char)tolower((unsigned char)c);
        } else {
            if (!token.empty()) {
                tokens.push_back(token);
                token.clear();
            }
        }
    }

    if (!token.empty())
        tokens.push_back(token);

    return tokens;
}

class Indexer {
public:
    void build(const string& version, const string& filePath, size_t bufferKB);
    long long getFrequency(const string& version, const string& word);
    unordered_map<string, long long> getMap(const string& version);
    bool hasVersion(const string& version);

private:
    unordered_map<string, unordered_map<string, long long>> index;
};

void Indexer::build(const string& version, const string& filePath, size_t bufferKB) {
    index[version].clear();

    Buffered_File_Reader reader(filePath, bufferKB);
    Tokenizer tokenizer;
    string chunk;

    while (reader.nextChunk(chunk)) {
        vector<string> words = tokenizer.tokenize(chunk);
        for (const string& word : words)
            index[version][word]++;
    }
}

long long Indexer::getFrequency(const string& version, const string& word) {
    if (index.find(version) == index.end())
        throw invalid_argument("Version not found: " + version);

    if (index[version].find(word) == index[version].end())
        return 0;

    return index[version][word];
}

unordered_map<string, long long> Indexer::getMap(const string& version) {
    if (index.find(version) == index.end())
        throw invalid_argument("Version not found: " + version);

    return index[version];
}

bool Indexer::hasVersion(const string& version) {
    return index.find(version) != index.end();
}

class Query {
public:
    virtual void execute(Indexer& idx) = 0;
    virtual void printResult() = 0;
    virtual ~Query();
};

Query::~Query() {}

class WordQuery : public Query {
public:
    WordQuery(const string& version, const string& word);
    void execute(Indexer& idx);
    void printResult();

private:
    string    version;
    string    word;
    long long result;
};

WordQuery::WordQuery(const string& version, const string& word) {
    this->version = version;
    this->word    = word;
    this->result  = 0;
}

void WordQuery::execute(Indexer& idx) {
    result = idx.getFrequency(version, word);
}

void WordQuery::printResult() {
    cout << "Version: " << version << "\n";
    cout << "Count: " << result << "\n";
}

class DiffQuery : public Query {
public:
    DiffQuery(const string& version1, const string& version2, const string& word);
    void execute(Indexer& idx);
    void printResult();

private:
    string    version1;
    string    version2;
    string    word;
    long long result;
};

DiffQuery::DiffQuery(const string& version1, const string& version2, const string& word) {
    this->version1 = version1;
    this->version2 = version2;
    this->word     = word;
    this->result   = 0;
}

void DiffQuery::execute(Indexer& idx) {
    long long freq1 = idx.getFrequency(version1, word);
    long long freq2 = idx.getFrequency(version2, word);
    result = freq1 - freq2;
}

void DiffQuery::printResult() {
    cout << "Difference (" << version2 << " - " << version1 << "): " << result << "\n";
}

class TopKQuery : public Query {
public:
    TopKQuery(const string& version, int k);
    TopKQuery(const string& version, int k, long long minFrequency);
    void execute(Indexer& idx);
    void printResult();

private:
    string    version;
    int       k;
    long long minFrequency;
    vector<pair<string, long long>> results;
};

TopKQuery::TopKQuery(const string& version, int k) {
    this->version      = version;
    this->k            = k;
    this->minFrequency = 0;
}

TopKQuery::TopKQuery(const string& version, int k, long long minFrequency) {
    this->version      = version;
    this->k            = k;
    this->minFrequency = minFrequency;
}

void TopKQuery::execute(Indexer& idx) {
    if (k <= 0)
        throw invalid_argument("k must be a positive integer.");

    unordered_map<string, long long> freqMap = idx.getMap(version);
    vector<pair<string, long long>> entries;

    for (const auto& entry : freqMap) {
        if (entry.second >= minFrequency)
            entries.push_back(entry);
    }

    sortDescending<long long>(entries);

    results.clear();
    for (int i = 0; i < k && i < (int)entries.size(); i++)
        results.push_back(entries[i]);
}

void TopKQuery::printResult() {
    cout << "Top-" << k << " words in version " << version << ":\n";
    for (int i = 0; i < (int)results.size(); i++)
        cout << results[i].first << " " << results[i].second << "\n";
}

class QueryProcessor {
public:
    QueryProcessor(Indexer& idx);
    void run(Query& query);

private:
    Indexer& idx;
};

QueryProcessor::QueryProcessor(Indexer& idx) : idx(idx) {}

void QueryProcessor::run(Query& query) {
    query.execute(idx);
    query.printResult();
}

int main(int argc, char* argv[]) {
    try {
        string file1, file2;
        string version1, version2;
        size_t bufferKB = 512;
        string queryType;
        string word;
        int topK = 0;

        for (int i = 1; i < argc - 1; i++) {
            string flag = argv[i];
            string val  = argv[i + 1];

            if      (flag == "--file")     { file1     = val; i++; }
            else if (flag == "--file1")    { file1     = val; i++; }
            else if (flag == "--file2")    { file2     = val; i++; }
            else if (flag == "--version")  { version1  = val; i++; }
            else if (flag == "--version1") { version1  = val; i++; }
            else if (flag == "--version2") { version2  = val; i++; }
            else if (flag == "--buffer")   { bufferKB  = stoul(val); i++; }
            else if (flag == "--query")    { queryType = val; i++; }
            else if (flag == "--word")     { word      = val; i++; }
            else if (flag == "--top")      { topK      = stoi(val); i++; }
        }

        if (queryType.empty())
            throw invalid_argument("--query is required. Use: word | diff | top");
        if (version1.empty())
            throw invalid_argument("--version is required.");
        if (file1.empty())
            throw invalid_argument("--file is required.");
        if (queryType == "diff" && (file2.empty() || version2.empty()))
            throw invalid_argument("--file2 and --version2 are required for diff query.");
        if (queryType == "word" && word.empty())
            throw invalid_argument("--word is required for word query.");
        if (queryType == "diff" && word.empty())
            throw invalid_argument("--word is required for diff query.");
        if (queryType == "top" && topK <= 0)
            throw invalid_argument("--top <k> is required and must be positive.");

        auto startTime = chrono::high_resolution_clock::now();

        Indexer idx;
        idx.build(version1, file1, bufferKB);

        if (queryType == "diff")
            idx.build(version2, file2, bufferKB);

        QueryProcessor qp(idx);

        if (queryType == "word") {
            WordQuery wq(version1, word);
            qp.run(wq);
        }
        else if (queryType == "diff") {
            DiffQuery dq(version1, version2, word);
            qp.run(dq);
        }
        else if (queryType == "top") {
            TopKQuery tq(version1, topK);
            qp.run(tq);
        }
        else {
            throw invalid_argument("Unknown query type: " + queryType);
        }

        auto endTime = chrono::high_resolution_clock::now();
        chrono::duration<double> elapsed = endTime - startTime;

        cout << "Buffer Size (KB): " << bufferKB << "\n";
        cout << "Execution Time (s): " << elapsed.count() << "\n";

    } catch (const exception& e) {
        cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

    

