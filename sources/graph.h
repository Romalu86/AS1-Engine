#pragma once
#include "core/types.h"
#include "core/as_string.h"
#include "core/configuration.h"
#include "graphics/rect.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace as1::win
{
    struct DialogItemRef;
}

namespace as1
{

    extern DWORD g_color16RedMask;
    extern DWORD g_color16GreenMask;
    extern DWORD g_color16RedShift;
    extern DWORD g_color16GreenShift;

    extern DWORD g_packedSoftwareDepth;
    extern WORD g_softwareDepthWordPrimary;
    extern WORD g_softwareDepthWordSecondary;
    extern const DWORD* g_softwarePaletteLookup;

    extern int g_softwareClipLeft; // left
    extern int g_softwareClipRight; // right
    extern int g_softwareClipTop; // top
    extern int g_softwareClipBottom; // bottom

    class RESOURCE;
    class MAP;
    class SPRITE;
    class VID;
    class VID_HARDWARE_Z;
    class BASE_TEXTURE;
    class GraphTextFont;

    struct GraphEffectGammaRawPair
    {

        DWORD inverseMask = 0;
        DWORD color = 0;
    };


    GraphEffectGammaRawPair* buildEffectGammaPair(GraphEffectGammaRawPair* destination, DWORD mask, DWORD color);


    struct GraphDisplayModeEntry
    {
        DWORD width = 0;
        DWORD height = 0;
        DWORD displayFormat = 0;
        DWORD colorBits = 0;
        DWORD depthStencilFormat = 0;
    };

    struct GraphDisplayModeCatalog
    {
        bool recorded = false;
        int adapter = 0;
        STRING adapterDescription;
        DWORD desktopWidth = 0;
        DWORD desktopHeight = 0;
        DWORD desktopRefreshRate = 0;
        DWORD desktopFormat = 0;
        DWORD capsFlags = 0;
        DWORD maxPixelShaderValueRaw = 0;
        DWORD videoMemoryBudgetBytes = 8000000u;
        DWORD rawReturnValue = 0;
        std::vector<GraphDisplayModeEntry> modes;
    };


    struct GraphAdapterRecord
    {
        char description[0x28];
        std::uint8_t rawIdentifierTail28[0x14];
        DWORD videoMemoryBudgetBytes;
        DWORD displayModeCount;
        DWORD displayModeWidths[16];
        DWORD displayModeHeights[16];
        DWORD displayModeFormats[16];
        DWORD depthStencilFormats[16];
        DWORD desktopDisplayFormat;
        DWORD capabilityFlags;

        int findDisplayModeIndex(int width, int height, int bitsPerPixel) const noexcept;
        STRING& formatDisplayModeLabel(STRING& output, int modeIndex) const;
    };


    struct GraphHostState;

    struct GraphDeviceInitState
    {

        bool recorded = false;
        bool constructorBootstrapRecorded = false;
        DWORD startupFlags = 0;
        DWORD direct3DCreateSdkVersion = 0;
        bool direct3DCreateSucceeded = false;
        DWORD enumeratedAdapterCount = 0;
        DWORD rawFlagsBeforeCaps = 0;
        float requestedWidth = 0.0f;
        float requestedHeight = 0.0f;
        int selectedDisplayMode = 0;
        DWORD selectedBackBufferFormat = 0;
        DWORD selectedDisplayFormat = 0;
        std::string selectedDisplayLog;
        DWORD initField0 = 1;
        DWORD initField1 = 0;
        DWORD initField2 = 0;
        DWORD initField3 = 0;
        DWORD initField4 = 0;
        DWORD initField5 = 0;
        DWORD initField6 = 1;
        DWORD createDeviceType = 1u;
        DWORD createDeviceFirstBehavior = 0x40u;
        DWORD createDeviceSecondBehavior = 0x20u;
        bool createDeviceFirstAttempted = false;
        bool createDeviceSecondAttempted = false;
        DWORD createDeviceFirstResult = 0;
        DWORD createDeviceSecondResult = 0;
        bool attemptedBackBuffer = false;
        DWORD backBufferResult = 0;
        bool backBufferAcquired = false;
        bool renderStatesAreZeroed = false;
        DWORD rawFlagsAfterCaps = 0;
        DWORD capsMirror = 0;
        DWORD devCaps = 0;
        DWORD textureCaps = 0;
        DWORD textureOpCaps = 0;
        DWORD maxTextureWidth = 0;
        DWORD maxTextureHeight = 0;
        DWORD maxPixelShaderValueRaw = 0;
        DWORD compressedFormatMask = 0;
        bool capsReadSucceeded = false;
        bool tempBufferCreateRequired = false;
        DWORD tempBufferFormat = 0;
        bool tempBufferCreated = false;
        DWORD tempBufferCreateResult = 0;
        bool softwareTempBufferCreateRequired = false;
        DWORD softwareTempBufferBytes = 0;
        DWORD softwareTempBufferWidth = 0;
        bool softwareTempBufferCreated = false;
        bool teardownMovieCleanupBefore = false;
        bool teardownMovieCleanupAfter = false;
        int deviceReleaseResult = 0;
        int backBufferReleaseResult = 0;
        int direct3DReleaseResult = 0;
    };


