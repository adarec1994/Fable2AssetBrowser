#include "ISO/IsoMount.h"
#include "ISO/IsoWriteback.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

void require(bool value) {
    if (!value) std::abort();
}

void put32le(std::vector<uint8_t>& bytes, std::size_t offset,
             uint32_t value) {
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
    bytes[offset + 2] = static_cast<uint8_t>(value >> 16);
    bytes[offset + 3] = static_cast<uint8_t>(value >> 24);
}

void put_entry(std::vector<uint8_t>& image, std::size_t offset,
               uint32_t sector, uint32_t size, uint8_t attributes,
               const std::string& name) {
    put32le(image, offset + 4, sector);
    put32le(image, offset + 8, size);
    image[offset + 12] = attributes;
    image[offset + 13] = static_cast<uint8_t>(name.size());
    std::memcpy(image.data() + offset + 14, name.data(), name.size());
}

void make_test_iso(const std::filesystem::path& path) {
    constexpr std::size_t sector_size = 2048;
    constexpr uint32_t root_sector = 40;
    constexpr uint32_t data_sector = 41;
    constexpr uint32_t file_sector = 42;
    std::vector<uint8_t> image(43 * sector_size, 0);
    const std::size_t descriptor = 32 * sector_size;
    const char magic[] = "MICROSOFT*XBOX*MEDIA";
    std::memcpy(image.data() + descriptor, magic, 20);
    put32le(image, descriptor + 20, root_sector);
    put32le(image, descriptor + 24, sector_size);
    put_entry(image, root_sector * sector_size, data_sector, sector_size,
              0x10, "data");
    put_entry(image, data_sector * sector_size, file_sector, 4, 0,
              "test.bin");
    const std::vector<uint8_t> original = {'o', 'l', 'd', '!'};
    std::copy(original.begin(), original.end(),
              image.begin() + file_sector * sector_size);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(image.data()),
                 static_cast<std::streamsize>(image.size()));
    require(output.good());
}

}

int main() {
    std::error_code ec;
    std::string error;
    const std::filesystem::path iso_path =
        std::filesystem::temp_directory_path() /
        "fable2_asset_browser_writeback_test.iso";
    const std::filesystem::path backup_path =
        std::filesystem::path(iso_path.string() + ".f2ab_backup");
    std::filesystem::remove(iso_path, ec);
    std::filesystem::remove_all(backup_path, ec);
    make_test_iso(iso_path);
    require(ISO::IsoMount::instance().mount(iso_path.string(), &error));
    require(ISO::Writeback::CreateBackup({"data/test.bin"}, {}, error));
    const std::vector<uint8_t> replacement = {
        'r', 'e', 'p', 'l', 'a', 'c', 'e', 'm', 'e', 'n', 't'};
    require(ISO::Writeback::WriteMember("iso://data/test.bin",
                                        replacement, true, error));
    require(ISO::IsoMount::instance().read_file("data/test.bin") ==
            replacement);
    require(ISO::Writeback::RestoreBackup({}, error));
    require(ISO::IsoMount::instance().read_file("data/test.bin") ==
            std::vector<uint8_t>({'o', 'l', 'd', '!'}));
    ISO::IsoMount::instance().unmount();
    std::filesystem::remove(iso_path, ec);
    std::filesystem::remove_all(backup_path, ec);
    return 0;
}
