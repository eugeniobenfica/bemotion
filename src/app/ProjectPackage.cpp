#include "app/ProjectPackage.h"

#include "app/Platform.h"
#include "core/Serialization.h"

#include <QFile>
#include <QSaveFile>

#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <set>
#include <string_view>
#include <vector>

namespace bemotion {
namespace {
namespace fs = std::filesystem;

constexpr std::array<uint32_t, 256> crcTable() {
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < table.size(); ++i) {
        uint32_t c = i;
        for (int bit = 0; bit < 8; ++bit) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
        table[i] = c;
    }
    return table;
}
constexpr auto kCrcTable = crcTable();

uint32_t crc(uint32_t value, const char* data, qsizetype size) {
    for (qsizetype i = 0; i < size; ++i) value = kCrcTable[(value ^ static_cast<unsigned char>(data[i])) & 255] ^ (value >> 8);
    return value;
}

class ZipWriter {
public:
    ZipWriter(const fs::path& output, const std::atomic_bool* cancelled) : file_(QString::fromUtf8(platform::toUtf8(output))), cancelled_(cancelled) {}
    bool open(std::string& error) {
        if (file_.open(QIODevice::WriteOnly)) return true;
        error = "Could not write " + platform::toUtf8(fs::path(file_.fileName().toStdString()));
        return false;
    }
    bool add(std::string_view name, const std::string& text, std::string& error) {
        return addBytes(name, text.data(), static_cast<uint64_t>(text.size()), error);
    }
    bool add(std::string_view name, const fs::path& path, std::string& error) {
        std::error_code ec;
        const uint64_t size = fs::file_size(path, ec);
        if (ec) { error = "Could not read " + platform::toUtf8(path); return false; }
        if (!begin(name, size, error)) return false;
        QFile input(QString::fromUtf8(platform::toUtf8(path)));
        if (!input.open(QIODevice::ReadOnly)) { error = "Could not read " + platform::toUtf8(path); return false; }
        QByteArray block(1024 * 1024, Qt::Uninitialized);
        while (true) {
            if (stopped()) { error = "Cancelled"; return false; }
            const qint64 n = input.read(block.data(), block.size());
            if (n < 0) { error = "Could not read " + platform::toUtf8(path); return false; }
            if (!n) break;
            if (!write(block.constData(), n, error)) return false;
        }
        return end(error);
    }
    bool finish(std::string& error) {
        if (stopped()) { error = "Cancelled"; return false; }
        const uint64_t directory = written_;
        for (const Entry& e : entries_) {
            if (!number(0x02014b50u, 4, error) || !number(45, 2, error) || !number(45, 2, error) || !number(0x800, 2, error) ||
                !number(0, 2, error) || !number(0, 2, error) || !number(33, 2, error) || !number(e.crc, 4, error) ||
                !number(0xffffffffu, 4, error) || !number(0xffffffffu, 4, error) || !number(e.name.size(), 2, error) ||
                !number(28, 2, error) || !number(0, 2, error) || !number(0, 2, error) || !number(0, 2, error) || !number(0, 4, error) ||
                !number(0xffffffffu, 4, error) || !write(e.name.data(), e.name.size(), error) || !number(1, 2, error) || !number(24, 2, error) ||
                !number(e.size, 8, error) || !number(e.size, 8, error) || !number(e.offset, 8, error)) return false;
        }
        const uint64_t directorySize = written_ - directory, end64 = written_;
        if (!number(0x06064b50u, 4, error) || !number(44, 8, error) || !number(45, 2, error) || !number(45, 2, error) ||
            !number(0, 4, error) || !number(0, 4, error) || !number(entries_.size(), 8, error) || !number(entries_.size(), 8, error) ||
            !number(directorySize, 8, error) || !number(directory, 8, error) || !number(0x07064b50u, 4, error) || !number(0, 4, error) ||
            !number(end64, 8, error) || !number(1, 4, error) || !number(0x06054b50u, 4, error) || !number(0, 2, error) || !number(0, 2, error) ||
            !number(0xffff, 2, error) || !number(0xffff, 2, error) || !number(0xffffffffu, 4, error) || !number(0xffffffffu, 4, error) || !number(0, 2, error)) return false;
        if (!file_.commit()) { error = "Could not finish the ZIP file"; return false; }
        return true;
    }
private:
    struct Entry { std::string name; uint64_t size; uint64_t offset; uint32_t crc = 0; };
    bool stopped() const { return cancelled_ && cancelled_->load(); }
    bool begin(std::string_view name, uint64_t size, std::string& error) {
        if (stopped()) { error = "Cancelled"; return false; }
        if (name.empty() || name.size() > 0xffff) { error = "Invalid package file name"; return false; }
        entries_.push_back({std::string(name), size, written_});
        return number(0x04034b50u, 4, error) && number(45, 2, error) && number(0x800, 2, error) && number(0, 2, error) && number(0, 2, error) && number(33, 2, error) &&
               number(0, 4, error) && number(0xffffffffu, 4, error) && number(0xffffffffu, 4, error) && number(name.size(), 2, error) && number(20, 2, error) &&
               write(name.data(), name.size(), error) && number(1, 2, error) && number(16, 2, error) && number(size, 8, error) && number(size, 8, error);
    }
    bool addBytes(std::string_view name, const char* data, uint64_t size, std::string& error) {
        return begin(name, size, error) && write(data, size, error) && end(error);
    }
    bool end(std::string& error) {
        Entry& e = entries_.back();
        e.crc ^= 0xffffffffu;
        const qint64 endPos = file_.pos();
        if (!file_.seek(static_cast<qint64>(e.offset + 14)) || !writeRaw(reinterpret_cast<const char*>(&e.crc), 4) || !file_.seek(endPos)) {
            error = "Could not finish the ZIP file"; return false;
        }
        return true;
    }
    bool write(const char* data, uint64_t size, std::string& error) {
        if (size > static_cast<uint64_t>(std::numeric_limits<qint64>::max()) || file_.write(data, static_cast<qint64>(size)) != static_cast<qint64>(size)) {
            error = "Could not write the ZIP file"; return false;
        }
        if (!entries_.empty()) entries_.back().crc = crc(entries_.back().crc ^ 0xffffffffu, data, static_cast<qsizetype>(size)) ^ 0xffffffffu;
        written_ += size; return true;
    }
    bool writeRaw(const char* data, qint64 size) { return file_.write(data, size) == size; }
    bool number(uint64_t value, int bytes, std::string& error) {
        char data[8]; for (int i = 0; i < bytes; ++i) data[i] = static_cast<char>(value >> (i * 8));
        if (!writeRaw(data, bytes)) { error = "Could not write the ZIP file"; return false; }
        written_ += bytes; return true;
    }
    QSaveFile file_; const std::atomic_bool* cancelled_; std::vector<Entry> entries_; uint64_t written_ = 0;
};

fs::path resolve(const std::string& absolute, const std::string& relative, const fs::path& root) {
    std::error_code ec;
    if (!relative.empty()) { const fs::path p = root / platform::fromUtf8(relative); if (fs::is_regular_file(p, ec)) return p; }
    return platform::fromUtf8(absolute);
}
bool sameFile(const fs::path& a, const fs::path& b) { std::error_code ec; return fs::equivalent(a, b, ec) && !ec; }

} // namespace