    struct GraphProjectionState
    {

        bool recorded = false;
        DWORD transformStateArg = 3;
        DWORD setTransformSlot = 0x94u;
        float projectionScaleX = 2.0f;
        float projectionScaleY = -2.0f;
        float projectionOne = 1.0f;
        float projectionDepthBias = 0.0049999999f;
        bool matrixLocalStartsAtViewportTail = true;
        bool zeroesMostMatrixSlots = true;
        float matrix00 = 0.0f;
        float matrix11 = 0.0f;
        float matrix22 = 0.0f;
        float matrix33 = 1.0f;
        DWORD setTransformResult = 0;
        bool setTransformSubmitted = false;
        bool setTransformSucceeded = false;
    };

    struct GraphViewportState
    {

        float left = 0.0f;
        float top = 0.0f;
        float right = 0.0f;
        float bottom = 0.0f;

        // Exact D3DVIEWPORT8 payload sent before projection transform.
        DWORD viewportX = 0;
        DWORD viewportY = 0;
        DWORD viewportWidth = 0;
        DWORD viewportHeight = 0;
        float minZ = 0.0f;
        float maxZ = 1.0f;

        bool projectionTransformRequired = false;
        GraphProjectionState projectionStack{};
    };

    class GRAPH
    {
    public:
        GRAPH();
        ~GRAPH();

        int init(void* hWnd);

        int initializeWindowDevice(void* hWnd);
        void deinit();
        bool isDeviceReady() const;
        const std::string& deviceStatus() const;
        static void* CurrentDeviceHandle();
        static void* CurrentDevice();
        void* direct3DHandle() const { return m_direct3D; }
        void* deviceHandle() const { return m_device; }
        BASE_TEXTURE* lightBuffer() const { return m_lightBuffer; }
        std::uint16_t intensityPaletteEntry(std::size_t index) const noexcept { return m_intensityPalette16[index]; }
        BASE_TEXTURE* hiBuffer() const { return m_hiBuffer; }
        BASE_TEXTURE* alphaBuffer() const { return m_alphaBuffer; }
        void* temporarySurface() const { return m_tempBuffer; }
        bool presentClear();
        bool Tact() { return presentClear(); }

        void DrawText(float x, float y, const char* format, ...);

        void DrawLine(float x1, float y1, float x2, float y2, DWORD color);
        // Physical retail owners used by the line/pixel draw path.
        void drawBackBufferPixel(float x, float y, DWORD color);
        void drawLineRaster(float x0, float y0, float x1, float y1, DWORD color);
        void DrawRect(float left, float top, float right, float bottom, DWORD color);

        int beginSceneWithDeviceRecovery();
        int endSceneAndPresentRetail(int presentFlag);
        int setViewportRetail(float left, float top, float right, float bottom);
        bool BeginScene();
        bool EndSceneAndPresent(bool doPresent);
        void SetViewport(float left, float top, float right, float bottom);
        void Clear(DWORD color);
        int drawTextureRectClipped(const RECTI& destination, const RECTI& source, BASE_TEXTURE& texture);
        int CopyTextureRectToSoftwareBackBuffer(BASE_TEXTURE& texture, const RECTI& destination, const RECTI& source);
        int drawPrimitiveUp(DWORD primitiveType, DWORD vertexShader, const void* vertexData, DWORD vertexStride, int vertexCount);
        int DrawPrimitiveList(DWORD primitiveType, DWORD vertexShader, const void* vertexData, DWORD vertexStride, int vertexCount);
        int SetLayer(int layer, DWORD textureToken, DWORD srcToken, DWORD flags, DWORD fallbackFlags);
        const GraphDeviceInitState& deviceInitState() const;
        const GraphDisplayModeCatalog& displayModeCatalog() const;
        const GraphViewportState& viewportState() const;
        double getViewportLeft() const noexcept; // [GRAPH+0x254] left
        double getViewportRight() const noexcept; // [GRAPH+0x258] right
        double getViewportTop() const noexcept; // [GRAPH+0x25C] top
        double getViewportBottom() const noexcept; // [GRAPH+0x260] bottom
        void rawSetSoftwareClipBounds(int left, int top, int right, int bottom) noexcept
        {
            g_softwareClipLeft = left;
            g_softwareClipTop = top;
            g_softwareClipRight = right;
            g_softwareClipBottom = bottom;
        }
        static GRAPH* CurrentGraph();

