#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct gdrv_bitmap8;
struct zmap_header_type;
struct ColorRgba;
struct SDL_Texture;

// Display-only sidecars. Original bitmap dimensions, anchors and depth buffers stay unchanged.
class hq_render
{
public:
    struct Binding { int Group; unsigned Resolution; gdrv_bitmap8* Bitmap; };
    struct FrameSample { double CompositionMs, UploadMs; };
    class OverlayScope
    {
    public:
        explicit OverlayScope(const void* owner);
        ~OverlayScope();
        OverlayScope(const OverlayScope&) = delete;
        OverlayScope& operator=(const OverlayScope&) = delete;
    private:
        bool Capturing;
    };

    static void Initialize(gdrv_bitmap8* screen, zmap_header_type* depth,
        gdrv_bitmap8* const* ballBackups, size_t backupCount, const std::vector<Binding>& bindings,
        const std::string& sourceData, const std::string& packPath);
    static void Shutdown();
    static bool Enable(bool enabled);
    // Uses the same strict loader. Partial packs are permitted only by an explicit test request.
    static bool LoadPackForTest(const std::string& path, bool requireComplete = true);
    static bool Enabled();
    static int Scale();
    static const std::string& Error();
    static SDL_Texture* UploadTexture();
    static void RecreateTexture();
    static void BeginRebuild();
    static void ReplayOverlays();
    static void ForgetBitmap(gdrv_bitmap8* bitmap);
    static void ForgetDepth(zmap_header_type* depth);
    static void ForgetOverlay(const void* owner);

    static void Fill(gdrv_bitmap8* bitmap, int width, int height, int x, int y, ColorRgba color);
    static void Copy(gdrv_bitmap8* destination, int width, int height, int x, int y,
        gdrv_bitmap8* source, int sourceX, int sourceY, bool transparent);
    static void FillDepth(zmap_header_type* depth, int width, int height, int x, int y, uint16_t value);
    static void Paint(int width, int height, gdrv_bitmap8* destination, int x, int y,
        zmap_header_type* destinationDepth, int depthX, int depthY, gdrv_bitmap8* source,
        int sourceX, int sourceY, zmap_header_type* sourceDepth, int sourceDepthX, int sourceDepthY);
    static void PaintFlat(int width, int height, gdrv_bitmap8* destination, int x, int y,
        zmap_header_type* depth, int depthX, int depthY, gdrv_bitmap8* source,
        int sourceX, int sourceY, uint16_t value);
    static void Scroll(gdrv_bitmap8* bitmap, int offset);

    static std::array<uint8_t, 32> Sha256(const void* data, size_t length);
    static std::string Sha256Hex(const void* data, size_t length);
    static void Measure(bool enabled);
    static const std::vector<FrameSample>& Samples();
    static const std::vector<ColorRgba>* ScreenPixelsForTest();
    static const std::vector<uint16_t>* DepthPixelsForTest();
};
