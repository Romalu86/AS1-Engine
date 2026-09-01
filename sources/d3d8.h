#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <unknwn.h>
#include <cstddef>

#if defined(_M_IX86) || defined(_X86_)
#pragma pack(push, 4)
#define AS1_D3D8_PACK4_ACTIVE 1
#endif

#ifndef D3D_SDK_VERSION
#define D3D_SDK_VERSION 120u
#endif

#ifndef D3DADAPTER_DEFAULT
#define D3DADAPTER_DEFAULT 0u
#endif

#ifndef D3D_OK
#define D3D_OK S_OK
#endif

#ifndef D3DERR_DEVICELOST
#define D3DERR_DEVICELOST ((HRESULT)0x88760868L)
#endif
#ifndef D3DERR_DEVICENOTRESET
#define D3DERR_DEVICENOTRESET ((HRESULT)0x88760869L)
#endif

typedef DWORD D3DCOLOR;
#define D3DCOLOR_ARGB(a,r,g,b) ((D3DCOLOR)((((DWORD)(a)&0xffu)<<24u)|(((DWORD)(r)&0xffu)<<16u)|(((DWORD)(g)&0xffu)<<8u)|((DWORD)(b)&0xffu)))
#define D3DCOLOR_XRGB(r,g,b) D3DCOLOR_ARGB(0xffu,(r),(g),(b))

struct IDirect3D8;
struct IDirect3DDevice8;
struct IDirect3DSwapChain8;
struct IDirect3DResource8;
struct IDirect3DBaseTexture8;
struct IDirect3DTexture8;
struct IDirect3DVolumeTexture8;
struct IDirect3DCubeTexture8;
struct IDirect3DVertexBuffer8;
struct IDirect3DIndexBuffer8;
struct IDirect3DSurface8;
struct IDirect3DVolume8;

#ifndef MAX_DEVICE_IDENTIFIER_STRING
#define MAX_DEVICE_IDENTIFIER_STRING 512
#endif

struct D3DADAPTER_IDENTIFIER8
{
    CHAR Driver[MAX_DEVICE_IDENTIFIER_STRING];
    CHAR Description[MAX_DEVICE_IDENTIFIER_STRING];
    LARGE_INTEGER DriverVersion;
    DWORD VendorId;
    DWORD DeviceId;
    DWORD SubSysId;
    DWORD Revision;
    GUID DeviceIdentifier;
    DWORD WHQLLevel;
};

#if defined(_M_IX86)
#endif

struct D3DCAPS8;
struct D3DDEVICE_CREATION_PARAMETERS;
struct D3DRASTER_STATUS;
struct D3DGAMMARAMP;
struct D3DMATRIX;
struct D3DVIEWPORT8;

struct D3DVIEWPORT8
{
    DWORD X;
    DWORD Y;
    DWORD Width;
    DWORD Height;
    float MinZ;
    float MaxZ;
};

struct D3DMATRIX
{
    union
    {
        struct
        {
            float _11, _12, _13, _14;
            float _21, _22, _23, _24;
            float _31, _32, _33, _34;
            float _41, _42, _43, _44;
        };
        float m[4][4];
    };
};
struct D3DMATERIAL8;
struct D3DLIGHT8;
struct D3DCLIPSTATUS8;
struct D3DVERTEXBUFFER_DESC;
struct D3DINDEXBUFFER_DESC;
struct D3DSURFACE_DESC;
struct D3DVOLUME_DESC;
struct D3DRECTPATCH_INFO;
struct D3DTRIPATCH_INFO;
struct D3DBOX;

struct D3DRECT { LONG x1; LONG y1; LONG x2; LONG y2; };
struct D3DLOCKED_RECT { INT Pitch; void* pBits; };
struct D3DLOCKED_BOX { INT RowPitch; INT SlicePitch; void* pBits; };

#define D3DCLEAR_TARGET  0x00000001l
#define D3DCLEAR_ZBUFFER 0x00000002l
#define D3DCLEAR_STENCIL 0x00000004l

#define D3DFVF_XYZRHW  0x000004u
#define D3DFVF_DIFFUSE 0x000040u
#define D3DFVF_TEX1    0x000100u

enum D3DDEVTYPE
{
    D3DDEVTYPE_HAL = 1,
    D3DDEVTYPE_REF = 2,
    D3DDEVTYPE_SW  = 3
};

