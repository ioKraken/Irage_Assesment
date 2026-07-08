#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace blc {

// caller can reuse one row buffer across messages (no per-field allocation).
void append_csv_field(std::string& out, std::string_view field);


std::vector<std::string> parse_csv_line(std::string_view line);

// A thin RAII wrapper over std::ofstream with a big buffer.
// One instance per output file; the destructor flushes and closes.
class CsvFile {
public:
    // Opens (truncates) the file and writes the header line immediately.
    CsvFile(const std::string& path, std::string_view header_line);
    ~CsvFile();

    CsvFile(const CsvFile&) = delete;
    CsvFile& operator=(const CsvFile&) = delete;

    bool is_open() const { return out_.is_open(); }

    // Writes one full row (caller builds it, without trailing newline).
    void write_row(std::string_view row);

    void flush();

    uint64_t rows_written() const { return rows_; }
    const std::string& path() const { return path_; }

private:
    std::string path_;
    std::vector<char> buffer_;  // stream buffer, keeps writes cheap
    std::ofstream out_;
    uint64_t rows_ = 0;
};

}  // namespace blc