        static void BindCurrentGraph(GRAPH* graph) noexcept;
        int setAlphaBlendFactors(DWORD srcBlend, DWORD dstBlend);
        int clearFrameBuffers(DWORD color);
        int setRenderStateCached(DWORD renderState, DWORD value);
        void runFrameService(int worldTickFlag);
        void updateRenderPulse();
        void drawFogBufferOverlay(float left, float top, float right, float bottom, int a6, int a7, DWORD colorMask, const WORD* ramp, int baseDepth, int blendFlag);
        void drawSnowLightBuffer();
        int drawLineParticles();
        int drawCrossParticles();
        int playMovieCentered(const STRING& moviePath);
        int drawFormattedText(float x, float y, const char* format, ...);
        int openMoviePlayback(const STRING& moviePath, int centerX, int centerY);
        int lockBackBuffer();
        void reloadPaletteLightBuffer();
        DWORD advanceMovieFrameClock(VID* movieVid);
        DWORD* sampleBackBufferPixel(DWORD* colorOut, float x, float y);

        void drawBackBufferPixel2x2(float x, float y, DWORD color);
        int captureBackBufferRegionTga(const STRING& outputPath, int x, int y, int width, int height);
        void drawVidFrame(VID* vid, int cadr, float x, float y, float z);
        int logGraphResourceError(int value1, const char* message, int value2);
        int saveGraphParameters(RESOURCE* stream);
        const GraphAdapterRecord& selectedAdapterRecord() const noexcept;
        GraphAdapterRecord& selectedAdapterRecord() noexcept;
#ifdef _WIN32
        int syncDisplayModeDialog(const win::DialogItemRef& deviceRef, const win::DialogItemRef& modeRef, const win::DialogItemRef* fullscreenRef);
#endif
        int drawAlphaOverlayQuad(int leftRaw, int topRaw, int rightRaw, int bottomRaw, int colorRaw);
        int drawAdditiveOverlayQuad(int leftRaw, int topRaw, int rightRaw, int bottomRaw, int colorRaw);

        void DrawEffect(int drawEffects);
        int unlockBackBufferIfLocked();
        int backBufferPitchPixels() const { return m_backBufferPitchPixels; }
        void* backBufferPixels() const { return m_lockedBackBufferPixels; }
        void* backBufferSurface() const { return m_backBuffer; }

        std::uint16_t* softwareDepthBuffer() { return m_softwareDepthBuffer; }
        const std::uint16_t* softwareDepthBuffer() const { return m_softwareDepthBuffer; }
        int softwareDepthPitch() const { return m_softwareDepthPitch; }

        float screenWidth() const noexcept { return m_sizeX; }
        float screenHeight() const noexcept { return m_sizeY; }
        int isMoviePlaybackComplete();
        void releaseMoviePlaybackIfComplete();
        int releaseMoviePlayback();
        int enterModalRenderState();
        int leaveModalRenderState();
        int updateRenderFlags(DWORD mask);
        void resetMapRenderRuntimeState();
        void* movieComObject(std::size_t index) const { return index < 4u ? m_movieComObjects[index] : nullptr; }
        DWORD renderFlags() const { return m_renderFlags; }
        void SetCamera(float x, float y);
        // Camera compatibility note: MAP camera code stores the camera top-left after
        // subtracting half screen from the requested center and clamping to the map rectangle.
        float cameraX() const;
        float cameraY() const;

        // MAP::startLoadMap calls GRAPH::LoadParameters before reading HEAD.
        // AS1 GRPH payload layout is kept readable here: environment, gamma pair, wind, optional sunlight.
        void LoadParameters(RESOURCE* map);
        void chunk_LoadParameters(RESOURCE* map) { LoadParameters(map); }

        int SizeX() const { return static_cast<int>(m_sizeX); }
        int SizeY() const { return static_cast<int>(m_sizeY); }
        VECTOR2 SizeXY() const { return VECTOR2{m_sizeX, m_sizeY}; }
        float DiffScreenScale() const { return 1.0f; }

        DWORD gammaDiffuse() const { return m_gammaPair.first; }
        DWORD gammaSpecular() const { return m_gammaPair.second; }
        DWORD windDirection() const { return m_windDirection; }
        float windSpeed() const { return m_windSpeed; }

        void SetWind(DWORD direction, float speed);

        void setGamma(const GammaRawPair& rawGamma);
        void setGamma(DWORD diffuse, DWORD specular);
        const GammaRawPair& rawGammaPair() const { return m_gammaPair; }
        static const GammaRawPair* CurrentRawGammaPair();
        DWORD gamma() const { return m_gammaPair.first; }
        int getEffectState(int effect) const;
        int setEffect(int effect, int argument1, int argument2, int duration);

