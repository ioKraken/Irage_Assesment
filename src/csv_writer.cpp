#include "csv_writer.h"

#include <stdexcept>

namespace blc {

void append_csv_field(std::string& out, std::string_view field) {
    bool needs_quotes = field.find_first_of(",\"\r\n") != std::string_view::npos;
    if (!needs_quotes) {
        out.append(field);
        return;
    }
    out.push_back('"');
    for (char c : field) {
        if (c == '"') out.push_back('"');  // escape " as ""
        out.push_back(c);
    }
    out.push_back('"');
}

std::vector<std::string> parse_csv_line(std::string_view line) {
    std::vector<std::string> fields;
    std::string current;
    bool in_quotes = false;

    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (in_quotes) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    current.push_back('"');  // "" -> literal quote
                    ++i;
                } else {
                    in_quotes = false;  // closing quote
                }
            } else {
                current.push_back(c);
            }
        } else {
            if (c == '"') {
                in_quotes = true;
            } else if (c == ',') {
                fields.push_back(std::move(current));
                current.clear();
            } else if (c == '\r') {
                // ignore trailing CR from CRLF lines
            } else {
                current.push_back(c);
            }
        }
    }
    fields.push_back(std::move(current));
    return fields;
}

CsvFile::CsvFile(const std::string& path, std::string_view header_line)
    : path_(path), buffer_(1 << 20) /* 1 MiB stream buffer */ {
    out_.rdbuf()->pubsetbuf(buffer_.data(), static_cast<std::streamsize>(buffer_.size()));
    out_.open(path, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!out_.is_open()) {
        throw std::runtime_error("cannot open output file: " + path);
    }
    out_.write(header_line.data(), static_cast<std::streamsize>(header_line.size()));
    out_.put('\n');
}

CsvFile::~CsvFile() {
    if (out_.is_open()) {
        out_.flush();
        out_.close();
    }
}

void CsvFile::write_row(std::string_view row) {
    out_.write(row.data(), static_cast<std::streamsize>(row.size()));
    out_.put('\n');
    ++rows_;
}

void CsvFile::flush() { out_.flush(); }

}  // namespace blc