enum D3DFORMAT
{
    D3DFMT_UNKNOWN       = 0,
    D3DFMT_R8G8B8        = 20,
    D3DFMT_A8R8G8B8      = 21,
    D3DFMT_X8R8G8B8      = 22,
    D3DFMT_R5G6B5        = 23,
    D3DFMT_X1R5G5B5      = 24,
    D3DFMT_A1R5G5B5      = 25,
    D3DFMT_A4R4G4B4      = 26,
    D3DFMT_R3G3B2        = 27,
    D3DFMT_A8            = 28,
    D3DFMT_A8R3G3B2      = 29,
    D3DFMT_X4R4G4B4      = 30,
    D3DFMT_A2B10G10R10   = 31,
    D3DFMT_A8B8G8R8      = 32,
    D3DFMT_X8B8G8R8      = 33,
    D3DFMT_G16R16        = 34,
    D3DFMT_A2R10G10B10   = 35,
    D3DFMT_A8P8          = 40,
    D3DFMT_P8            = 41,
    D3DFMT_L8            = 50,
    D3DFMT_A8L8          = 51,
    D3DFMT_A4L4          = 52,
    D3DFMT_V8U8          = 60,
    D3DFMT_L6V5U5        = 61,
    D3DFMT_X8L8V8U8      = 62,
    D3DFMT_Q8W8V8U8      = 63,
    D3DFMT_V16U16        = 64,
    D3DFMT_W11V11U10     = 65,
    D3DFMT_UYVY          = 0x59565955,
    D3DFMT_YUY2          = 0x32595559,
    D3DFMT_DXT1          = 0x31545844,
    D3DFMT_DXT2          = 0x32545844,
    D3DFMT_DXT3          = 0x33545844,
    D3DFMT_DXT4          = 0x34545844,
    D3DFMT_DXT5          = 0x35545844,
    D3DFMT_D16_LOCKABLE  = 70,
    D3DFMT_D32           = 71,
    D3DFMT_D15S1         = 73,
    D3DFMT_D24S8         = 75,
    D3DFMT_D24X8         = 77,
    D3DFMT_D24X4S4       = 79,
    D3DFMT_D16           = 80
};

enum D3DRESOURCETYPE
{
    D3DRTYPE_SURFACE       = 1,
    D3DRTYPE_VOLUME        = 2,
    D3DRTYPE_TEXTURE       = 3,
    D3DRTYPE_VOLUMETEXTURE = 4,
    D3DRTYPE_CUBETEXTURE   = 5,
    D3DRTYPE_VERTEXBUFFER  = 6,
    D3DRTYPE_INDEXBUFFER   = 7
};

enum D3DMULTISAMPLE_TYPE
{
    D3DMULTISAMPLE_NONE = 0
};

enum D3DSWAPEFFECT
{
    D3DSWAPEFFECT_DISCARD   = 1,
    D3DSWAPEFFECT_FLIP      = 2,
    D3DSWAPEFFECT_COPY      = 3,
    D3DSWAPEFFECT_COPY_VSYNC = 4
};

enum D3DBACKBUFFER_TYPE
{
    D3DBACKBUFFER_TYPE_MONO  = 0,
    D3DBACKBUFFER_TYPE_LEFT  = 1,
    D3DBACKBUFFER_TYPE_RIGHT = 2
};

enum D3DPOOL
{
    D3DPOOL_DEFAULT = 0,
    D3DPOOL_MANAGED = 1,
    D3DPOOL_SYSTEMMEM = 2,
    D3DPOOL_SCRATCH = 3
};

struct D3DSURFACE_DESC
{
    D3DFORMAT Format;
    D3DRESOURCETYPE Type;
    DWORD Usage;
    D3DPOOL Pool;
    UINT Size;
    D3DMULTISAMPLE_TYPE MultiSampleType;
    UINT Width;
    UINT Height;
};

enum D3DPRIMITIVETYPE
{
    D3DPT_POINTLIST     = 1,
    D3DPT_LINELIST      = 2,
    D3DPT_LINESTRIP     = 3,
    D3DPT_TRIANGLELIST  = 4,
    D3DPT_TRIANGLESTRIP = 5,
    D3DPT_TRIANGLEFAN   = 6
};

enum D3DCULL
{
    D3DCULL_NONE = 1,
    D3DCULL_CW   = 2,
    D3DCULL_CCW  = 3
};

