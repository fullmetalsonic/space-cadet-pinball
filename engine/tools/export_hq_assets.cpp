#include "pch.h"
#include "GroupData.h"
#include "partman.h"
#include "gdrv.h"
#include "zdrv.h"
#include "pb.h"
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace {
std::string quoted(const std::string& text) {
    std::ostringstream out; out << '"';
    const char* hex = "0123456789abcdef";
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') out << '\\' << static_cast<char>(c);
        else if (c < 32) out << "\\u00" << hex[c >> 4] << hex[c & 15];
        else out << static_cast<char>(c);
    }
    return out.str() + '"';
}
void writeFile(const std::string& name, const std::vector<uint8_t>& data) {
    std::ofstream out(name, std::ios::binary); if (!out) throw std::runtime_error("Cannot create " + name);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!out) throw std::runtime_error("Cannot write " + name);
}
void writePnm(const std::string& name, const char* magic, int w, int h, const std::vector<uint8_t>& data) {
    std::ofstream out(name, std::ios::binary); if (!out) throw std::runtime_error("Cannot create " + name);
    out << magic << '\n' << w << ' ' << h << "\n255\n";
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!out) throw std::runtime_error("Cannot write " + name);
}
}

int main(int argc, char** argv) {
    if (argc != 4) { std::cerr << "Usage: pinball_export_hq_assets <PINBALL.DAT> <new-existing-output-dir> <source-sha256>\n"; return 2; }
    const std::string source = argv[1], output = argv[2], sourceSha = argv[3];
    if (sourceSha.size() != 64 || sourceSha.find_first_not_of("0123456789abcdef") != std::string::npos) return 2;
    if (std::ifstream(output + "/inventory.json")) { std::cerr << "Refusing to replace inventory\n"; return 2; }
    try {
        datFileHeader header{};
        std::ifstream input(source, std::ios::binary); input.read(reinterpret_cast<char*>(&header), sizeof header);
        if (!input) throw std::runtime_error("Cannot read original DAT header");
        SDL_SetMainReady(); if (SDL_Init(0)) throw std::runtime_error(SDL_GetError());
        pb::FullTiltMode = pb::FullTiltDemoMode = false;
        pb::record_table = partman::load_records(source.c_str(), false);
        if (!pb::record_table) throw std::runtime_error("Engine DAT decoding failed");
        auto palette = reinterpret_cast<ColorRgba*>(pb::record_table->field_labeled("background", FieldTypes::Palette));
        if (!palette) throw std::runtime_error("Background palette unavailable");
        gdrv::display_palette(palette);
        const auto* paletteBytes = reinterpret_cast<const uint8_t*>(palette);
        writeFile(output + "/source-palette.bgra32le", std::vector<uint8_t>(paletteBytes, paletteBytes + 1024));
        std::ofstream manifest(output + "/inventory.json", std::ios::binary);
        if (!manifest) throw std::runtime_error("Output directory must already exist");
        manifest << "{\n\"schema\":1,\"sourceSha256\":" << quoted(sourceSha)
                 << ",\"sourceDatGroupCount\":" << header.NumberOfGroups
                 << ",\"normalizedGroupCount\":" << pb::record_table->Groups.size()
                 << ",\"orientation\":\"top-down after engine GroupData and palette normalization\",\n\"groups\":[";
        bool comma = false;
        for (const auto* group : pb::record_table->Groups) {
            if (comma) manifest << ','; comma = true;
            manifest << "{\"groupId\":" << group->GroupId << ",\"name\":" << quoted(group->GroupName) << ",\"entries\":[";
            bool entryComma = false;
            for (const auto* entry : group->GetEntries()) {
                if (entryComma) manifest << ','; entryComma = true;
                manifest << "{\"type\":" << static_cast<int>(entry->EntryType) << ",\"fieldSize\":" << entry->FieldSize << '}';
            }
            manifest << "]}";
        }
        manifest << "],\n\"assets\":["; comma = false; unsigned count = 0;
        for (const auto* group : pb::record_table->Groups) for (int resolution = 0; resolution < 3; ++resolution) {
            const auto* bmp = group->GetBitmap(resolution); const auto* depth = group->GetZMap(resolution);
            if (!bmp) continue;
            if (bmp->Width <= 0 || bmp->Height <= 0) throw std::runtime_error("Unexpected empty bitmap");
            const auto key = "g" + std::to_string(group->GroupId) + "_r" + std::to_string(resolution);
            const auto size = static_cast<size_t>(bmp->Width) * bmp->Height;
            std::vector<uint8_t> rgb(size * 3), bgra(size * 4), nonzero(size), alpha(size), rect(size, 255), indexed(size);
            size_t i = 0;
            for (int y = 0; y < bmp->Height; ++y) for (int x = 0; x < bmp->Width; ++x, ++i) {
                const auto color = bmp->BmpBufPtr1[y * bmp->Stride + x];
                rgb[i * 3] = color.GetRed(); rgb[i * 3 + 1] = color.GetGreen(); rgb[i * 3 + 2] = color.GetBlue();
                bgra[i * 4] = color.GetBlue(); bgra[i * 4 + 1] = color.GetGreen(); bgra[i * 4 + 2] = color.GetRed(); bgra[i * 4 + 3] = color.GetAlpha();
                nonzero[i] = color.Color ? 255 : 0; alpha[i] = color.GetAlpha();
                indexed[i] = static_cast<uint8_t>(bmp->IndexedBmpPtr[(bmp->Height - y - 1) * bmp->IndexedStride + x]);
            }
            writePnm(output + '/' + key + ".ppm", "P6", bmp->Width, bmp->Height, rgb);
            writeFile(output + '/' + key + ".bgra32le", bgra);
            writeFile(output + '/' + key + ".indexed8", indexed);
            writePnm(output + '/' + key + ".color-nonzero.pgm", "P5", bmp->Width, bmp->Height, nonzero);
            writePnm(output + '/' + key + ".engine-alpha.pgm", "P5", bmp->Width, bmp->Height, alpha);
            writePnm(output + '/' + key + ".rectangle-coverage.pgm", "P5", bmp->Width, bmp->Height, rect);
            const auto validDepth = depth && depth->Width == bmp->Width && depth->Height == bmp->Height;
            if (validDepth) {
                std::vector<uint8_t> bytes(size * 2), hint(size);
                for (int y = 0; y < depth->Height; ++y) for (int x = 0; x < depth->Width; ++x) {
                    const auto index = static_cast<size_t>(y) * depth->Width + x; const auto z = depth->ZPtr1[y * depth->Stride + x];
                    bytes[index * 2] = z & 255; bytes[index * 2 + 1] = z >> 8; hint[index] = z != 0xffff ? 255 : 0;
                }
                writeFile(output + '/' + key + ".depth16le", bytes);
                writePnm(output + '/' + key + ".depth-not-ffff-hint.pgm", "P5", bmp->Width, bmp->Height, hint);
            }
            if (comma) manifest << ','; comma = true; ++count;
            manifest << "\n{\"key\":" << quoted(key) << ",\"groupId\":" << group->GroupId << ",\"name\":" << quoted(group->GroupName)
                     << ",\"resolution\":" << resolution << ",\"width\":" << bmp->Width << ",\"height\":" << bmp->Height
                     << ",\"xPosition\":" << bmp->XPosition << ",\"yPosition\":" << bmp->YPosition
                     << ",\"stride\":" << bmp->Stride << ",\"indexedStride\":" << bmp->IndexedStride
                     << ",\"normalizedBitmapType\":" << static_cast<int>(bmp->BitmapType)
                     << ",\"generatedEmbeddedFont\":" << (group->GroupId >= header.NumberOfGroups ? "true" : "false")
                     << ",\"depthEntryPresent\":" << (depth ? "true" : "false") << ",\"depthWidth\":" << (depth ? depth->Width : 0)
                     << ",\"depthHeight\":" << (depth ? depth->Height : 0) << ",\"depthStride\":" << (depth ? depth->Stride : 0)
                     << ",\"depthPayload\":" << (validDepth ? "true" : "false") << '}';
        }
        manifest << "\n],\"assetCount\":" << count << "}\n";
        if (!manifest) throw std::runtime_error("Inventory write failed");
        delete pb::record_table; pb::record_table = nullptr; SDL_Quit();
        std::cout << "Exported " << count << " normalized bitmaps; no source metadata modified\n"; return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; delete pb::record_table; pb::record_table = nullptr; SDL_Quit(); return 1;
    }
}