bool packageProject(const Project& project, const fs::path& projectFile, const fs::path& output, std::string* error, const std::atomic_bool* cancelled) {
    auto fail = [&](std::string text) { if (error) *error = std::move(text); return false; };
    if (output.empty()) return fail("Choose where to save the package");
    const fs::path root = projectFile.empty() ? fs::current_path() : projectFile.parent_path();
    Project copy = project;
    struct File { fs::path source; std::string entry; };
    std::vector<File> files;
    std::vector<fs::path> known;
    auto include = [&](const std::string& absolute, const std::string& relative, std::string& newAbsolute, std::string& newRelative) -> bool {
        const fs::path source = resolve(absolute, relative, root);
        std::error_code ec;
        if (!fs::is_regular_file(source, ec)) return false;
        for (size_t i = 0; i < known.size(); ++i) if (sameFile(source, known[i])) { newAbsolute = files[i].entry; newRelative = files[i].entry; return true; }
        const std::string leaf = platform::toUtf8(source.filename());
        const std::string entry = "assets/" + std::to_string(files.size() + 1) + "-" + (leaf.empty() ? "file" : leaf);
        known.push_back(source); files.push_back({source, entry}); newAbsolute = entry; newRelative = entry; return true;
    };
    for (MediaItem& media : copy.comp.media) {
        std::string path, relative;
        if (!include(media.path, media.relative, path, relative)) return fail("Missing media file: " + (media.relative.empty() ? media.path : media.relative));
        media.path = path; media.relative = relative;
    }
    for (Layer& layer : copy.comp.layers) for (PluginParam& param : layer.plugin.params) if (param.kind == PluginParamKind::File && (!param.text.empty() || !param.relative.empty())) {
        std::string path, relative;
        if (!include(param.text, param.relative, path, relative)) return fail("Missing plugin file: " + (param.relative.empty() ? param.text : param.relative));
        param.text = path; param.relative = relative;
    }
    for (Layer& layer : copy.comp.layers) for (Effect& effect : layer.effects) if (!effect.file.empty() && effect.file.rfind("bundled:", 0) != 0) {
        std::string path, relative;
        if (!include(effect.file, effect.relative, path, relative)) return fail("Missing LUT file: " + (effect.relative.empty() ? effect.file : effect.relative));
        effect.file = path; effect.relative = relative;
    }
    if (!projectFile.empty() && sameFile(output, projectFile)) return fail("Choose a ZIP file outside the project file");
    for (const File& f : files) if (sameFile(output, f.source)) return fail("Choose a ZIP file outside the project media");
    ZipWriter zip(output, cancelled); std::string detail;
    if (!zip.open(detail)) return fail(detail);
    if (!zip.add("Project.bemotion", serializeProject(copy), detail)) return fail(detail);
    for (const File& f : files) { if (cancelled && cancelled->load()) return fail("Cancelled"); if (!zip.add(f.entry, f.source, detail)) return fail(detail); }
    if (!zip.finish(detail)) return fail(detail);
    return true;
}

} // namespace bemotion
