#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace {

#if defined(_WIN32)

void write_raw(HANDLE handle, const void* data, DWORD size) {
    if (size == 0) return;
    DWORD written = 0;
    const char* ptr = static_cast<const char*>(data);
    while (size > 0) {
        if (!::WriteFile(handle, ptr, size, &written, nullptr) || written == 0) {
            break;
        }
        ptr += written;
        size -= written;
    }
}

void write_stdout(std::string_view text) {
    HANDLE h = ::GetStdHandle(STD_OUTPUT_HANDLE);
    write_raw(h, text.data(), static_cast<DWORD>(text.size()));
}

void write_stderr(std::string_view text) {
    HANDLE h = ::GetStdHandle(STD_ERROR_HANDLE);
    write_raw(h, text.data(), static_cast<DWORD>(text.size()));
}

std::string wide_to_utf8(std::wstring_view ws) {
    if (ws.empty()) return {};
    int len = ::WideCharToMultiByte(
        CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string result(static_cast<size_t>(len), '\0');
    ::WideCharToMultiByte(
        CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()), result.data(), len, nullptr, nullptr);
    return result;
}

#endif

} // namespace

#if defined(_WIN32)

int wmain(int argc, wchar_t* argv[]) {
    if (argc < 2) {
        write_stderr("taskforge_test_helper: missing mode argument\n");
        return 1;
    }

    std::wstring mode = argv[1];

    if (mode == L"--exit") {
        if (argc < 3) return 0;
        return std::wcstol(argv[2], nullptr, 10);
    }

    if (mode == L"--stdout") {
        if (argc >= 3) {
            std::string text = wide_to_utf8(argv[2]);
            write_stdout(text);
        }
        return 0;
    }

    if (mode == L"--stderr") {
        if (argc >= 3) {
            std::string text = wide_to_utf8(argv[2]);
            write_stderr(text);
        }
        return 0;
    }

    if (mode == L"--pwd") {
        std::error_code ec;
        auto cwd = std::filesystem::current_path(ec);
        std::string cwd_u8 = wide_to_utf8(cwd.wstring());
        write_stdout(cwd_u8);
        return 0;
    }

    if (mode == L"--read-stdin") {
        HANDLE hIn = ::GetStdHandle(STD_INPUT_HANDLE);
        char buf[256];
        DWORD bytes_read = 0;
        while (::ReadFile(hIn, buf, sizeof(buf), &bytes_read, nullptr) && bytes_read > 0) {
            // Drain stdin
        }
        write_stdout("STDIN_EOF");
        return 0;
    }

    if (mode == L"--echo-args") {
        // Output each argument following --echo-args as "[index]: <arg>\n"
        for (int i = 2; i < argc; ++i) {
            int index = i - 2;
            std::string line = "[" + std::to_string(index) + "]: " + wide_to_utf8(argv[i]) + "\n";
            write_stdout(line);
        }
        return 0;
    }

    if (mode == L"--stream-interleaved") {
        if (argc < 4) {
            write_stderr("Usage: --stream-interleaved <bytes_total> <chunk_size>\n");
            return 1;
        }
        long long total = std::wcstoll(argv[2], nullptr, 10);
        long long chunk_size = std::wcstoll(argv[3], nullptr, 10);
        if (total <= 0 || chunk_size <= 0) return 1;

        std::string chunk_out(static_cast<size_t>(chunk_size), 'O');
        std::string chunk_err(static_cast<size_t>(chunk_size), 'E');

        long long written = 0;
        while (written < total) {
            long long to_write = std::min(chunk_size, total - written);
            write_stdout(std::string_view(chunk_out.data(), static_cast<size_t>(to_write)));
            write_stderr(std::string_view(chunk_err.data(), static_cast<size_t>(to_write)));
            written += to_write;
        }
        return 0;
    }

    if (mode == L"--stream-large-stdout") {
        if (argc < 3) return 1;
        long long total = std::wcstoll(argv[2], nullptr, 10);
        constexpr size_t kChunk = 65536;
        std::string chunk(kChunk, 'X');
        long long written = 0;
        while (written < total) {
            long long to_write = std::min(static_cast<long long>(kChunk), total - written);
            write_stdout(std::string_view(chunk.data(), static_cast<size_t>(to_write)));
            written += to_write;
        }
        return 0;
    }

    write_stderr("taskforge_test_helper: unknown mode: " + wide_to_utf8(mode) + "\n");
    return 1;
}

int main() {
    int argc = 0;
    wchar_t** argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (!argv) return 1;
    int res = wmain(argc, argv);
    ::LocalFree(argv);
    return res;
}

#else

int main(int argc, char* argv[]) {
    return 0;
}

#endif
