// Microsoft's SDK headers do not supply MinGW's __uuidof specializations.
// Use the pinned DirectX-Headers GUID definitions, without changing dependencies.
#include <windows.h>
#include <d3d12.h>
#define WDC_GUID(T) template<> const GUID& __mingw_uuidof<T>() { return IID_##T; }
WDC_GUID(ID3D12CommandAllocator)
WDC_GUID(ID3D12CommandQueue)
WDC_GUID(ID3D12DescriptorHeap)
WDC_GUID(ID3D12Device12)
WDC_GUID(ID3D12Device8)
WDC_GUID(ID3D12Fence)
WDC_GUID(ID3D12GraphicsCommandList)
WDC_GUID(ID3D12GraphicsCommandList1)
WDC_GUID(ID3D12GraphicsCommandList4)
WDC_GUID(ID3D12PipelineState)
WDC_GUID(ID3D12QueryHeap)
WDC_GUID(ID3D12Resource)
WDC_GUID(ID3D12RootSignature)
WDC_GUID(ID3D12StateObject)
WDC_GUID(ID3D12StateObjectProperties)
#undef WDC_GUID
// Interface IDs from the pinned dxc/inc/dxcapi.h.
struct IDxcCompiler;
struct IDxcLinker;
struct IDxcUtils;
template<> const GUID& __mingw_uuidof<IDxcCompiler>() {
    static const GUID id{0x8c210bf3,0x011f,0x4422,{0x8d,0x70,0x6f,0x9a,0xcb,0x8d,0xb6,0x17}}; return id;
}
template<> const GUID& __mingw_uuidof<IDxcLinker>() {
    static const GUID id{0xf1b5be2a,0x62dd,0x4327,{0xa1,0xc2,0x42,0xac,0x1e,0x1e,0x78,0xe6}}; return id;
}
template<> const GUID& __mingw_uuidof<IDxcUtils>() {
    static const GUID id{0x4605c4cb,0x2019,0x492a,{0xad,0xa4,0x65,0xf2,0x0b,0xb7,0xd6,0x7f}}; return id;
}