enum D3DBLEND
{
    D3DBLEND_ZERO        = 1,
    D3DBLEND_ONE         = 2,
    D3DBLEND_SRCCOLOR    = 3,
    D3DBLEND_INVSRCCOLOR = 4,
    D3DBLEND_SRCALPHA    = 5,
    D3DBLEND_INVSRCALPHA = 6,
    D3DBLEND_DESTCOLOR   = 9
};


enum D3DCMPFUNC
{
    D3DCMP_NEVER        = 1,
    D3DCMP_LESS         = 2,
    D3DCMP_EQUAL        = 3,
    D3DCMP_LESSEQUAL    = 4,
    D3DCMP_GREATER      = 5,
    D3DCMP_NOTEQUAL     = 6,
    D3DCMP_GREATEREQUAL = 7,
    D3DCMP_ALWAYS       = 8
};

enum D3DTEXTUREFILTERTYPE
{
    D3DTEXF_NONE   = 0,
    D3DTEXF_POINT  = 1,
    D3DTEXF_LINEAR = 2
};

enum D3DTEXTUREADDRESS
{
    D3DTADDRESS_WRAP   = 1,
    D3DTADDRESS_MIRROR = 2,
    D3DTADDRESS_CLAMP  = 3
};

enum D3DTEXTUREOP
{
    D3DTOP_DISABLE    = 1,
    D3DTOP_SELECTARG1 = 2,
    D3DTOP_SELECTARG2 = 3,
    D3DTOP_MODULATE   = 4,
    D3DTOP_MODULATE2X = 5,
    D3DTOP_ADD        = 7
};

enum D3DTEXTURESTAGESTATETYPE
{
    D3DTSS_COLOROP   = 1,
    D3DTSS_COLORARG1 = 2,
    D3DTSS_COLORARG2 = 3,
    D3DTSS_ALPHAOP   = 4,
    D3DTSS_ALPHAARG1 = 5,
    D3DTSS_ALPHAARG2 = 6,
    D3DTSS_ADDRESSU  = 13,
    D3DTSS_ADDRESSV  = 14,
    D3DTSS_MAGFILTER = 16,
    D3DTSS_MINFILTER = 17
};

#define D3DTA_DIFFUSE 0x00000000u
#define D3DTA_CURRENT 0x00000001u
#define D3DTA_TEXTURE 0x00000002u

enum D3DRENDERSTATETYPE
{
    D3DRS_ZENABLE          = 7,
    D3DRS_SRCBLEND         = 19,
    D3DRS_DESTBLEND        = 20,
    D3DRS_CULLMODE         = 22,
    D3DRS_ZFUNC            = 23,
    D3DRS_ALPHAREF         = 24,
    D3DRS_ALPHAFUNC        = 25,
    D3DRS_ALPHABLENDENABLE = 27,
    D3DRS_ALPHATESTENABLE  = 15,
    D3DRS_LIGHTING         = 137
};

enum D3DTRANSFORMSTATETYPE
{
    D3DTS_VIEW       = 2,
    D3DTS_PROJECTION = 3,
    D3DTS_TEXTURE0   = 16,
    D3DTS_WORLD      = 256
};

#define D3DCREATE_FPU_PRESERVE                 0x00000002l
#define D3DCREATE_MULTITHREADED                0x00000004l
#define D3DCREATE_SOFTWARE_VERTEXPROCESSING    0x00000020l
#define D3DCREATE_HARDWARE_VERTEXPROCESSING    0x00000040l
#define D3DCREATE_MIXED_VERTEXPROCESSING       0x00000080l
#define D3DPRESENTFLAG_LOCKABLE_BACKBUFFER     0x00000001u
#define D3DPRESENT_INTERVAL_DEFAULT            0x00000000u
#define D3DPRESENT_INTERVAL_IMMEDIATE          0x80000000u

struct D3DDISPLAYMODE
{
    UINT Width;
    UINT Height;
    UINT RefreshRate;
    D3DFORMAT Format;
};