        bool GraphFlag34Bit0() const { return (m_graphFlags & 0x00000001u) != 0; }

        bool GraphFlag34Bit1() const { return (m_graphFlags & 0x00000002u) != 0; }
        bool GraphFlag34Bit7() const { return (m_graphFlags & 0x00000080u) != 0; }
        bool lastGammaRefreshChanged() const;
        size_t lastGammaRefreshScannedSlots() const;
        size_t lastGammaRefreshRequests() const;
        size_t lastGammaRefreshLoadedSlots() const;
        size_t lastGammaRefreshSoftwareBlockedSlots() const;
        DWORD lastChunkGamma() const;
        bool isUseSoftZBuffer() const;

        void drawLightSourceRaw(float x, float y, float z, float sizeX, float sizeY, DWORD color);
        void DrawLightSource(float x, float y, float z, float sizeX, float sizeY, DWORD color);
        void drawRectOutlineRaster(float left, float top, float right, float bottom, DWORD color);
        void setScreenSize(int x, int y) { if (x > 0) m_sizeX = static_cast<float>(x); if (y > 0) m_sizeY = static_cast<float>(y); }

        GRAPH* initializeRetailGraphState(const as1::core::StartupSettingsBlock& startupSettings);
        void SetStartupFont(const STRING& face, int sizeX, int sizeY);

        int rebuildTextFont(const STRING& face, int sizeX, int sizeY);
        int drawTextColored(float x, float y, const char* text, DWORD color);
        int drawStringColored(float x, float y, const STRING& text, DWORD color);
        const STRING& startupFontFace() const;
        int startupFontSizeX() const;
        int startupFontSizeY() const;


        static const char* D3DFormatToString(DWORD format);

    private:
        friend class VID;
        friend class VID_HARDWARE_Z;

        GraphHostState& hostState() noexcept;
        const GraphHostState& hostState() const noexcept;
        void releaseHostState() noexcept;

        std::uint32_t m_presentParameters[13];
        DWORD m_graphFlags;                       // +0x034
        DWORD m_maxPixelShaderValueRaw;          // +0x038
        std::array<std::uint16_t, 256> m_intensityPalette16; // +0x03C..+0x23B
        void* m_lockedBackBufferPixels;   // +0x23C
        float m_sizeX;                      // +0x240
        float m_sizeY;                      // +0x244
        int m_backBufferPitchPixels;                // +0x248
        std::uint16_t* m_softwareDepthBuffer; // +0x24C
        int m_softwareDepthPitch;             // +0x250
        float m_viewportLeft;              // +0x254
        float m_viewportRight;             // +0x258
        float m_viewportTop;               // +0x25C
        float m_viewportBottom;            // +0x260
        DWORD m_adapterCount;                 // +0x264
        GraphAdapterRecord m_adapterRecords[8]; // +0x268..+0xCC7
        int m_selectedAdapterIndex;                     // +0xCC8
        std::uint32_t m_effectSnapshotXBits;    // +0xCCC
        std::uint32_t m_effectSnapshotYBits;    // +0xCD0
        std::uint32_t m_effectArgument1[16];   // +0xCD4
        std::uint32_t m_effectArgument2[16];   // +0xD14
        std::uint32_t m_effectStartTimes[16]; // +0xD54
        std::uint32_t m_effectDurations[16];  // +0xD94
        GammaRawPair m_gammaPair;               // +0xDD4
        DWORD m_renderFlags;                  // +0xDDC
        DWORD m_windDirection;                 // +0xDE0
        float m_windSpeed;              // +0xDE4
        void* m_movieComObjects[4]; // +0xDE8
        void* m_windowHandle;              // +0xDF8
        DWORD m_deviceLifecycleState;            // +0xDFC
        DWORD m_selectedDisplayFormat;        // +0xE00
        BASE_TEXTURE* m_lightBuffer;       // +0xE04
        BASE_TEXTURE* m_hiBuffer;          // +0xE08
        BASE_TEXTURE* m_alphaBuffer;       // +0xE0C
        void* m_direct3D;                  // +0xE10
        void* m_device;                    // +0xE14
        void* m_backBuffer;                // +0xE18
        void* m_tempBuffer;                // +0xE1C
        GraphTextFont* m_textFont;         // +0xE20

        bool DrawPixelToSoftwareBackBuffer(float x, float y, DWORD color);
        void buildAdapterRecord(GraphAdapterRecord& record, void* direct3D, int adapter,
                                const as1::core::StartupSettingsBlock& startupSettings);
    };

#if defined(_MSC_VER) && defined(_M_IX86)
#endif
}