struct D3DCAPS8
{
    D3DDEVTYPE DeviceType;
    UINT AdapterOrdinal;
    DWORD Caps;
    DWORD Caps2;
    DWORD Caps3;
    DWORD PresentationIntervals;
    DWORD CursorCaps;
    DWORD DevCaps;
    DWORD PrimitiveMiscCaps;
    DWORD RasterCaps;
    DWORD ZCmpCaps;
    DWORD SrcBlendCaps;
    DWORD DestBlendCaps;
    DWORD AlphaCmpCaps;
    DWORD ShadeCaps;
    DWORD TextureCaps;
    DWORD TextureFilterCaps;
    DWORD CubeTextureFilterCaps;
    DWORD VolumeTextureFilterCaps;
    DWORD TextureAddressCaps;
    DWORD VolumeTextureAddressCaps;
    DWORD LineCaps;
    DWORD MaxTextureWidth;
    DWORD MaxTextureHeight;
    DWORD MaxVolumeExtent;
    DWORD MaxTextureRepeat;
    DWORD MaxTextureAspectRatio;
    DWORD MaxAnisotropy;
    float MaxVertexW;
    float GuardBandLeft;
    float GuardBandTop;
    float GuardBandRight;
    float GuardBandBottom;
    float ExtentsAdjust;
    DWORD StencilCaps;
    DWORD FVFCaps;
    DWORD TextureOpCaps;
    DWORD MaxTextureBlendStages;
    DWORD MaxSimultaneousTextures;
    DWORD VertexProcessingCaps;
    DWORD MaxActiveLights;
    DWORD MaxUserClipPlanes;
    DWORD MaxVertexBlendMatrices;
    DWORD MaxVertexBlendMatrixIndex;
    float MaxPointSize;
    DWORD MaxPrimitiveCount;
    DWORD MaxVertexIndex;
    DWORD MaxStreams;
    DWORD MaxStreamStride;
    DWORD VertexShaderVersion;
    DWORD MaxVertexShaderConst;
    DWORD PixelShaderVersion;
    float MaxPixelShaderValue;
};

#if defined(_M_IX86)
#endif

struct D3DPRESENT_PARAMETERS
{
    UINT BackBufferWidth;
    UINT BackBufferHeight;
    D3DFORMAT BackBufferFormat;
    UINT BackBufferCount;
    D3DMULTISAMPLE_TYPE MultiSampleType;
    D3DSWAPEFFECT SwapEffect;
    HWND hDeviceWindow;
    BOOL Windowed;
    BOOL EnableAutoDepthStencil;
    D3DFORMAT AutoDepthStencilFormat;
    DWORD Flags;
    UINT FullScreen_RefreshRateInHz;
    UINT FullScreen_PresentationInterval;
};

#if defined(_M_IX86)
#endif

struct IDirect3D8 : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE RegisterSoftwareDevice(void* pInitializeFunction) = 0;
    virtual UINT STDMETHODCALLTYPE GetAdapterCount() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAdapterIdentifier(UINT Adapter, DWORD Flags, D3DADAPTER_IDENTIFIER8* pIdentifier) = 0;
    virtual UINT STDMETHODCALLTYPE GetAdapterModeCount(UINT Adapter) = 0;
    virtual HRESULT STDMETHODCALLTYPE EnumAdapterModes(UINT Adapter, UINT Mode, D3DDISPLAYMODE* pMode) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAdapterDisplayMode(UINT Adapter, D3DDISPLAYMODE* pMode) = 0;
    virtual HRESULT STDMETHODCALLTYPE CheckDeviceType(UINT Adapter, D3DDEVTYPE DevType, D3DFORMAT AdapterFormat, D3DFORMAT BackBufferFormat, BOOL Windowed) = 0;
    virtual HRESULT STDMETHODCALLTYPE CheckDeviceFormat(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, DWORD Usage, D3DRESOURCETYPE RType, D3DFORMAT CheckFormat) = 0;
    virtual HRESULT STDMETHODCALLTYPE CheckDeviceMultiSampleType(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SurfaceFormat, BOOL Windowed, D3DMULTISAMPLE_TYPE MultiSampleType) = 0;
    virtual HRESULT STDMETHODCALLTYPE CheckDepthStencilMatch(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, D3DFORMAT RenderTargetFormat, D3DFORMAT DepthStencilFormat) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceCaps(UINT Adapter, D3DDEVTYPE DeviceType, D3DCAPS8* pCaps) = 0;
    virtual HMONITOR STDMETHODCALLTYPE GetAdapterMonitor(UINT Adapter) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateDevice(UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DDevice8** ppReturnedDeviceInterface) = 0;
};

struct IDirect3DSurface8 : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice8** ppDevice) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID refguid, const void* pData, DWORD SizeOfData, DWORD Flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID refguid, void* pData, DWORD* pSizeOfData) = 0;
    virtual HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID refguid) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetContainer(REFIID riid, void** ppContainer) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDesc(D3DSURFACE_DESC* pDesc) = 0;
    virtual HRESULT STDMETHODCALLTYPE LockRect(D3DLOCKED_RECT* pLockedRect, const RECT* pRect, DWORD Flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE UnlockRect() = 0;
};


struct IDirect3DVertexBuffer8 : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice8** ppDevice) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID refguid, const void* pData, DWORD SizeOfData, DWORD Flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID refguid, void* pData, DWORD* pSizeOfData) = 0;
    virtual HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID refguid) = 0;
    virtual DWORD STDMETHODCALLTYPE SetPriority(DWORD PriorityNew) = 0;
    virtual DWORD STDMETHODCALLTYPE GetPriority() = 0;
    virtual void STDMETHODCALLTYPE PreLoad() = 0;
    virtual D3DRESOURCETYPE STDMETHODCALLTYPE GetType() = 0;
    virtual HRESULT STDMETHODCALLTYPE Lock(UINT OffsetToLock, UINT SizeToLock, BYTE** ppbData, DWORD Flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE Unlock() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDesc(D3DVERTEXBUFFER_DESC* pDesc) = 0;
};

struct IDirect3DIndexBuffer8 : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice8** ppDevice) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID refguid, const void* pData, DWORD SizeOfData, DWORD Flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID refguid, void* pData, DWORD* pSizeOfData) = 0;
    virtual HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID refguid) = 0;
    virtual DWORD STDMETHODCALLTYPE SetPriority(DWORD PriorityNew) = 0;
    virtual DWORD STDMETHODCALLTYPE GetPriority() = 0;
    virtual void STDMETHODCALLTYPE PreLoad() = 0;
    virtual D3DRESOURCETYPE STDMETHODCALLTYPE GetType() = 0;
    virtual HRESULT STDMETHODCALLTYPE Lock(UINT OffsetToLock, UINT SizeToLock, BYTE** ppbData, DWORD Flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE Unlock() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDesc(D3DINDEXBUFFER_DESC* pDesc) = 0;
};

struct IDirect3DTexture8 : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice8** ppDevice) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID refguid, const void* pData, DWORD SizeOfData, DWORD Flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID refguid, void* pData, DWORD* pSizeOfData) = 0;
    virtual HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID refguid) = 0;
    virtual DWORD STDMETHODCALLTYPE SetPriority(DWORD PriorityNew) = 0;
    virtual DWORD STDMETHODCALLTYPE GetPriority() = 0;
    virtual void STDMETHODCALLTYPE PreLoad() = 0;
    virtual D3DRESOURCETYPE STDMETHODCALLTYPE GetType() = 0;
    virtual DWORD STDMETHODCALLTYPE SetLOD(DWORD LODNew) = 0;
    virtual DWORD STDMETHODCALLTYPE GetLOD() = 0;
    virtual DWORD STDMETHODCALLTYPE GetLevelCount() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT Level, D3DSURFACE_DESC* pDesc) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSurfaceLevel(UINT Level, IDirect3DSurface8** ppSurfaceLevel) = 0;
    virtual HRESULT STDMETHODCALLTYPE LockRect(UINT Level, D3DLOCKED_RECT* pLockedRect, const RECT* pRect, DWORD Flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE UnlockRect(UINT Level) = 0;
    virtual HRESULT STDMETHODCALLTYPE AddDirtyRect(const RECT* pDirtyRect) = 0;
};

struct IDirect3DDevice8 : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE TestCooperativeLevel() = 0;
    virtual UINT STDMETHODCALLTYPE GetAvailableTextureMem() = 0;
    virtual HRESULT STDMETHODCALLTYPE ResourceManagerDiscardBytes(DWORD Bytes) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDirect3D(IDirect3D8** ppD3D8) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS8* pCaps) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDisplayMode(D3DDISPLAYMODE* pMode) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS* pParameters) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetCursorProperties(UINT XHotSpot, UINT YHotSpot, IDirect3DSurface8* pCursorBitmap) = 0;
    virtual void STDMETHODCALLTYPE SetCursorPosition(UINT XScreenSpace, UINT YScreenSpace, DWORD Flags) = 0;
    virtual BOOL STDMETHODCALLTYPE ShowCursor(BOOL bShow) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DSwapChain8** pSwapChain) = 0;
    virtual HRESULT STDMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS* pPresentationParameters) = 0;
    virtual HRESULT STDMETHODCALLTYPE Present(const RECT* pSourceRect, const RECT* pDestRect, HWND hDestWindowOverride, const RGNDATA* pDirtyRegion) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT BackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface8** ppBackBuffer) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetRasterStatus(D3DRASTER_STATUS* pRasterStatus) = 0;
    virtual void STDMETHODCALLTYPE SetGammaRamp(DWORD Flags, const D3DGAMMARAMP* pRamp) = 0;
    virtual void STDMETHODCALLTYPE GetGammaRamp(D3DGAMMARAMP* pRamp) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateTexture(UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DTexture8** ppTexture) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateVolumeTexture(UINT Width, UINT Height, UINT Depth, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DVolumeTexture8** ppVolumeTexture) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateCubeTexture(UINT EdgeLength, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DCubeTexture8** ppCubeTexture) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool, IDirect3DVertexBuffer8** ppVertexBuffer) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateIndexBuffer(UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DIndexBuffer8** ppIndexBuffer) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateRenderTarget(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, BOOL Lockable, IDirect3DSurface8** ppSurface) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateDepthStencilSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, IDirect3DSurface8** ppSurface) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateImageSurface(UINT Width, UINT Height, D3DFORMAT Format, IDirect3DSurface8** ppSurface) = 0;
    virtual HRESULT STDMETHODCALLTYPE CopyRects(IDirect3DSurface8* pSourceSurface, const RECT* pSourceRectsArray, UINT cRects, IDirect3DSurface8* pDestinationSurface, const POINT* pDestPointsArray) = 0;
    virtual HRESULT STDMETHODCALLTYPE UpdateTexture(IDirect3DBaseTexture8* pSourceTexture, IDirect3DBaseTexture8* pDestinationTexture) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetFrontBuffer(IDirect3DSurface8* pDestSurface) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetRenderTarget(IDirect3DSurface8* pRenderTarget, IDirect3DSurface8* pNewZStencil) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetRenderTarget(IDirect3DSurface8** ppRenderTarget) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDepthStencilSurface(IDirect3DSurface8** ppZStencilSurface) = 0;
    virtual HRESULT STDMETHODCALLTYPE BeginScene() = 0;
    virtual HRESULT STDMETHODCALLTYPE EndScene() = 0;
    virtual HRESULT STDMETHODCALLTYPE Clear(DWORD Count, const D3DRECT* pRects, DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetTransform(D3DTRANSFORMSTATETYPE State, const D3DMATRIX* pMatrix) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTransform(D3DTRANSFORMSTATETYPE State, D3DMATRIX* pMatrix) = 0;
    virtual HRESULT STDMETHODCALLTYPE MultiplyTransform(D3DTRANSFORMSTATETYPE State, const D3DMATRIX* pMatrix) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetViewport(const D3DVIEWPORT8* pViewport) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetViewport(D3DVIEWPORT8* pViewport) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetMaterial(const D3DMATERIAL8* pMaterial) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMaterial(D3DMATERIAL8* pMaterial) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetLight(DWORD Index, const D3DLIGHT8* pLight) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetLight(DWORD Index, D3DLIGHT8* pLight) = 0;
    virtual HRESULT STDMETHODCALLTYPE LightEnable(DWORD Index, BOOL Enable) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetLightEnable(DWORD Index, BOOL* pEnable) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetClipPlane(DWORD Index, const float* pPlane) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetClipPlane(DWORD Index, float* pPlane) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE State, DWORD Value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE State, DWORD* pValue) = 0;
    virtual HRESULT STDMETHODCALLTYPE BeginStateBlock() = 0;
    virtual HRESULT STDMETHODCALLTYPE EndStateBlock(DWORD* pToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE ApplyStateBlock(DWORD Token) = 0;
    virtual HRESULT STDMETHODCALLTYPE CaptureStateBlock(DWORD Token) = 0;
    virtual HRESULT STDMETHODCALLTYPE DeleteStateBlock(DWORD Token) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateStateBlock(DWORD Type, DWORD* pToken) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetClipStatus(const D3DCLIPSTATUS8* pClipStatus) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetClipStatus(D3DCLIPSTATUS8* pClipStatus) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTexture(DWORD Stage, IUnknown** ppTexture) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetTexture(DWORD Stage, IUnknown* pTexture) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD* pValue) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value) = 0;
    virtual HRESULT STDMETHODCALLTYPE ValidateDevice(DWORD* pNumPasses) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetInfo(DWORD DevInfoID, void* pDevInfoStruct, DWORD DevInfoStructSize) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPaletteEntries(UINT PaletteNumber, const PALETTEENTRY* pEntries) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPaletteEntries(UINT PaletteNumber, PALETTEENTRY* pEntries) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetCurrentTexturePalette(UINT PaletteNumber) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentTexturePalette(UINT* PaletteNumber) = 0;
    virtual HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount) = 0;
    virtual HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT minIndex, UINT NumVertices, UINT startIndex, UINT primCount) = 0;
    virtual HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, const void* pVertexStreamZeroData, UINT VertexStreamZeroStride) = 0;
    virtual HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertexIndices, UINT PrimitiveCount, const void* pIndexData, D3DFORMAT IndexDataFormat, const void* pVertexStreamZeroData, UINT VertexStreamZeroStride) = 0;
    virtual HRESULT STDMETHODCALLTYPE ProcessVertices(UINT SrcStartIndex, UINT DestIndex, UINT VertexCount, IDirect3DVertexBuffer8* pDestBuffer, DWORD Flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateVertexShader(const DWORD* pDeclaration, const DWORD* pFunction, DWORD* pHandle, DWORD Usage) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetVertexShader(DWORD Handle) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetVertexShader(DWORD* pHandle) = 0;
    virtual HRESULT STDMETHODCALLTYPE DeleteVertexShader(DWORD Handle) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetVertexShaderConstant(DWORD Register, const void* pConstantData, DWORD ConstantCount) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetVertexShaderConstant(DWORD Register, void* pConstantData, DWORD ConstantCount) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetVertexShaderDeclaration(DWORD Handle, void* pData, DWORD* pSizeOfData) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetVertexShaderFunction(DWORD Handle, void* pData, DWORD* pSizeOfData) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer8* pStreamData, UINT Stride) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer8** ppStreamData, UINT* pStride) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetIndices(IDirect3DIndexBuffer8* pIndexData, UINT BaseVertexIndex) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetIndices(IDirect3DIndexBuffer8** ppIndexData, UINT* pBaseVertexIndex) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD* pFunction, DWORD* pHandle) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPixelShader(DWORD Handle) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPixelShader(DWORD* pHandle) = 0;
    virtual HRESULT STDMETHODCALLTYPE DeletePixelShader(DWORD Handle) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPixelShaderConstant(DWORD Register, const void* pConstantData, DWORD ConstantCount) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPixelShaderConstant(DWORD Register, void* pConstantData, DWORD ConstantCount) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPixelShaderFunction(DWORD Handle, void* pData, DWORD* pSizeOfData) = 0;
    virtual HRESULT STDMETHODCALLTYPE DrawRectPatch(UINT Handle, const float* pNumSegs, const D3DRECTPATCH_INFO* pRectPatchInfo) = 0;
    virtual HRESULT STDMETHODCALLTYPE DrawTriPatch(UINT Handle, const float* pNumSegs, const D3DTRIPATCH_INFO* pTriPatchInfo) = 0;
    virtual HRESULT STDMETHODCALLTYPE DeletePatch(UINT Handle) = 0;
};

#if defined(AS1_D3D8_PACK4_ACTIVE)
#pragma pack(pop)
#undef AS1_D3D8_PACK4_ACTIVE
#endif

#if defined(_MSC_VER) && defined(_M_IX86)
#pragma comment(linker, "/alternatename:__imp__Direct3DCreate8@4=__imp__Direct3DCreate8")
#endif
#if defined(_MSC_VER)
extern "C" __declspec(dllimport) IDirect3D8* WINAPI Direct3DCreate8(UINT SDKVersion);
#else
extern "C" IDirect3D8* WINAPI Direct3DCreate8(UINT SDKVersion);
#endif
